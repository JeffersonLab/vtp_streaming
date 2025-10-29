/*----------------------------------------------------------------------------*
 *  Copyright (c) 2016        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Authors: Bryan Moffit                                                   *
 *             moffit@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5660             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *     VTP library
 *
 *----------------------------------------------------------------------------*/
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>
#ifdef IPC
#include "ipc.h"
#endif
#include "vtpLib.h"

#define VTP_FT_SENDHODOSCALERS     0

/* Shared Robust Mutex for vme bus access */
char* shm_name_vtp = "/vtp";

typedef struct vtpShmData
{
  uint32_t Control;
  uint32_t Status;
  uint32_t FirmwareType;
} VTPSHMDATA;

/* Keep this as a structure, in case we want to add to it in the future */
struct shared_memory_struct
{
  pthread_mutex_t mutex;
  pthread_mutexattr_t m_attr;
  VTPSHMDATA vtp;
  uint32_t shmSize;
};
struct shared_memory_struct *p_sync=NULL;
/* mmap'd address of shared memory mutex */
void *addr_shm = NULL;

static int vtpDevOpenMASK = 0;
static int vtpFPGAFD = -1;
const char vtpFPGADev[256] = "/dev/uio0";

static int VTP_FW_Version[2];
static int VTP_FW_Type[2];

static volatile ZYNC_REGS *vtp = NULL;

static int vtpTiLinkEventReadErrors;
static int vtpEbEventReadErrors;

uint32_t vtpDebugMask = 0;

/* Mutex to guard VTP read/writes */
pthread_mutex_t   vtpMutex = PTHREAD_MUTEX_INITIALIZER;
#define VLOCK     if(pthread_mutex_lock(&vtpMutex)<0) perror("pthread_mutex_lock");
#define VUNLOCK   if(pthread_mutex_unlock(&vtpMutex)<0) perror("pthread_mutex_unlock");

#define CHECKINIT {						\
    if(vtp == NULL) {						\
      printf("%s: ERROR: VTP not initialized\n",__func__);	\
      return ERROR;						\
    }								\
  }

#define CHECKTYPE(v,c) {					    \
    if ((c!=0)&&(c!=1)) {					    \
       printf("%s: ERROR: VTP wrong Chip ID (%d)\n",__func__,c);    \
       return ERROR;                                                \
       }                                                            \
    if( (v != VTP_FW_TYPE_COMMON) && (v != VTP_FW_Type[c]) ) {         \
      printf("%s: ERROR: VTP wrong firmware type (%d)\n",__func__,v);  \
      return ERROR;						       \
    }								       \
  }


#define CHECKRANGE_INT(var, min, max) { \
    if(var > max) { \
      printf("%s: ERROR %s=%d exceeds maximum supported value %d\n", \
        __func__, #var, var, max); \
      var = max; \
    } \
    else if(var < min) { \
      printf("%s: ERROR %s=%d exceeds minimum supported value %d\n", \
        __func__, #var, var, min); \
      var = min; \
    } \
  }

#define CHECKRANGE_FLOAT(var, min, max) { \
    if(var > max) { \
      printf("%s: ERROR %s=%f exceeds maximum supported value %f\n", \
        __func__, #var, var, max); \
      var = max; \
    } \
    else if(var < min) { \
      printf("%s: ERROR %s=%f exceeds minimum supported value %f\n", \
        __func__, #var, var, min); \
      var = min; \
    } \
  }

typedef struct
{
  int serdes_chup[20];
  int serdes_txlatency_max[20];
  int serdes_rxlatency_max[20];
  char host[100];
} vtp_Limit;

vtp_Limit vtpLimits[] = {
//                            Payload Port                                           |
//      1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   16 |
//                                                                                   |
//                            VME SLOT                                               |   Fiber
//     10   13    9   14    8   15    7   16    6   17    5   18    4   19    3   20 |  1    2    3    4
    {
      {   1,   1,   1,   1,   1,   1,   1,  -1,   1,  -1,   1,  -1,   1,  -1,   1,  -1,   1,   1,   1,   1},
      {  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,1500,1000,  -1,  -1},
      { 300, 300, 300, 300, 300, 300, 300,  -1, 300,  -1, 300,  -1, 300,  -1, 300,  -1,  -1,1100,1100,1100},
      "adcft1vtp"
    }
  };

int
vtpSetDebugMask(uint32_t mask)
{
  vtpDebugMask = mask;
  return OK;
}


/*******************************************************************************
 *
 * vtpInit - Initialize JLAB VTP Library.
 *
 *
 *   iFlag: 18 bit integer
 *      bit 3-0:  Defines trig/sync/clock source
 *             1 Internal clock, software trig & sync
 *             2 VXS clock 250MHz, trig, sync
 *             3 VXS clock 125MHz, trig, sync
 *             0,4-15 undefined
 *
 *
 *      bit 16:  Exit before board initialization
 *             0 Initialize FADC (default behavior)
 *             1 Skip initialization (just setup register map pointers)
 *
 *      bit 18:  Skip firmware check.  Useful for firmware updating.
 *             0 Perform firmware check
 *             1 Skip firmware check
 *
 *
 * RETURNS: OK, or ERROR if the address is invalid or a board is not present.
 */

int
vtpInit(int iFlag)
{
  int i;
  int rval = OK;
  int syncSrc, trig1Src, clkSrc, sdStatus;

  rval = vtpCheckAddresses();
  if(rval != OK)
    return rval;

  switch(iFlag & VTP_INIT_CLK_MASK)
  {
    case VTP_INIT_CLK_INT:
      syncSrc = VTP_SD_SYNCSEL_0;
      trig1Src = VTP_SD_TRIG1SEL_0;
      clkSrc = SI5341_IN_SEL_LOCAL;
      break;

    case VTP_INIT_CLK_VXS_250:
      syncSrc = VTP_SD_SYNCSEL_VXS;
      trig1Src = VTP_SD_TRIG1SEL_VXS;
      clkSrc = SI5341_IN_SEL_VXS_250;
      break;

    case VTP_INIT_CLK_VXS_125:
      syncSrc = VTP_SD_SYNCSEL_VXS;
      trig1Src = VTP_SD_TRIG1SEL_VXS;
      clkSrc = SI5341_IN_SEL_VXS_125;
      break;

    default:
      printf("%s: ERROR invalid trig/sync/clock source specification.\n", __func__);
      break;
  }

  if(iFlag & VTP_INIT_SKIP)
  {
    for(i=0;i<2;i++) {
      VTP_FW_Version[i] = vtpGetFW_Version(i);
      VTP_FW_Type[i] = vtpGetFW_Type(i);
    }
    printf("%s: V7 Chip FW_Version=0x%x, V7 Chip FW_Type=%d\n", __func__, VTP_FW_Version[0], VTP_FW_Type[0]);
    printf("%s: Z7 Chip FW_Version=0x%x, Z7 Chip FW_Type=%d\n", __func__, VTP_FW_Version[1], VTP_FW_Type[1]);
    return rval;
  }

  vtpLock();

  if(clkSrc == SI5341_IN_SEL_LOCAL)
    printf("%s: Setting up VTP PLL for local reference\n", __func__);
  else if(clkSrc == SI5341_IN_SEL_VXS_250)
    printf("%s: Setting up VTP PLL for 250MHz VXS reference\n", __func__);
  else if(clkSrc == SI5341_IN_SEL_VXS_125)
    printf("%s: Setting up VTP PLL for 125MHz VXS reference\n", __func__);
  else
  {
    printf("%s: ERROR - unknown reference clock specified. Unable to setup VTP PLL.\n", __func__);
    vtpUnlock();
    return ERROR;
  }
  si5341_Init(clkSrc);

  vtpV7SetReset(1);
  vtpV7SetReset(0);
  usleep(10000);

  vtpV7PllReset(1);
  vtpV7PllReset(0);

  if(vtpV7PllLocked() != OK)
  {
    printf("%s: ERROR - PLL not locked.\n", __func__);
    vtpUnlock();
    return ERROR;
  }

  /* Make sure some buffers and counters are clear */
  vtpV7SetResetSoft(1);
  vtpV7SetResetSoft(0);

  for(i=0;i<2;i++) {
    VTP_FW_Type[i] = vtpGetFW_Type(i);
    VTP_FW_Version[i] = vtpGetFW_Version(i);
  }
  vtpUnlock();

  printf("%s: V7 Chip FW_Version=0x%x, V7 Chip FW_Type=%d\n", __func__, VTP_FW_Version[0], VTP_FW_Type[0]);
  printf("%s: Z7 Chip FW_Version=0x%x, Z7 Chip FW_Type=%d\n", __func__, VTP_FW_Version[1], VTP_FW_Type[1]);

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_GT:
    case VTP_FW_TYPE_HCAL:
    case VTP_FW_TYPE_PCS:
    case VTP_FW_TYPE_HTCC:
    case VTP_FW_TYPE_FTOF:
    case VTP_FW_TYPE_CND:
    case VTP_FW_TYPE_ECS:
    case VTP_FW_TYPE_FTCAL:
    case VTP_FW_TYPE_FTHODO:
    case VTP_FW_TYPE_HPS:
    case VTP_FW_TYPE_DC:
    case VTP_FW_TYPE_COMPTON:
      vtpSetTrig1Source(trig1Src);
      vtpSetSyncSource(syncSrc);
      vtpTiLinkInit();
      //    vtpEbResetFifo();

      VLOCK;
      vtp->v7.sd.FPAOSel = 0xFFFFFFFF;  /* Route trigger output to FPAO */
      vtp->v7.sd.FPBOSel = 0xFFFFFFFF;  /* Route trigger output to FPBO */
      sdStatus = vtp->v7.sd.Status;
      VUNLOCK;

      printf("VTP SD Daughtercard ID = 0x%08X\n", sdStatus);

      break;

    case VTP_FW_TYPE_FADCSTREAM:
    case VTP_FW_TYPE_VCODAROC:
      vtpSetTrig1Source(trig1Src);
      vtpSetSyncSource(syncSrc);

      // Zynq FPGA clock reset
      VLOCK;
      vtp->clk.Ctrl = 0x7;
      vtp->clk.Ctrl = 0x6;
      vtp->clk.Ctrl = 0x0;
      usleep(10000);
      printf("VTP: z7 clock status = 0x%08X\n", vtp->clk.Status);
      VUNLOCK;

      break;

    default:
      printf("%s: ERROR - unknown firmware type %d. Unable to setup VTP PLL.\n", __func__, VTP_FW_Type[0]);
      return ERROR;
  }

  vtpTiLinkEventReadErrors = 0;
  vtpEbEventReadErrors = 0;

  return rval;
}

int
vtpStatus(int pflag)
{
  int status, fw_version, fw_type, timestamp, temp;
  float t;

  CHECKINIT;

  VLOCK;
  status     = vtp->v7.clk.Status;
  fw_version = vtp->v7.clk.FW_Version;
  fw_type    = vtp->v7.clk.FW_Type;
  timestamp  = vtp->v7.clk.Timestamp;
  temp       = vtp->v7.clk.Temp;
  VUNLOCK;

  t = (float)temp * 503.975 / 4096.0 - 273.15;  
  
  printf("---------------------------------------\n");
  printf("--VTP Status                         --\n");
  printf("---------------------------------------\n");
  printf("Clock:\n");
  printf("  Global PLL locked: %d\n", (status>>0) & 0x1);
  printf("\n");
  printf("Firmware:\n");
  printf("  Type: %2d\n", fw_type);
  printf("  Version: %d.%d\n", (fw_version>>16) & 0xFFFF, (fw_version>>0) & 0xFFFF);
  printf("  Timestamp: 0x%08X\n", timestamp);
  printf("    %d/%d/%d %d:%d:%d\n",
	 ((timestamp>>17)&0x3f)+2000, ((timestamp>>23)&0xf), ((timestamp>>27)&0x1f),
	 ((timestamp>>12)&0x1f), ((timestamp>>6)&0x3f), ((timestamp>>0)&0x3f)
	 );
  printf("\n");
  printf("Misc:\n");
  printf("  Temperature: %dC\n", (int)t);

  return(OK);
}


int
vtpStats()
{
  int status, fw_version, fw_type, timestamp, temp, mig[2][2], i, rtime;
  float t, pNum;
  unsigned int ebiorx[2][2];
  unsigned int pSent[4][2], fSent[4][2], bSent[4][2];     // counts out each network port
  unsigned int tcp_ctrl,tcp_state, tcp_mtu, tcp_status, tcp_ipstatus;
  unsigned int ppmask, ppcfg[16], fcnt[4];
  int ebstatus, ebctrl[3];
  int mig_cnts[2][4];

  CHECKINIT;

  rtime = time(NULL);
  
  VLOCK;
  status     = vtp->v7.clk.Status;
  fw_version = vtp->v7.clk.FW_Version;
  fw_type    = vtp->v7.clk.FW_Type;
  timestamp  = vtp->v7.clk.Timestamp;
  temp       = vtp->v7.clk.Temp;

  tcp_ctrl   = vtp->tcpClient[0].Ctrl;
  tcp_status = vtp->tcpClient[0].Status;
  tcp_mtu    = vtp->tcpClient[0].MTU;
  tcp_state  = vtp->tcpClient[0].IP4_TCPStateStatus;
  tcp_ipstatus = vtp->tcpClient[0].IP4_TCPStatus;

  for(i=0;i<2;i++) {
    mig[i][0]        = vtp->v7.mig[i].Ctrl;
    mig[i][1]        = vtp->v7.mig[i].Status;
    mig_cnts[i][0]   = vtp->v7.mig[i].WriteCnt;
    mig_cnts[i][1]   = vtp->v7.mig[i].ReadCnt;
    mig_cnts[i][2]   = vtp->v7.mig[i].WriteDataCnt;
    mig_cnts[i][3]   = vtp->v7.mig[i].ReadDataCnt;

    ebiorx[i][0]     = vtp->ebiorx[i].Ctrl;
    ebiorx[i][1]     = vtp->ebiorx[i].Status;
  }

  // Collect data counters for each Network output port (from Ebiorx registers)
  for(i=0;i<4;i++) {

    /* Get EB frame counters.*/
    fcnt[i] = vtp->v7.streamingEb.FrameCnt[i];

    if((i==0)||(i==1)) {
      pSent[i][0]    = vtp->ebiorx[0].pkts_sent[i][0];
      pSent[i][1]    = vtp->ebiorx[0].pkts_sent[i][1];
      fSent[i][0]    = vtp->ebiorx[0].frames_sent[i][0];
      fSent[i][1]    = vtp->ebiorx[0].frames_sent[i][1];
      bSent[i][0]    = vtp->ebiorx[0].bytes_sent[i][0];
      bSent[i][1]    = vtp->ebiorx[0].bytes_sent[i][1];
    }else{
      pSent[i][0]    = vtp->ebiorx[1].pkts_sent[i-2][0];
      pSent[i][1]    = vtp->ebiorx[1].pkts_sent[i-2][1];
      fSent[i][0]    = vtp->ebiorx[1].frames_sent[i-2][0];
      fSent[i][1]    = vtp->ebiorx[1].frames_sent[i-2][1];
      bSent[i][0]    = vtp->ebiorx[1].bytes_sent[i-2][0];
      bSent[i][1]    = vtp->ebiorx[1].bytes_sent[i-2][1];
    }

  }



  if(VTP_FW_Type[0] == VTP_FW_TYPE_FADCSTREAM)
    {
      ebctrl[0]  = vtp->v7.streamingEb.Ctrl;
      ppmask = ebctrl[0]&0xffff;
      ebstatus   = vtp->v7.streamingEb.Status;
      ebctrl[1]  = vtp->v7.streamingEb.rocid;
      ebctrl[2]  = vtp->v7.streamingEb.Ctrl3;

      for(i=0;i<16;i++) {
	ppcfg[i] = vtp->v7.streamingEb.pp_cfg[i];
      }
    }

  VUNLOCK;

  t = (float)temp * 503.975 / 4096.0 - 273.15;

    printf("---------------------------------------\n");
    printf("--VTP Statistics                     --\n");
    printf("---------------------------------------\n");
    printf("Clock:\n");
    printf("  Global PLL locked: %d\n", (status>>0) & 0x1);
    printf("\n");
    printf("Temperature: %dC\n", (int)t);
    printf("\n");
    printf("Firmware:\n");
    printf("  Type: %2d\n", fw_type);
    printf("  Version: %d.%d\n", (fw_version>>16) & 0xFFFF, (fw_version>>0) & 0xFFFF);
    printf("  Timestamp: 0x%08X : %d/%d/%d %d:%d:%d \n", timestamp,
        ((timestamp>>17)&0x3f)+2000, ((timestamp>>23)&0xf), ((timestamp>>27)&0x1f),
        ((timestamp>>12)&0x1f), ((timestamp>>6)&0x3f), ((timestamp>>0)&0x3f) );
    printf("\n");
    printf("  Current time: 0x%08X\n", rtime);
    printf("    %d/%d/%d %02d:%02d:%02d\n",
        ((rtime>>17)&0x3f)+2000, ((rtime>>23)&0xf), ((rtime>>27)&0x1f),
        ((rtime>>12)&0x1f), ((rtime>>6)&0x3f), ((rtime>>0)&0x3f)
      );

    printf("\n");
    printf("TCP LINK 0 Status:\n");
    printf("    Ctrl            = %08x  (MTU=%d)\n",tcp_ctrl,tcp_mtu);
    if(tcp_status&0x80000000)
      printf("    Status          = %08x  (Client Mode)\n",tcp_status);
    else
      printf("    Status          = %08x  (Server Mode)\n",tcp_status);
    printf("    State           = %08x\n",tcp_state);
    if(tcp_ipstatus)
      printf("    Status          = %08x  (Connected)\n",tcp_ipstatus);
    else
      printf("    Status          = %08x  (No connection)\n",tcp_ipstatus);

    printf("\n");
    printf("Event Building:\n");
    printf("    MIG  Control            : %08X %08x\n", mig[0][0], mig[1][0]);
    printf("    MIG calibration complete: %8d %8d\n", mig[0][1], mig[1][1]);
    printf("    EBIORX Control          : %08X %08X\n",ebiorx[0][0], ebiorx[1][0]);

    if(VTP_FW_Type[0] == VTP_FW_TYPE_FADCSTREAM) {
      printf("    EB Control : 0x%08X\n", ebctrl[0]);
      printf("    EB Control2: 0x%08X\n", ebctrl[1]);
      printf("    EB Control3: 0x%08X\n", ebctrl[2]);
      printf("    EB Status  : 0x%08X\n", ebstatus);
      printf("    EB FrameCnt: 0x%08X\n", fcnt[0]);

      for(i=0;i<16;i++) {
	if(ppmask&(1<<i))
	   printf("      PP %2d Config: 0x%08X\n",(i+1), ppcfg[i]); 
      }   

      printf("\n");
      for(i=0;i<(ebctrl[2]&0x7);i++) {
	pNum = (float)pSent[i][0]/(float)fSent[i][0];
	printf("    NET Output Port %d   Frames : %u (%u)\n",i,fSent[i][0],fcnt[i]);
	printf("    NET Output Port %d   Packets: %u (%6.2f pkts/frame)\n",i,pSent[i][0],pNum);
	printf("    NET Output Port %d   Bytes  : 0x%08x%08x \n",i,bSent[i][1],bSent[i][0]);
	printf("\n");
      }
    }

    printf("    MIG0 WriteCnt: %u\n",     mig_cnts[0][0]);
    printf("    MIG0 ReadCnt: %u\n",      mig_cnts[0][1]);
    printf("    MIG0 WriteDataCnt: %u\n", mig_cnts[0][2]);
    printf("    MIG0 ReadDataCnt: %u\n",  mig_cnts[0][3]);
    printf("\n");
    printf("    MIG1 WriteCnt: %u\n",     mig_cnts[1][0]);
    printf("    MIG1 ReadCnt: %u\n",      mig_cnts[1][1]);
    printf("    MIG1 WriteDataCnt: %u\n", mig_cnts[1][2]);
    printf("    MIG1 ReadDataCnt: %u\n",  mig_cnts[1][3]);
    printf("\n");

  return(OK);
}



/* Print out register information for the TCPIP Network peripheral */
int
vtpNetStats()
{
  int mig[2][2], i, j;
  float pNum;
  unsigned int netmode[2], ebiorx[2][2];
  unsigned int pSent[4][2], fSent[4][2], bSent[4][2];     // counts out each network port
  unsigned int tcp_ctrl[4],tcp_state[4], tcp_mtu[4], tcp_status[4], tcp_ipstatus[4];
  unsigned int mac_status [4][4], pcs_status[4], phy_status[4], linkUp[4];
  int ebstatus, ebctrl[3], nstreams=1;
  int mig_cnts[2][4];

  CHECKINIT;

  
  VLOCK;

  for(i=0;i<4;i++) {
    tcp_ctrl[i]     = vtp->tcpClient[i].Ctrl;
    tcp_status[i]   = vtp->tcpClient[i].Status;
    tcp_mtu[i]      = vtp->tcpClient[i].MTU;
    tcp_state[i]    = vtp->tcpClient[i].IP4_TCPStateStatus;
    tcp_ipstatus[i] = vtp->tcpClient[i].IP4_TCPStatus;
    pcs_status[i]   = vtp->tcpClient[i].PCS_STATUS;
    phy_status[i]   = vtp->tcpClient[i].PHY_STATUS;
    linkUp[i] = pcs_status[i]&0x00000001;  /* Network Link status */
    for(j=0;j<4;j++) {
      mac_status[i][j] = vtp->tcpClient[i].MAC_STATUS[j];
    }
  }

  for(i=0;i<2;i++) {
    mig[i][0]        = vtp->v7.mig[i].Ctrl;
    mig[i][1]        = vtp->v7.mig[i].Status;
    mig_cnts[i][0]   = vtp->v7.mig[i].WriteCnt;
    mig_cnts[i][1]   = vtp->v7.mig[i].ReadCnt;
    mig_cnts[i][2]   = vtp->v7.mig[i].WriteDataCnt;
    mig_cnts[i][3]   = vtp->v7.mig[i].ReadDataCnt;

    ebiorx[i][0]     = vtp->ebiorx[i].Ctrl;
    ebiorx[i][1]     = vtp->ebiorx[i].Status;
    if((ebiorx[i][0])&0x100)
      netmode[i] = 1;     /* netmode>0  UDP */
    else
      netmode[i] = 0;     // TCP
  }

  // Collect data counters for each Network output port (from Ebiorx registers)
  for(i=0;i<4;i++) {

    if((i==0)||(i==1)) {
      pSent[i][0]    = vtp->ebiorx[0].pkts_sent[i][0];
      pSent[i][1]    = vtp->ebiorx[0].pkts_sent[i][1];
      fSent[i][0]    = vtp->ebiorx[0].frames_sent[i][0];
      fSent[i][1]    = vtp->ebiorx[0].frames_sent[i][1];
      bSent[i][0]    = vtp->ebiorx[0].bytes_sent[i][0];
      bSent[i][1]    = vtp->ebiorx[0].bytes_sent[i][1];
    }else{
      pSent[i][0]    = vtp->ebiorx[1].pkts_sent[i-2][0];
      pSent[i][1]    = vtp->ebiorx[1].pkts_sent[i-2][1];
      fSent[i][0]    = vtp->ebiorx[1].frames_sent[i-2][0];
      fSent[i][1]    = vtp->ebiorx[1].frames_sent[i-2][1];
      bSent[i][0]    = vtp->ebiorx[1].bytes_sent[i-2][0];
      bSent[i][1]    = vtp->ebiorx[1].bytes_sent[i-2][1];

    }

  }

  if(VTP_FW_Type[0] == VTP_FW_TYPE_FADCSTREAM)
    {
      ebctrl[0]  = vtp->v7.streamingEb.Ctrl;
      ebstatus   = vtp->v7.streamingEb.Status;
      ebctrl[1]  = vtp->v7.streamingEb.rocid;
      ebctrl[2]  = vtp->v7.streamingEb.Ctrl3;
      nstreams = ebctrl[2]&0x7;
    }

  VUNLOCK;

    printf("---------------------------------------\n");
    printf("--VTP NETWORK Statistics             --\n");
    printf("---------------------------------------\n");


    for(i=0;i<nstreams;i++) {
      printf("\n");
      printf("NETWORK LINK %d Status:\n",i);
      printf("    Ctrl            = %08x  (MTU=%d)\n",tcp_ctrl[i], tcp_mtu[i]);
      if(tcp_status[i]&0x80000000)
	printf("    Status          = %08x  (Client Mode)\n",tcp_status[i]);
      else
	printf("    Status          = %08x  (Server Mode)\n",tcp_status[i]);
      printf("    State           = %08x\n",tcp_state[i]);
      if(tcp_ipstatus[i])
	printf("    Status          = %08x  (Connected)\n",tcp_ipstatus[i]);
      else
	printf("    Status          = %08x  (No connection)\n",tcp_ipstatus[i]);
      printf("    MAC Status      = 0x%x 0x%x 0x%x 0x%x\n",
	     mac_status[i][0],mac_status[i][1],mac_status[i][2],mac_status[i][3]);
      if(linkUp[i])
	printf("    PCS Status      = 0x%08x (Link Up)\n",pcs_status[i]);
      else
	printf("    PCS Status      = 0x%08x (No link)\n",pcs_status[i]);
	
      printf("    PHY Status      = 0x%08x\n",phy_status[i]);
    }

      
    printf("\n");
    printf("Event Building:\n");
    printf("    MIG  Control            : %08X %08x\n", mig[0][0], mig[1][0]);
    printf("    MIG calibration complete: %8d %8d\n", mig[0][1], mig[1][1]);
    if(netmode[0])
      printf("    EBIORX Control          : %08X %08X (UDP)\n",ebiorx[0][0], ebiorx[1][0]);
    else
      printf("    EBIORX Control          : %08X %08X (TCP)\n",ebiorx[0][0], ebiorx[1][0]);

    if(VTP_FW_Type[0] == VTP_FW_TYPE_FADCSTREAM) {
      printf("    EB Control : 0x%08X\n", ebctrl[0]);
      printf("    EB Control2: 0x%08X\n", ebctrl[1]);
      printf("    EB Control3: 0x%08X\n", ebctrl[2]);
      printf("    EB Status  : 0x%08X\n", ebstatus);

      printf("\n");
      for(i=0;i<nstreams;i++) {
	pNum = (float)pSent[i][0]/(float)fSent[i][0];
	printf("    NET Output Port %d   Frames : %u \n",i,fSent[i][0]);
	printf("    NET Output Port %d   Packets: %u (%6.2f pkts/frame)\n",i,pSent[i][0],pNum);
	printf("    NET Output Port %d   Bytes  : 0x%08x%08x \n",i,bSent[i][1],bSent[i][0]);
	printf("\n");
      }
    }

    printf("    MIG0 WriteCnt: %u\n",     mig_cnts[0][0]);
    printf("    MIG0 ReadCnt: %u\n",      mig_cnts[0][1]);
    printf("    MIG0 WriteDataCnt: %u\n", mig_cnts[0][2]);
    printf("    MIG0 ReadDataCnt: %u\n",  mig_cnts[0][3]);
    printf("\n");
    printf("    MIG1 WriteCnt: %u\n",     mig_cnts[1][0]);
    printf("    MIG1 ReadCnt: %u\n",      mig_cnts[1][1]);
    printf("    MIG1 WriteDataCnt: %u\n", mig_cnts[1][2]);
    printf("    MIG1 ReadDataCnt: %u\n",  mig_cnts[1][3]);
    printf("\n");

  return(OK);

}


/* careful using these block level commands. The block level should
   be gotten directly from the TI in the VXS crate (from the TI Link).
   These routines should only be used when there is no TI to access */

int
vtpSetBlockLevel(int level)
  {
  CHECKINIT;

  VLOCK;
  vtp->v7.eb.BlockSize = level;
  VUNLOCK;

  return(OK);
  }

int
vtpGetBlockLevel()
{
  int rval;
  CHECKINIT;

  VLOCK;
  rval = vtp->v7.eb.BlockSize;
  VUNLOCK;

  return(rval);
}


int
vtpSetWindow(int lookback, int width)
{
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_DC:
      lookback/=8;
      width/=8;
      break;
    default:
      lookback/=4;
      width/=4;
      break;
  }

  VLOCK;
  vtp->v7.eb.Lookback = lookback;
  vtp->v7.eb.WindowWidth = width;
  VUNLOCK;

  return(OK);
}

int
vtpGetWindowLookback()
{
  int rval;
  CHECKINIT;

  VLOCK;
  rval = vtp->v7.eb.Lookback;
  VUNLOCK;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_DC:
      rval*=8;
      break;
    default:
      rval*=4;
      break;
  }

  return(rval);
}

int
vtpGetWindowWidth()
{
  int rval;
  CHECKINIT;

  VLOCK;
  rval = vtp->v7.eb.WindowWidth;
  VUNLOCK;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_DC:
      rval*=8;
      break;
    default:
      rval*=4;
      break;
  }

  return(rval);
}

int
vtpCheckAddresses()
{
  int rval = OK;
  unsigned long offset=0, expected=0, base=0;
  ZYNC_REGS test;

  printf("%s:\n\t ---------- Checking VTP register map ---------- \n",
	 __func__);

  base = (unsigned long) &test.eb.LinkCtrl;

  offset = ((unsigned long) &test.v7) - base;
  expected = 0x10000;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7 not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.clk) - base;
  expected = 0x10100;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.clk not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.sd) - base;
  expected = 0x10200;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.sd not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.sd.FPAOVal) - base;
  expected = 0x10240;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.sd.FPAOVal not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.fadcDec) - base;
  expected = 0x10300;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.fadcDec not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.vxs[0]) - base;
  expected = 0x11000;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.vxs[0] not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.qsfp[0]) - base;
  expected = 0x12000;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.qsfp[0] not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.ecTrigger[0]) - base;
  expected = 0x14100;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.ecTrigger[0] not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.trigOut) - base;
  expected = 0x15000;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.trigOut not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.eb) - base;
  expected = 0x15100;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.eb not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.hpsFeeTriggerTop) - base;
  expected = 0x15600;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.hpsFeeTriggerTop not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.hpsFeeTriggerBot) - base;
  expected = 0x15680;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.hpsFeeTriggerBot not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.hpsCluster) - base;
  expected = 0x15800;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.hpsCluster not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.hpsSingleTriggerTop[0]) - base;
  expected = 0x15900;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.hpsSingleTriggerTop[0] not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.hpsSingleTriggerBot[0]) - base;
  expected = 0x15B00;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.hpsSingleTriggerBot[0] not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.hpsMultiplicityTrigger[0]) - base;
  expected = 0x15E40;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.hpsMultiplicityTrigger[0] not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.gtBit[0]) - base;
  expected = 0x16000;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.gtBit[0] not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  offset = ((unsigned long) &test.v7.Cfg) - base;
  expected = 0x1FFFC;
  if(offset != expected)
    {
      printf("%s: ERROR VTPp->v7.Cfg not at offset = 0x%lx (@ 0x%lx)\n",
	     __func__,expected,offset);
      rval = ERROR;
    }

  return rval;
}

/* Macro for Serdes routines */
#define CHECKTYPEDEV {							\
    if(type == VTP_SERDES_VXS) {					\
      if(dev > 16) {							\
	printf("%s: ERROR: Invalid dev (%d) for type (%d)\n",		\
	       __func__, dev, type);					\
	return ERROR;							\
      }	else {								\
	sdev = &vtp->v7.vxs[dev];					\
      }									\
    } else if(type == VTP_SERDES_QSFP) {				\
      if(dev > 3) {							\
	printf("%s: ERROR: Invalid dev (%d) for type (%d)\n",		\
	       __func__, dev, type);					\
	return ERROR;							\
      }	else {								\
	sdev = &vtp->v7.qsfp[dev];					\
      }									\
    } else {								\
      printf("%s: ERROR: Invalid type (%d)\n",				\
	     __func__, type);						\
      return ERROR;							\
    }									\
  }									\

#define VTP_SERDES_MAX_TRIES  10

int
vtpSerdesCheckLinks()
{
  uint32_t i, status, ctrl, pass, tries;
  int chmask=0;
  CHECKINIT;

  for(tries=0; tries<VTP_SERDES_MAX_TRIES; tries++)
  {
    printf("Waiting on links:");
    pass = 1;

    for(i=0; i<20; i++)
    {
      VLOCK;
      if(i<16)
      {
        ctrl = vtp->v7.vxs[i].Ctrl;
        status = vtp->v7.vxs[i].Status;
      }
      else
      {
        ctrl = vtp->v7.qsfp[i-16].Ctrl;
        status = vtp->v7.qsfp[i-16].Status;
      }
      VUNLOCK;

      if(!(ctrl & VTP_SERDES_CTRL_GT_RESET))
      {
        if(!(status & VTP_SERDES_STATUS_CHUP))
        {
          if(i<16)
            printf(" PP%d", i+1);
          else
            printf(" FB%d", i-15);

          pass = 0;
        }else{
	  chmask |= (1<<i);
	}
      }
    }

    if(pass)
    {
      printf(" none. All links up!\n");
      break;
    }
    else
    {
      printf("\n");
      sleep(1);
    }
  }

  if(tries>=VTP_SERDES_MAX_TRIES)
    printf("%s: ERROR - all serdes links not up!\n", __func__);

  return chmask;
}

int
vtpSerdesStatus(int type, uint16_t dev, int pflag, int data[NSERDES])
{
  volatile SERDES_REGS *sdev;
  uint32_t status = 0, ctrl = 0, ctrl2, latency = 0;
  uint32_t chmask = 0;
  int index;
  CHECKINIT;
  CHECKTYPEDEV;


  VLOCK;
  ctrl2 = sdev->Ctrl;
  status = sdev->Status;
  latency = sdev->Latency;
  switch(VTP_FW_Type[0])
    {
    case VTP_FW_TYPE_ECS:
    case VTP_FW_TYPE_PCS:
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_FTOF:
    case VTP_FW_TYPE_HTCC:
    case VTP_FW_TYPE_CND:
    case VTP_FW_TYPE_FTHODO:
    case VTP_FW_TYPE_HPS:
    case VTP_FW_TYPE_COMPTON:
      ctrl = vtp->v7.fadcDec.Ctrl;
      break;
    case VTP_FW_TYPE_GT:
      ctrl = vtp->v7.sspDec.Ctrl;
      break;
    case VTP_FW_TYPE_DC:
      ctrl = vtp->v7.dcrbDec.Ctrl;
      break;
    case VTP_FW_TYPE_HCAL:
      ctrl = vtp->v7.hcal.Ctrl;
      break;
    case VTP_FW_TYPE_FTCAL:
      ctrl = vtp->v7.ftcalDec.Ctrl;
      break;
    case VTP_FW_TYPE_VCODAROC:   /* Check all 16 payload ports */
    case VTP_FW_TYPE_FADCSTREAM:
      ctrl = 0xffff;
    }
  VUNLOCK;

  if(type == VTP_SERDES_VXS)
    chmask = ctrl & 0xFFFF;
  else
    chmask = (ctrl >> 16) & 0xFFFF;


  if(pflag==1) /* print */
    {
      if((dev==0) && (chmask != 0))
	{
	  printf("\n");
	  printf("    -Lane--  Channel  Error  Link   Trg   Latency [ns]\n");
	  if(type == VTP_SERDES_VXS)
	    {
	      printf("PP  0 1 2 3  Status   Count  Reset  En    RX     TX\n");
	    }
	  else
	    {
	      printf("FB  0 1 2 3  Status   Count  Reset  En    RX     TX\n");
	    }
	  printf("------------------------------------------------------------------------------\n");
	}

      if((1 << dev) & chmask)
	{
	  printf("%2d  ", (dev+1));

	  printf("%s ", (status & VTP_SERDES_STATUS_LANE_UP(0))?"U":"-");
	  printf("%s ", (status & VTP_SERDES_STATUS_LANE_UP(1))?"U":"-");
	  printf("%s ", (status & VTP_SERDES_STATUS_LANE_UP(2))?"U":"-");
	  printf("%s   ", (status & VTP_SERDES_STATUS_LANE_UP(3))?"U":"-");

	  printf("%s      ", (status & VTP_SERDES_STATUS_CHUP)?" UP ":"DOWN");

	  printf("%3d  ", (status & VTP_SERDES_STATUS_SOFT_ERR_CNT_MASK)>>24);

	  printf("%d      ", (ctrl2 & VTP_SERDES_CTRL_GT_RESET) ? 1 : 0);

	  printf("%d     ", (chmask & (1<<dev)) ? 1 : 0);

	  printf("%5d  ", ((latency>>16)&0xFFFF)*4);

	  printf("%5d ", ((latency>>0)&0xFFFF)*4);

	  printf("\n");
	}
    }
  else /* send */
    {
      index = 0;

      data[index++] = (status & VTP_SERDES_STATUS_LANE_UP(0)) ? 1 : 0;
      if(index>NSERDES) return(OK);

      data[index++] = (status & VTP_SERDES_STATUS_LANE_UP(1)) ? 1 : 0;
      if(index>NSERDES) return(OK);

      data[index++] = (status & VTP_SERDES_STATUS_LANE_UP(2)) ? 1 : 0;
      if(index>NSERDES) return(OK);

      data[index++] = (status & VTP_SERDES_STATUS_LANE_UP(3)) ? 1 : 0;
      if(index>NSERDES) return(OK);

      data[index++] = (status & VTP_SERDES_STATUS_CHUP) ? 1 : 0;
      if(index>NSERDES) return(OK);

      data[index++] = (status & VTP_SERDES_STATUS_SOFT_ERR_CNT_MASK)>>24;
      if(index>NSERDES) return(OK);

      data[index++] = (ctrl2 & VTP_SERDES_CTRL_GT_RESET) ? 1 : 0;
      if(index>NSERDES) return(OK);

      if(type == VTP_SERDES_VXS) data[index++] = (ctrl & (1<<dev)) ? 1 : 0;
      else                       data[index++] = (ctrl & (1<<(dev+16))) ? 1 : 0;
      if(index>NSERDES) return(OK);

      data[index++] = ((latency>>16)&0xFFFF)*4;
      if(index>NSERDES) return(OK);

      data[index++] = ((latency>>0)&0xFFFF)*4;
      if(index>NSERDES) return(OK);
    }

  return OK;
}

int
vtpSerdesStatusAll()
{
  int i;
  int data[NSERDES];

  for(i = 0; i < 16; i++)
    vtpSerdesStatus(VTP_SERDES_VXS, i, 1, data);

  for(i = 0; i < 4; i++)
    vtpSerdesStatus(VTP_SERDES_QSFP, i, 1, data);

  return OK;
}

int
vtpSerdesEnable(int type, uint16_t idx, int enable)
{
  CHECKINIT;
  volatile SERDES_REGS *pSerdes;

  if( (type == VTP_SERDES_VXS) && (idx >= 0) && (idx < 16) )
    pSerdes = &vtp->v7.vxs[idx];
  else if( (type == VTP_SERDES_QSFP) && (idx >= 0) && (idx < 4) )
    pSerdes = &vtp->v7.qsfp[idx];
  else
{
    printf("%s: Error - invalid serdes selection(type=%d, idx=%d)\n", __func__, type, idx);
    return ERROR;
}

  VLOCK;
  if(enable)
{
    pSerdes->Ctrl = VTP_SERDES_CTRL_GT_RESET;
    usleep(10);
    pSerdes->Ctrl = 0;
    usleep(10000);
}
  else
    pSerdes->Ctrl = VTP_SERDES_CTRL_GT_RESET;
  VUNLOCK;

  return OK;
}

int
vtpSerdesSettings(int type, uint16_t idx, int txpre, int txpost, int txswing, int lpmen)
{
  CHECKINIT;
  volatile SERDES_REGS *pSerdes;

  if( (type == VTP_SERDES_VXS) && (idx >= 0) && (idx < 16) )
    pSerdes = &vtp->v7.vxs[idx];
  else if( (type == VTP_SERDES_QSFP) && (idx >= 0) && (idx < 4) )
    pSerdes = &vtp->v7.qsfp[idx];
  else
{
    printf("%s: Error - invalid serdes selection(type=%d, idx=%d)\n", __func__, type, idx);
    return ERROR;
}

  VLOCK;
  pSerdes->TrxCtrl =
    ((txpre & 0x1F)<<0) |
    ((txpost & 0x1F)<<5) |
    ((txswing & 0xF)<<10) |
    ((lpmen & 0x1)<<31);
  pSerdes->Ctrl = VTP_SERDES_CTRL_GT_RESET;
  usleep(10);
  pSerdes->Ctrl = 0;
  usleep(10000);
  VUNLOCK;

  return OK;
}

/* Payload Port configure routine. Pass an array of PP_CONF structures 
   
    port    - Payload slot ID  1-16
    module  - Module ID:  None=0, FADC250=1, MPD=2, ..., UNKNOWN=15
    lag     - Link Info:  1-> bonded lanes  0-> 4 independent lanes/modules
    bank    - Bank Info:  bits0-1   BankID for LANE1 or for a Bonded Link
      This is used by     bits8-9   BankID for LANE2
      the ROC Event       bits16-17 BankID for LANE3
      Builder             bits24-25 BankID for LANE4
    stream  - Stream Info stream number (1-4) where PP data will be output
                          otherwise 0 for triggered readout
*/
int
vtpPayloadConfig(int port, PP_CONF *ppc, int module, unsigned int lag, unsigned int bank, unsigned int stream)
{
  int ii, pp_mask = 0;
  int mod_mask = 0;

  if((port<1)||(port>16)) {
    printf("ERROR: %s:  Port ID is out of range (=%d)\n",__func__,port);
    return ERROR;
  }else{
    port = port-1;  // index from 0-15
  } 

  if((bank==0)&&(stream==0)) {   // Not allowed. If not streaming then Must have a least one Bank defined
    printf("ERROR: %s: Bank Info cannot be 0 \n",__func__);
    return ERROR;
  }

  if(lag==0) { // create a module mask for this port using bank info
    if((bank&0x03)>0)       mod_mask  = 0x0100;
    if((bank&0x0300)>0)     mod_mask |= 0x0200;
    if((bank&0x030000)>0)   mod_mask |= 0x0400;
    if((bank&0x03000000)>0) mod_mask |= 0x0800;
  }

  if((stream>0)&&(stream<=4)) { // Check that it is valid and set the stream #
    ppc[port].streamInfo = stream;
  }else{
    ppc[port].streamInfo = 0; // triggered mode
  }

  ppc[port].module_id = module;
  ppc[port].laneInfo  = mod_mask|(lag&0xff);
  ppc[port].bankInfo  = bank;

  /* establish the mask of ports that are currently configured */
  for (ii=0;ii<16;ii++) {
    if(ppc[ii].module_id)
      pp_mask |= (1<<ii);
  }

  return pp_mask;
}



int
vtpV7PllLocked()
{
  int status;

  VLOCK;
  status = vtp->v7.clk.Status;
  VUNLOCK;
  if(status & VTP_V7CLK_STATUS_GCLK_LOCKED)
  {
    printf("%s: PLL successfully locked\n",
      __func__);
  }
  else
  {
    printf("%s: PLL not locked\n",
      __func__);
    return ERROR;
  }

  return OK;
}

int
vtpV7PllReset(int enable)
{
  int status;

  if(enable)
  {
    VLOCK;
    vtp->v7.clk.Ctrl = VTP_V7CLK_CTRL_GCLK_RESET;
    VUNLOCK;
  }
  else
  {
    VLOCK;
    vtp->v7.clk.Ctrl &= ~VTP_V7CLK_CTRL_GCLK_RESET;
    VUNLOCK;
  }
  usleep(10000);

  VLOCK;
  status = vtp->v7.clk.Status;
  VUNLOCK;
  if(status & VTP_V7CLK_STATUS_GCLK_LOCKED)
  {
    printf("%s: PLL successfully locked\n",
      __func__);
  }
  else
  {
    printf("%s: PLL not locked\n",
      __func__);

    if(!enable)
      return ERROR;
  }

  return OK;
}

/* Read a 4 byte aligned offset in the V7 memory space and return a 32 bit value */
unsigned int
vtpV7Read32(unsigned int offset)
{
  unsigned int val, *ptr;

  /* Check 4 byte alignment of offset */
  if ((offset&0x3) != 0) return ERROR;

  ptr = (unsigned int *)((unsigned int) &vtp->v7 + offset);
  val = *ptr;
  
  return val;
}





int
vtpGetFW_Version(int chip)
{
  int rval=0;
  CHECKINIT;

  if (chip == 1) {
    VLOCK;
    rval = vtp->clk.FW_Version;
    VUNLOCK;
  }else{
    VLOCK;
    rval = vtp->v7.clk.FW_Version;
    VUNLOCK;
  }

  return rval;
}

int
vtpGetFW_Type(int chip)
{
  int rval=0;
  CHECKINIT;

  if(chip == 1) {
      VLOCK;
      rval = vtp->clk.FW_Type;
      VUNLOCK;
    }else{
      VLOCK;
      rval = vtp->v7.clk.FW_Type;
      VUNLOCK;
    }

  return rval;
}

static unsigned int CfgCtrl_Shadow = 0x1F;

int
vtpV7CtrlInit()
{
  CHECKINIT;

  VLOCK;
  CfgCtrl_Shadow = 0x1F;

  vtp->v7.Ctrl = CfgCtrl_Shadow;
  VUNLOCK;

  return OK;
}

int
vtpV7SetReset(int val)
{
  CHECKINIT;

  VLOCK;
  if(val)
    CfgCtrl_Shadow |= VTP_V7BRIDGE_CTRL_RESET;
  else
    CfgCtrl_Shadow &= ~VTP_V7BRIDGE_CTRL_RESET;

  vtp->v7.Ctrl = CfgCtrl_Shadow;
  VUNLOCK;

  return OK;
}

int
vtpV7SetResetSoft(int val)
{
  CHECKINIT;

  VLOCK;
  if(val)
    CfgCtrl_Shadow |= VTP_V7BRIDGE_CTRL_RESET_SOFT;
  else
    CfgCtrl_Shadow &= ~VTP_V7BRIDGE_CTRL_RESET_SOFT;

  vtp->v7.Ctrl = CfgCtrl_Shadow;
  VUNLOCK;

  return OK;
}

/* Make an easier Soft Reset for the User in the Readout list */
void
vtpV7SoftReset()
{
  vtpV7SetResetSoft(1);
  vtpV7SetResetSoft(0);
}


int
vtpV7GetDone()
{
  int rval = 0;
  CHECKINIT;

  VLOCK;
  if(vtp->v7.Status & VTP_V7BRIDGE_STATUS_DONE)
    rval = 1;
  VUNLOCK;

  return rval;
}

int
vtpV7GetInit_B()
{
  int rval = 0;
  CHECKINIT;

  VLOCK;
  if(vtp->v7.Status & VTP_V7BRIDGE_STATUS_INIT_B)
    rval = 1;
  VUNLOCK;

  return rval;
}

int
vtpV7SetProgram_B(int val)
{
  CHECKINIT;

  VLOCK;
  if(val)
    CfgCtrl_Shadow |= VTP_V7BRIDGE_CTRL_PROGRAM_B;
  else
    CfgCtrl_Shadow &= ~VTP_V7BRIDGE_CTRL_PROGRAM_B;

  vtp->v7.Ctrl = CfgCtrl_Shadow;
  VUNLOCK;

  return OK;
}

int
vtpV7SetRDWR_B(int val)
{
  CHECKINIT;

  VLOCK;
  if(val)
    CfgCtrl_Shadow |= VTP_V7BRIDGE_CTRL_RDWR_B;
  else
    CfgCtrl_Shadow &= ~VTP_V7BRIDGE_CTRL_RDWR_B;

  vtp->v7.Ctrl = CfgCtrl_Shadow;
  VUNLOCK;

  return OK;
}

int
vtpV7SetCSI_B(int val)
{
  CHECKINIT;

  VLOCK;
  if(val)
    CfgCtrl_Shadow |= VTP_V7BRIDGE_CTRL_CSI_B;
  else
    CfgCtrl_Shadow &= ~VTP_V7BRIDGE_CTRL_CSI_B;

  vtp->v7.Ctrl = CfgCtrl_Shadow;
  VUNLOCK;

  return OK;
}

void
vtpV7WriteCfgData(uint16_t *buf, int N)
{
  while(N--)
    vtp->v7.Cfg = *buf++;
}

#define V7_CFG_INITB_CNT_MAX	100000
#define V7_CFG_DONE_CNT_MAX	10000000

int
vtpV7CfgStart()
{
  int i, result;
  vtpV7SetCSI_B(1);
  vtpV7SetProgram_B(1);
  vtpV7SetRDWR_B(0);	// Write Mode

  result = vtpV7GetInit_B();
  printf("%s: Init_B = %d, Expected to be 1...%s\r\n",
	 __func__, result, (result == 0) ? "Failed" : "Okay");
  fflush(stdout);

  vtpV7SetProgram_B(0);

  result = vtpV7GetInit_B();
  printf("%s: Init_B = %d, Expected to be 0...%s\r\n",
	 __func__, result, (result == 1) ? "Failed" : "Okay");
  fflush(stdout);

  vtpV7SetProgram_B(1);
  vtpV7SetCSI_B(0);

  for(i = 0; i <= V7_CFG_INITB_CNT_MAX; i++)
    {
      result = vtpV7GetInit_B();

      if(result)
	break;
      else if(i >= V7_CFG_INITB_CNT_MAX)
	{
	  printf("%s: ERROR Init_B assert timeout\r\n", __func__);
	  return ERROR;
	}
    }

  printf("%s: end reached.\r\n", __func__);

  return OK;
}

int
vtpV7CfgLoad(char *filename)
{
  uint16_t buf[256];
  unsigned int bytesRead, i = 0;
  FILE *f;

  vtpLock();
  vtpV7CfgStart();

  printf("%s: Opening file: %s...", __func__, filename);
  f = fopen(filename, "rb");
  if(!f)
    {
      printf("FAILED\r\n");
    vtpUnlock();
      return ERROR;
    }
  printf("Opened successfully\r\n");

  while(1)
    {
      bytesRead = fread(&buf[0], 1, sizeof(buf), f);

      if(bytesRead < 0)
	{
	  printf("ERROR: fread() returned %d\r\n", bytesRead);
      vtpUnlock();
	  return ERROR;
	}

      vtpV7WriteCfgData(buf, (bytesRead+1)>>1);
      i+= bytesRead;

      if(feof(f))
	break;
    }

  fclose(f);

  printf("%s: wrote %d bytes\r\n", __func__, i);
  printf("%s: end reached.\r\n", __func__);

  vtpV7CfgEnd();


  vtpUnlock();

  return OK;
}

int
vtpV7CfgEnd()
{
  uint16_t val = 0;
  int result, i;

  for(i = 0; i <= V7_CFG_DONE_CNT_MAX; i++)
    {
      vtpV7WriteCfgData(&val, 1);

      result = vtpV7GetDone();

      if(result)
	break;
      else if(i >= V7_CFG_DONE_CNT_MAX)
	{
	  printf("%s: ERROR Done assert timeout\r\n", __func__);
	  return ERROR;
	}
    }
  for(i = 0; i < 64; i++)
    vtpV7WriteCfgData(&val, 1);

  printf("%s: end reached.\r\n", __func__);

  return OK;
}

int
vtpZ7CfgLoad(char *filename)
{
  long len = 0;
  int fd = 0;
  unsigned char *pBits;
  FILE *f = NULL;

  printf("%s: Opening file: %s...", __func__, filename);
  f = fopen(filename, "rb");
  if(!f)
  {
    printf("failed to open file %s\n", filename);
    vtpUnlock();
    return ERROR;
  }
  printf("Opened successfully\r\n");

  fseek(f, 0, SEEK_END);
  len = ftell(f);
  fseek(f, 0, SEEK_SET);

  pBits = (unsigned char *)malloc(len);
  fread(pBits, 1, len, f);
  fclose(f);

  fd = open("/dev/xdevcfg", O_WRONLY);
  if(fd < 1)
  {
    free(pBits);
    printf("failed to open device\n");
    vtpUnlock();
    return ERROR;
  }
  write(fd, pBits, len);
  close(fd);
  free(pBits);

  printf("%s: wrote %ld bytes\r\n", __func__, len);
  printf("%s: end reached.\r\n", __func__);

  return OK;
}

/* send_daq_message_to_epics(expid,session,myname,chname,chtype,nelem,data_array) */
int send_daq_message_to_epics(const char *expid, const char *session, const char *myname, const char *caname, const char *catype, int nelem, void *data);

#ifdef IPC
int
vtpSendScalers()
{
  int i, r = OK;
  char host[100];
  CHECKINIT;

  gethostname(host,sizeof(host));
  for(i=0; i<strlen(host); i++)
  {
    if(host[i] == '.')
    {
      host[i] = '\0';
      break;
    }
  }

  vtpLock();
  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_ECS:
      r = vtpEcsSendErrors(host);
      r = vtpEcsSendScalers(host);
      break;
    case VTP_FW_TYPE_PCS:
      r = vtpPcsSendErrors(host);
      r = vtpPcsSendScalers(host);
      break;
    case VTP_FW_TYPE_HTCC:
      r = vtpHtccSendErrors(host);
      r = vtpHtccSendScalers(host);
      break;
    case VTP_FW_TYPE_FTOF:
      r = vtpFtofSendErrors(host);
      r = vtpFtofSendScalers(host);
      break;
    case VTP_FW_TYPE_CND:
      r = vtpCndSendErrors(host);
      r = vtpCndSendScalers(host);
      break;
    case VTP_FW_TYPE_EC:
      break;
    case VTP_FW_TYPE_PC:
      break;
    case VTP_FW_TYPE_FTHODO:
      r = vtpFTHodoSendErrors(host);
      r = vtpFTHodoSendScalers(host);
      break;
    case VTP_FW_TYPE_GT:
      r = vtpGtSendScalers(host);
      break;
    case VTP_FW_TYPE_DC:
      r = vtpDcSendErrors(host);
      r = vtpDcSendScalers(host);
      break;
    case VTP_FW_TYPE_HCAL:
      break;
    case VTP_FW_TYPE_FTCAL:
      r = vtpFTSendErrors(host);
      r = vtpFTSendScalers(host);
      break;
    case VTP_FW_TYPE_HPS:
      r = vtpHPSSendErrors(host);
      r = vtpHPSSendScalers(host);
  }
  vtpUnlock();

  return r;
}

int
vtpSendSerdes()
{
  int i, r = OK;
  char host[100];
  char name[100];
  int data[NSERDES+1];
  int vxs_2_vmeslot[16] = {10,13,9,14,8,15,7,16,6,17,5,18,4,19,3,20};
  CHECKINIT;

  gethostname(host,sizeof(host));
  for(i=0; i<strlen(host); i++)
  {
    if(host[i] == '.')
    {
      host[i] = '\0';
      break;
    }
  }

  vtpLock();

  for(i = 0; i < 16; i++)
  {
    sprintf(name, "%s_VTP_SERDES_SLOT%d", host, vxs_2_vmeslot[i]);
    vtpSerdesStatus(VTP_SERDES_VXS, i, 0, data);
    epics_json_msg_send(name, "int", NSERDES, data);
  }

  for(i = 0; i < 4; i++)
  {
    sprintf(name, "%s_VTP_SERDES_QSFP%d", host, i);
    vtpSerdesStatus(VTP_SERDES_QSFP, i, 0, data);
    epics_json_msg_send(name, "int", NSERDES, data);
  }

  vtpUnlock();

  return r;
}
#endif

int
vtpWrite32(volatile unsigned int *addr, unsigned int val)
{
  uintptr_t pint = (uintptr_t)vtp + (uintptr_t)addr;
  volatile unsigned int *p = (volatile unsigned int *)pint;

  CHECKINIT;
  VLOCK;
    *p = val;
  VUNLOCK;

  return OK;
}

unsigned int
vtpRead32(volatile unsigned int *addr)
{
  uintptr_t pint = (uintptr_t)vtp + (uintptr_t)addr;
  volatile unsigned int *p = (volatile unsigned int *)pint;
  unsigned int val;

  CHECKINIT;
  VLOCK;
    val = *p;
  VUNLOCK;
  return val;
}

int
vtpEnableTriggerPayloadMask(int pp_mask)
{
  int i;
  CHECKINIT;

  VLOCK;
  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_ECS:
    case VTP_FW_TYPE_PCS:
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_HTCC:
    case VTP_FW_TYPE_FTOF:
    case VTP_FW_TYPE_CND:
    case VTP_FW_TYPE_FTHODO:
    case VTP_FW_TYPE_HPS:
    case VTP_FW_TYPE_COMPTON:
    case VTP_FW_TYPE_VCODAROC:
      vtp->v7.fadcDec.Ctrl = pp_mask;
      break;
    case VTP_FW_TYPE_GT:
      vtp->v7.sspDec.Ctrl = pp_mask;
      break;
    case VTP_FW_TYPE_DC:
      vtp->v7.dcrbDec.Ctrl = pp_mask;
      break;
    case VTP_FW_TYPE_HCAL:
      vtp->v7.hcal.Ctrl = pp_mask;
      break;
    case VTP_FW_TYPE_FTCAL:
      vtp->v7.ftcalDec.Ctrl = pp_mask;
      break;
  }
  VUNLOCK;

  for(i = 0; i < 16; i++)
    vtpSerdesEnable(VTP_SERDES_VXS, i, pp_mask & (1<<i));

  return OK;
}

int
vtpGetTriggerPayloadMask()
  {
  int pp_mask = 0;
  CHECKINIT;

  VLOCK;
  switch(VTP_FW_Type[0])
    {
    case VTP_FW_TYPE_ECS:
    case VTP_FW_TYPE_PCS:
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_HTCC:
    case VTP_FW_TYPE_FTOF:
    case VTP_FW_TYPE_CND:
    case VTP_FW_TYPE_FTHODO:
    case VTP_FW_TYPE_HPS:
    case VTP_FW_TYPE_COMPTON:
    case VTP_FW_TYPE_VCODAROC:
      pp_mask = vtp->v7.fadcDec.Ctrl;
      break;
    case VTP_FW_TYPE_GT:
      pp_mask = vtp->v7.sspDec.Ctrl;
      break;
    case VTP_FW_TYPE_DC:
      pp_mask = vtp->v7.dcrbDec.Ctrl;
      break;
    case VTP_FW_TYPE_HCAL:
      pp_mask = vtp->v7.hcal.Ctrl;
      break;
    case VTP_FW_TYPE_FTCAL:
      pp_mask = vtp->v7.ftcalDec.Ctrl;
      break;
    }
  VUNLOCK;

  return pp_mask;
}

int
vtpEnableTriggerFiberMask(int fiber_mask)
    {
  int i, mask;
  CHECKINIT;

  VLOCK;
  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_FTCAL:
      mask = (vtp->v7.ftcalDec.Ctrl & 0xFFFF) | (fiber_mask<<16);
      vtp->v7.ftcalDec.Ctrl = mask;
      break;
    case VTP_FW_TYPE_DC:
      vtp->v7.dcrbRoadFind.Ctrl = (fiber_mask>>1) & 0x3;
      break;
  }
  VUNLOCK;

  for(i = 0; i < 4; i++)
    vtpSerdesEnable(VTP_SERDES_QSFP, i, fiber_mask & (1<<i));

  return OK;
  }

int
vtpGetTriggerFiberMask()
{
  int val = 0;
  CHECKINIT;

  VLOCK;
  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_FTCAL:
      val = (vtp->v7.ftcalDec.Ctrl>>16) & 0xF;
      break;
    case VTP_FW_TYPE_DC:
      val = (vtp->v7.dcrbRoadFind.Ctrl & 0x3)<<1;
      break;
  }
  VUNLOCK;

  return val;
}

int
vtpSetFPAO(unsigned int val)
{
  CHECKINIT;

  VLOCK;
  vtp->v7.sd.FPAOVal = val;
  VUNLOCK;
  return OK;
}

int
vtpSetFPASel(unsigned int sel)
{
  CHECKINIT;

  VLOCK;
  vtp->v7.sd.FPAOSel = sel;
  VUNLOCK;
  return OK;
}

int
vtpSetFPBO(unsigned int val)
{
  CHECKINIT;

  VLOCK;
  vtp->v7.sd.FPBOVal = val;
  VUNLOCK;
  return OK;
}

int
vtpSetFPBSel(unsigned int sel)
{
  CHECKINIT;

  VLOCK;
  vtp->v7.sd.FPBOSel = sel;
  VUNLOCK;
  return OK;
}

int
vtpSetTrig1Source(int src)
{
  CHECKINIT;

  src &= VTP_SD_TRIG1SEL_MASK;

  if(src == VTP_SD_TRIG1SEL_0)
    printf("%s: Setting trig1 source to constant 0.\n", __func__);
  else if(src == VTP_SD_TRIG1SEL_1)
    printf("%s: Setting trig1 source to constant 1.\n", __func__);
  else //if(src == VTP_SD_TRIG1SEL_VXS)
    printf("%s: Setting trig1 source to VXS.\n", __func__);

  VLOCK;
    vtp->v7.sd.Trig1Sel = src;
  VUNLOCK;

  return OK;
}

int
vtpSetSyncSource(int src)
{
  CHECKINIT;

  src &= VTP_SD_SYNCSEL_MASK;

  if(src == VTP_SD_SYNCSEL_0)
    printf("%s: Setting sync source to constant 0.\n", __func__);
  else if(src == VTP_SD_SYNCSEL_1)
    printf("%s: Setting sync source to constant 1.\n", __func__);
  else //if(src == VTP_SD_SYNCSEL_VXS)
    printf("%s: Setting sync source to VXS.\n", __func__);

  VLOCK;
    vtp->v7.sd.SyncSel = src;
  VUNLOCK;

  return OK;
}

int
vtpStreamingReset(int mask)
{
  CHECKINIT;
//  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);
  vtp->tcpClient[0].Ctrl = mask;
  return OK;
}


int
vtpStreamingEnd()
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  VLOCK;
  vtp->v7.streamingEb.Ctrl = 0x80000000;
  vtp->tcpClient[0].IP4_StateRequest = 0;
  VUNLOCK;

  return OK;
}



/* This function configures the Payload port stream builder on the V7

    Ctrl  Reg: bit(31): Reset   bits(29-16): frame length in ns   bits(15-0): PP enable mask
    rocid Reg:                                                    bits(15-0): ROC_ID
    Ctrl3 Reg:    bit(7): Enable Input Stream Port            bit(6): Enable AsyncFifo port
               bits(5-4): Active Network port for AsyncFifo   bit(3): Enable cMsg Header  
               bits(2-0): Total # of Streams (max 4)

    pp_cfg Reg: bits(31-30): Network Port                          bits(3-0): PP Module ID (FADC=1)
 */
int
vtpStreamingSetEbCfg(int mask, int nstreams, int frame_len, int roc_id, PP_CONF *ppInfo)
{

  int ii;

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(nstreams<=0) nstreams = 1;
  
  frame_len = (frame_len+31) / 32;
  frame_len*= 32;
  frame_len--;

  CHECKRANGE_INT(frame_len, 1024, 65535);
  CHECKRANGE_INT(roc_id, 0, 127);
  CHECKRANGE_INT(nstreams,1,4);

  mask = mask & 0xFFFF;  /* Payload Port is a 16 bit mask */
  frame_len >>= 5;       /* Divide by 32 since register needs 32ns clocks */

  VLOCK;
  vtp->v7.streamingEb.Ctrl  = 0x80000000 | (frame_len<<16) | mask;
  vtp->v7.streamingEb.rocid = roc_id;
  // Enable cMsg Headers by default
  vtp->v7.streamingEb.Ctrl3 = VTP_STREB_CMSG_HDR_EN | nstreams;

  if(ppInfo != NULL) {
    for(ii=0;ii<16;ii++) {
      if((1<<ii)&mask) {
	vtp->v7.streamingEb.pp_cfg[ii] = ((ppInfo[ii].streamInfo)-1)<<30;
      }
    }
  }

  VUNLOCK;

  return OK;
}

/* Hack to replace the EB rocid register with the correct ROCID for new streaming firmware */
int
vtpStreamingSetEbRocid(int roc_id) 
{
  unsigned int temp;

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  VLOCK;
  temp = vtp->v7.streamingEb.rocid;
  vtp->v7.streamingEb.rocid = (temp&0xffff0000) | (roc_id&0xffff);
  VUNLOCK;

  return OK;
}



/*  */
int
vtpStreamingGetEbCfg(int *mask, int *nstreams, int *frame_len, int *roc_id)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);


  VLOCK;
  val = vtp->v7.streamingEb.Ctrl;
  *mask = (val>>0) & 0xFF;
  *frame_len = ((val>>16) & 0x3FFF)*4+4;

  *nstreams = (vtp->v7.streamingEb.Ctrl3)&0x7;

  val = vtp->v7.streamingEb.rocid;
  *roc_id    = (val & 0x7F);

  VUNLOCK;

  return OK;
}

/* Get the current Event builder 32bit frame counter for specified stream # only */
unsigned int
vtpStreamingGetEbFrameCnt(int stream)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if((stream<0) || (stream> 3)) stream = 0; /*default to stream 0 */

  VLOCK;
  val = vtp->v7.streamingEb.FrameCnt[stream];
  VUNLOCK;

  return val;
}




/* enable specific input data processing and then enable the EB */
int
vtpStreamingEbEnable(int mask)
{

  /* check that mask is valid  */
  if((mask&VTP_STREB_ENABLE_MASK) != 0) {
    printf("vtpStreamingEbEnable: Invalid mask (0x%x)\n",mask);
    return ERROR;
  }
  
  VLOCK;;
  vtp->v7.streamingEb.Ctrl3 |= mask;      /*set the bits */
  vtp->v7.streamingEb.Ctrl &= 0x7FFFFFFF; /* Enable the EB */
  VUNLOCK;

  return OK;
}

/* Stop specific input data (either Streams or AsyncFifo) for processing */
int
vtpStreamingEbDisable(int dmask)
{

  /* check that mask is valid  */
  if((dmask&VTP_STREB_ENABLE_MASK) != 0) {
    printf("vtpStreamingEbDisable: Invalid mask (0x%x)\n",dmask);
    return ERROR;
  }

  VLOCK;
  vtp->v7.streamingEb.Ctrl3 &= ~(dmask);
  vtp->v7.streamingEb.Ctrl |= 0x80000000;  /* Disable the EB */
  VUNLOCK;


  return OK;
}

/* Disable Reset for Streaming EB  - This allows the EB to process streams and Async Events */
void
vtpStreamingEbGo()
{
  VLOCK;
  vtp->v7.streamingEb.Ctrl &= 0x7FFFFFFF;
  VUNLOCK;
}


/* Enable Reset for Streaming EB - This stops all processing */
void
vtpStreamingEbReset()
{
  VLOCK;
  vtp->v7.streamingEb.Ctrl |= 0x80000000;
  VUNLOCK;
}




int
vtpStreamingSetTcpCfg(
    int inst,
    unsigned char ipaddr[4],
    unsigned char subnet[4],
    unsigned char gateway[4],
    unsigned char mac[6],
    unsigned char destipaddr[4],
    unsigned short destipport,
    unsigned short localport
  )
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(inst<0 || inst>3)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }

  if(localport ==0) localport = 10001;

  VLOCK;
  vtp->tcpClient[inst].IP4_StateRequest = 0;
  vtp->tcpClient[inst].IP4_Addr         = (    ipaddr[0]<<24) | (    ipaddr[1]<<16) | (    ipaddr[2]<<8) | (    ipaddr[3]<<0);
  vtp->tcpClient[inst].IP4_SubnetMask   = (    subnet[0]<<24) | (    subnet[1]<<16) | (    subnet[2]<<8) | (    subnet[3]<<0);
  vtp->tcpClient[inst].IP4_GatewayAddr  = (   gateway[0]<<24) | (   gateway[1]<<16) | (   gateway[2]<<8) | (   gateway[3]<<0);
  vtp->tcpClient[inst].MAC_ADDR[1]      =                                             (       mac[0]<<8) | (       mac[1]<<0);
  vtp->tcpClient[inst].MAC_ADDR[0]      = (       mac[2]<<24) | (       mac[3]<<16) | (       mac[4]<<8) | (       mac[5]<<0);
  vtp->tcpClient[inst].TCP_DEST_ADDR[0] = (destipaddr[0]<<24) | (destipaddr[1]<<16) | (destipaddr[2]<<8) | (destipaddr[3]<<0);
  printf("%s: TCP_DEST_ADDR (%d) = 0x%08X   %d %d %d %d \n", __func__, inst, vtp->tcpClient[inst].TCP_DEST_ADDR[0],
	 destipaddr[0],destipaddr[1],destipaddr[2],destipaddr[3]);
  vtp->tcpClient[inst].TCP_PORT[0]      = ( localport<<16 | destipport );
  VUNLOCK;

  return OK;
}

/* Generic function to support both TCP and UDP transport */
int
vtpStreamingSetNetCfg(
    int inst,  
    int mode,
    unsigned char ipaddr[4],
    unsigned char subnet[4],
    unsigned char gateway[4],
    unsigned char mac[6],
    unsigned char destipaddr[4],
    unsigned short destipport,
    unsigned short localport
  )
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(inst<0 || inst>3)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }

  if(localport ==0) localport = 10001;

  VLOCK;
  vtp->tcpClient[inst].IP4_StateRequest = 0;
  vtp->tcpClient[inst].IP4_Addr         = (    ipaddr[0]<<24) | (    ipaddr[1]<<16) | (    ipaddr[2]<<8) | (    ipaddr[3]<<0);
  vtp->tcpClient[inst].IP4_SubnetMask   = (    subnet[0]<<24) | (    subnet[1]<<16) | (    subnet[2]<<8) | (    subnet[3]<<0);
  vtp->tcpClient[inst].IP4_GatewayAddr  = (   gateway[0]<<24) | (   gateway[1]<<16) | (   gateway[2]<<8) | (   gateway[3]<<0);
  vtp->tcpClient[inst].MAC_ADDR[1]      =                                             (       mac[0]<<8) | (       mac[1]<<0);
  vtp->tcpClient[inst].MAC_ADDR[0]      = (       mac[2]<<24) | (       mac[3]<<16) | (       mac[4]<<8) | (       mac[5]<<0);

  /* For now if we are running UDP we will use jumbo frames (8000 MTU)
     otherwise we will use a standard MTU=1500 */


  if(mode) {  /* We are doing UDP transport */
    vtp->tcpClient[inst].MTU = VTP_TCPIP_MTU_JUMBO;
    vtp->tcpClient[inst].TCP_DEST_ADDR[0] = 0;
    vtp->tcpClient[inst].TCP_PORT[0] = (localport)<<16;
    vtp->tcpClient[inst].UDP_DEST_ADDR = (destipaddr[0]<<24) | (destipaddr[1]<<16) | (destipaddr[2]<<8) | (destipaddr[3]<<0);
    printf("%s: UDP_DEST_ADDR (%d) = 0x%08X   %d %d %d %d \n", __func__, inst, vtp->tcpClient[inst].UDP_DEST_ADDR,
	   destipaddr[0],destipaddr[1],destipaddr[2],destipaddr[3]);
    vtp->tcpClient[inst].UDP_PORT      = ( (localport+1)<<16 | destipport );
  }else{
    vtp->tcpClient[inst].MTU = VTP_TCPIP_MTU_DEFAULT;
    vtp->tcpClient[inst].UDP_DEST_ADDR = 0;
    vtp->tcpClient[inst].UDP_PORT = (localport+1)<<16;
    vtp->tcpClient[inst].TCP_DEST_ADDR[0] = (destipaddr[0]<<24) | (destipaddr[1]<<16) | (destipaddr[2]<<8) | (destipaddr[3]<<0);
    printf("%s: TCP_DEST_ADDR (%d) = 0x%08X   %d %d %d %d \n", __func__, inst, vtp->tcpClient[inst].TCP_DEST_ADDR[0],
	   destipaddr[0],destipaddr[1],destipaddr[2],destipaddr[3]);
    vtp->tcpClient[inst].TCP_PORT[0]      = ( localport<<16 | destipport );
  }
  VUNLOCK;

  return OK;
}


int
vtpStreamingGetTcpCfg(
    int inst,
    unsigned char ipaddr[4],
    unsigned char subnet[4],
    unsigned char gateway[4],
    unsigned char mac[6],
    unsigned char destipaddr[4],
    unsigned short *destipport,
    unsigned short *localport
  )
{
  unsigned int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(inst<0 || inst>1)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }

  VLOCK;
  val = vtp->tcpClient[inst].IP4_Addr;
  ipaddr[0] = ((val>>24)&0xFF); ipaddr[1] = ((val>>16)&0xFF); ipaddr[2] = ((val>>8)&0xFF); ipaddr[3] = ((val>>0)&0xFF);

  val = vtp->tcpClient[inst].IP4_SubnetMask;
  subnet[0] = ((val>>24)&0xFF); subnet[1] = ((val>>16)&0xFF); subnet[2] = ((val>>8)&0xFF); subnet[3] = ((val>>0)&0xFF);

  val = vtp->tcpClient[inst].IP4_GatewayAddr;
  gateway[0] = ((val>>24)&0xFF); gateway[1] = ((val>>16)&0xFF); gateway[2] = ((val>>8)&0xFF); gateway[3] = ((val>>0)&0xFF);

  val = vtp->tcpClient[inst].MAC_ADDR[1];
  mac[0] = ((val>>8)&0xFF); mac[1] = ((val>>0)&0xFF);

  val = vtp->tcpClient[inst].MAC_ADDR[0];
  mac[2] = ((val>>24)&0xFF); mac[3] = ((val>>16)&0xFF); mac[4] = ((val>>8)&0xFF); mac[5] = ((val>>0)&0xFF);

  val = vtp->tcpClient[inst].TCP_DEST_ADDR[0];
  destipaddr[0] = ((val>>24)&0xFF); destipaddr[1] = ((val>>16)&0xFF); destipaddr[2] = ((val>>8)&0xFF); destipaddr[3] = ((val>>0)&0xFF);

  printf("%s: TCP_DEST_ADDR[%d] = 0x%08X\n", __func__, inst, vtp->tcpClient[inst].TCP_DEST_ADDR[0]);
  val = vtp->tcpClient[inst].TCP_PORT[0];
  VUNLOCK;

  *destipport = (val&0xFFFF);
  *localport  = ((val&0xFFFF0000)>>16);

  return OK;
}

int
vtpStreamingGetNetCfg(
    int inst,
    unsigned char ipaddr[4],
    unsigned char subnet[4],
    unsigned char gateway[4],
    unsigned char mac[6],
    unsigned char udpaddr[4],
    unsigned char tcpaddr[4],
    unsigned int *udpport,
    unsigned int *tcpport
  )
{
  unsigned int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(inst<0 || inst>3)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }

  VLOCK;
  val = vtp->tcpClient[inst].IP4_Addr;
  ipaddr[0] = ((val>>24)&0xFF); ipaddr[1] = ((val>>16)&0xFF); ipaddr[2] = ((val>>8)&0xFF); ipaddr[3] = ((val>>0)&0xFF);

  val = vtp->tcpClient[inst].IP4_SubnetMask;
  subnet[0] = ((val>>24)&0xFF); subnet[1] = ((val>>16)&0xFF); subnet[2] = ((val>>8)&0xFF); subnet[3] = ((val>>0)&0xFF);

  val = vtp->tcpClient[inst].IP4_GatewayAddr;
  gateway[0] = ((val>>24)&0xFF); gateway[1] = ((val>>16)&0xFF); gateway[2] = ((val>>8)&0xFF); gateway[3] = ((val>>0)&0xFF);

  val = vtp->tcpClient[inst].MAC_ADDR[1];
  mac[0] = ((val>>8)&0xFF); mac[1] = ((val>>0)&0xFF);

  val = vtp->tcpClient[inst].MAC_ADDR[0];
  mac[2] = ((val>>24)&0xFF); mac[3] = ((val>>16)&0xFF); mac[4] = ((val>>8)&0xFF); mac[5] = ((val>>0)&0xFF);

  val = vtp->tcpClient[inst].TCP_DEST_ADDR[0];
  tcpaddr[0] = ((val>>24)&0xFF); tcpaddr[1] = ((val>>16)&0xFF); tcpaddr[2] = ((val>>8)&0xFF); tcpaddr[3] = ((val>>0)&0xFF);
  printf("%s: TCP_DEST_ADDR[%d] = 0x%08X\n", __func__, inst, vtp->tcpClient[inst].TCP_DEST_ADDR[0]);

  val = vtp->tcpClient[inst].UDP_DEST_ADDR;
  udpaddr[0] = ((val>>24)&0xFF); udpaddr[1] = ((val>>16)&0xFF); udpaddr[2] = ((val>>8)&0xFF); udpaddr[3] = ((val>>0)&0xFF);
  printf("%s: UDP_DEST_ADDR[%d] = 0x%08X\n", __func__, inst, vtp->tcpClient[inst].UDP_DEST_ADDR);

  *tcpport = vtp->tcpClient[inst].TCP_PORT[0];
  *udpport = vtp->tcpClient[inst].UDP_PORT;
  VUNLOCK;

  return OK;
}



int
vtpStreamingEbioTxSoftWrite(int inst, int val0, int val1, int val2, int val3, int val4)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(inst<0 || inst>1)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }

  VLOCK;
  vtp->v7.ebioTx[inst].SoftWrite[0] = val0;
  vtp->v7.ebioTx[inst].SoftWrite[1] = val1;
  vtp->v7.ebioTx[inst].SoftWrite[2] = val2;
  vtp->v7.ebioTx[inst].SoftWrite[3] = val3;
  vtp->v7.ebioTx[inst].SoftWrite[4] = val4;
  VUNLOCK;

  return OK;
}

int
vtpStreamEbioRxReset(int inst, int rst)
{
  int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(inst<0 || inst>1)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }

  VLOCK;
  val = vtp->ebiorx[inst].Ctrl;
  printf("Before = 0x%08X\n", val);

  if(rst) val |= 0x00000080;
  else    val &= 0xFFFFFF7F;
  vtp->ebiorx[inst].Ctrl = val;

  val = vtp->ebiorx[inst].Ctrl;
  printf("After = 0x%08X\n", val);
  VUNLOCK;

  return 0;
}

int
vtpStreamQsfpReset(int inst, int reset)
{
  int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(inst<0 || inst>1)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }

  VLOCK;
  val = vtp->tcpClient[inst].Ctrl;
  if(reset) val &= 0xFFFFEFFF;
  else      val |= 0x00001000;
  VUNLOCK;

  return 0;
}

// If set to 0x100 this will enable UDP mode for the EBIO RX by default
#define TCP_SKIP_EN   0x0

int
vtpStreamingSkipTcp(int inst, int skip)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(inst<0 || inst>1)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }

  VLOCK;
  if(skip) vtp->ebiorx[inst].Ctrl |= 0x00000100;
  else     vtp->ebiorx[inst].Ctrl &= 0xFFFFFEFF;
  VUNLOCK;

  return OK;
}

int
vtpStreamingMigFifoReset(int inst)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(inst<0 || inst>1)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }

  printf("%s(%d)\n", __func__, inst);
  // V7 Mig
  VLOCK;
  vtp->v7.mig[inst].Ctrl = 0x2;                 // Assert FIFO_RST
  usleep(10000);
  vtp->v7.mig[inst].Ctrl = 0x0;
  usleep(10000);
  VUNLOCK;
  return OK;
}



int
vtpStreamingEbioReset(int mode)
{
  int ii, j;

  CHECKINIT;

  /* There are 2 EBIO Peripherals that must be reset (each managing 8 payload ports) */
  /* mode>0 enables the EBIO Rx to operate in UDP packet mode */

  if(mode) mode = 0x100; /* Make sure EBIO RX is in UDP Mode */


  VLOCK;
  for(ii=0;ii<2;ii++) {

    vtp->v7.ebioTx[ii].Ctrl = 0x7; // Assert: RESET, TRAINING, FIFO_RESET
    vtp->v7.ebioTx[ii].Ctrl = 0x6; // Release RESET, ebioTx: TRAINING,FIFO_RESET

    vtp->ebiorx[ii].Ctrl = 0xBA | mode;
    usleep(10000);
    vtp->ebiorx[ii].Ctrl = 0xB8 | mode;
    usleep(10000);
    
    for(j=0;j<10;j++)
      {
	vtp->ebiorx[ii].Ctrl = 0xBA | mode;
	vtp->ebiorx[ii].Ctrl = 0xB8 | mode;
	usleep(1000);
	if(!(vtp->ebiorx[ii].Status & 0xFFFF0000))
	  break;
	vtp->ebiorx[ii].Ctrl = 0xBE | mode;
      }
    vtp->ebiorx[ii].Ctrl = 0xB8 | mode;
    usleep(1000);
    vtp->ebiorx[ii].Ctrl = 0x38 | mode;
    

    if(j != 10) printf("EBIORX[%d]     sync'd\n",ii);
    else        printf("EBIORX[%d] NOT sync'd\n",ii);
    
    // V7 evioTx reset release
    vtp->v7.ebioTx[ii].Ctrl = 0x4;
    
    // Release resets downstream->upstream
    vtp->ebiorx[ii].Ctrl = 0x78 | mode;

    vtp->v7.ebioTx[ii].Ctrl = 0x0;
    vtp->v7.ebioTx[ii].Ctrl = 0x8;
  }
  VUNLOCK;

  return OK;
}



int
vtpStreamingMigReset()
{

  int ii;

  CHECKINIT;

  printf("Calling %s()\n", __func__);

  // V7 Mig  - Two instances for each set of 8 payload ports 
  VLOCK;
  for(ii=0;ii<2;ii++) {
    vtp->v7.mig[ii].Ctrl = 0x1;         // Assert SYS_RST
    usleep(10000);
    vtp->v7.mig[ii].Ctrl = 0x2;         // Assert FIFO_RST
    usleep(10000);
    vtp->v7.mig[ii].Ctrl = 0x0;         // Release all Resets   
    usleep(10000);
  }
  VUNLOCK;

  return OK;
}






/* Note I am using the connect flag to distinguish TCP/UDP 
    connect = 0   Close the socket
    connect = 1   TCP Open Socket - check for connection completion
    connect = 2   UDP Open Socket - skip the connection loop
*/
int
vtpStreamingTcpConnect(int inst, int connect, unsigned int *cdata, int dlen)
{
  int jj;
  unsigned int temp=0;

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(inst<0 || inst>3)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }

  printf("%s(%d,%d,cdata,dlen)\n", __func__, inst, connect);


  VLOCK;
  if(connect>0)
    {
      temp = (vtp->v7.streamingEb.Ctrl3)&0xFFFFFFCF;
      vtp->tcpClient[inst].IP4_StateRequest = 0;    // tcp: disconnect socket
      vtp->v7.streamingEb.Ctrl |= 0x80000000;       // streaming_eb: RESET=1
      vtp->v7.streamingEb.Ctrl &= 0x7FFFFFFF;       // streaming_eb: RESET=0
      vtp->v7.streamingEb.Ctrl3  = (inst<<4)|temp;  // set the network port being used
      vtp->tcpClient[inst].Ctrl = 0x03C5;           // tcp: reset: phy, qsfp, tcp
      usleep(10000);


      // Z7 socket connect
      vtp->tcpClient[inst].Ctrl = 0x03C0;           // tcp: reset: qsfp
      usleep(10000);
      vtp->tcpClient[inst].Ctrl = 0x13C0;           // tcp: reset: none
      usleep(250000);
      vtp->tcpClient[inst].IP4_StateRequest = 1;    // tcp: connect to first socket
      usleep(250000);

      /* Make sure we are connected before we send any data */
      if(connect==1) {
	volatile unsigned int done=0;
	int wait=20;
	printf("%s: Connecting to Server...\n",__func__);
	while(wait>0) {
	  done = (vtp->tcpClient[inst].IP4_TCPStatus)&0xff;
	  if(done>0) {
	    printf("%s: TCP Connection - Complete! (%d)\n",__func__,(20-wait));
	    break;
	  }
	  wait--;
	  sleep(1);
	}
	//printf("out of wait loop %d\n",wait);
	if (wait<=0) printf("%s: **WARNING**: TCP Connection - May have failed!\n",__func__);
      }else{
	printf("%s: Opened UDP Socket for Stream %d\n",__func__,(inst+1));
	if(inst==0){ /* if the first socket then make sure the network link is up. Check PCS register*/ 
	  printf(" Wait 2 seconds to make sure the interface is up\n");
	  sleep(2);
	}
      }

      /* Send optional Data required to complete connection to the CODA EMU (EB) */
      if((cdata!=0)&&(dlen!=0)) {
	printf("%s: Sending connection info (%d words)\n",__func__,dlen);
	for(jj=0; jj<dlen; jj++) {
	  printf(" 0x%08x ",cdata[jj]);
	  vtp->v7.streamingEb.CpuAsyncEventData = cdata[jj];
	}
	printf("\n");
	printf("writing 0x%x to Async Info Register\n",dlen);
	vtp->v7.streamingEb.CpuAsyncEventInfo = dlen;
      }


    }
  else  /* disconnect the socket */
    {
      vtp->v7.streamingEb.Ctrl = 0x80000000;      /* Disable stream building */
      vtp->tcpClient[inst].IP4_StateRequest = 0;  
      usleep(500000);
    }
  VUNLOCK;



  return OK;
}


int
vtpStreamingEvioWriteControl(int inst, unsigned int type, unsigned int val0, unsigned int val1)
{

  unsigned int temp=0, rocid=0, netMode=0;

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if(inst<0 || inst>3)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }


  VLOCK;
  rocid = vtp->v7.streamingEb.rocid;
  temp =  (vtp->v7.streamingEb.Ctrl3)&~VTP_STREB_AFIFO_MASK;
  vtp->v7.streamingEb.Ctrl3 = (inst<<4)|temp;      // set the network port being used

  /* Check network transport mode */
  netMode = (vtp->ebiorx[0].Ctrl)&0x100;   /* if bit set then it is UDP */
  if(netMode == 0) {  /* if TCP then send cMsg Header*/
    /* cMsg Header */
    if(type == 0xffd4) // End Event
      vtp->v7.streamingEb.CpuAsyncEventData = 3;
    else
      vtp->v7.streamingEb.CpuAsyncEventData = 1;
    vtp->v7.streamingEb.CpuAsyncEventData = (13<<2);
  }

  /* EVIO Block header */
  vtp->v7.streamingEb.CpuAsyncEventData = 13;
  vtp->v7.streamingEb.CpuAsyncEventData = 0xffffffff;
  vtp->v7.streamingEb.CpuAsyncEventData = 8;
  vtp->v7.streamingEb.CpuAsyncEventData = 1;
  vtp->v7.streamingEb.CpuAsyncEventData = rocid;
  vtp->v7.streamingEb.CpuAsyncEventData = (0x1400|0x200|4); /* Control Event, Last block, evio version */  
  vtp->v7.streamingEb.CpuAsyncEventData = 0;
  vtp->v7.streamingEb.CpuAsyncEventData = 0xc0da0100;

  /* CODA Control Event */
  vtp->v7.streamingEb.CpuAsyncEventData = 4;
  vtp->v7.streamingEb.CpuAsyncEventData = ((type<<16)|(1<<8)|(0));
  vtp->v7.streamingEb.CpuAsyncEventData = 1200;
  vtp->v7.streamingEb.CpuAsyncEventData = val0;
  vtp->v7.streamingEb.CpuAsyncEventData = val1;

  if(netMode)
    vtp->v7.streamingEb.CpuAsyncEventInfo = 13;
  else
    vtp->v7.streamingEb.CpuAsyncEventInfo = 15;
  VUNLOCK;

  return OK;
}

/* routine to just write a value to the AsyncInfo Register */
void
vtpStreamingAsyncInfoWrite(int val)
{
  vtp->v7.streamingEb.CpuAsyncEventInfo = val;
}


/* Routine when the VTP TCP Stack is in Server mode.
 
   When a given TCP client instance hase been configured, it will begin 
   listening on all the configured socket ports for connection requests.

*/ 
int
vtpStreamingTcpAccept(int inst)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  vtp->tcpClient[inst].IP4_StateRequest = 0;    // tcp: disconnect socket
  vtp->tcpClient[inst].Ctrl = 0x03C5;           // tcp: reset: phy, qsfp, tcp
  usleep(10000);

  vtp->tcpClient[inst].Ctrl = 0x03C0;           // tcp: reset: qsfp
  usleep(10000);
  vtp->tcpClient[inst].Ctrl = 0x13C0;           // tcp: reset: none
  usleep(250000);

  // Z7 Socket Accept (Server mode)
  vtp->tcpClient[inst].IP4_ConnectionReset = 1;
  usleep(10000);
  vtp->tcpClient[inst].IP4_ConnectionReset = 0;


  return OK;
}

int
vtpStreamingTcpReset(int inst)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  // Reset the Socket Connection
  vtp->tcpClient[inst].IP4_ConnectionReset = 1;
  usleep(10000);
  vtp->tcpClient[inst].IP4_ConnectionReset = 0;


  return OK;
}


unsigned int
vtpStreamingFramesSent(int inst)
{
  unsigned int totalFrames;

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if((inst<0)||(inst>3)) {
      return 0xFFFFFFFF;
    }

  if((inst==0)||(inst==1)) {
    totalFrames = vtp->ebiorx[0].frames_sent[inst][0];
  }else{
    totalFrames = vtp->ebiorx[1].frames_sent[inst][0];
  }

      
    return totalFrames;
}


unsigned long long
vtpStreamingBytesSent(int inst)
{
  unsigned int bytes[2];
  unsigned long long totalBytes;

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FADCSTREAM,0);

  if((inst<0)||(inst>3)) {
      return 0xFFFFFFFF;
    }

  VLOCK;
  if((inst==0)||(inst==1)) {
    bytes[0] = vtp->ebiorx[0].bytes_sent[inst][0];
    bytes[1] = vtp->ebiorx[0].bytes_sent[inst][0];
  }else{
    bytes[0] = vtp->ebiorx[1].bytes_sent[inst-2][0];
    bytes[1] = vtp->ebiorx[1].bytes_sent[inst-2][0];
  }
  VUNLOCK

  totalBytes  =  (bytes[1])&0x00000000FFFFFFFF;
  totalBytes  = (totalBytes<<(32ll))&0xFFFFFFFF00000000;
  totalBytes += bytes[0];

      
    return totalBytes;
}




/* Include some VTP CODA ROC Functions */
#include "vtpRocLib.c"





/****************************************************/

int
vtpSetECtrig_dt(int inst, int dt)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_EC,0);

  dt = dt / 4;
  if(dt<0)
  {
    printf("%s: ERROR dt too small. Setting to minimum (0).\n", __func__);
    dt = 0;
  }
  else if(dt>8)
  {
    printf("%s: ERROR dt too large. Setting to maximum (8).\n", __func__);
    dt = 8;
  }

  VLOCK;
  val = vtp->v7.ecTrigger[inst].Hit;
  val = (val & 0xFFF0FFFF) | (dt<<16);
  vtp->v7.ecTrigger[inst].Hit = val;
  VUNLOCK;

  return OK;
}


int
vtpGetECtrig_dt(int inst, int *dt)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_EC,0);

  VLOCK;
  *dt = ((vtp->v7.ecTrigger[inst].Hit>>16) & 0xF) * 4;
  VUNLOCK;

  return OK;
}

int
vtpSetECtrig_emin(int inst, int emin)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_EC,0);

  VLOCK;
    val = vtp->v7.ecTrigger[inst].Hit;
    val = (val & 0xFFFFE000) | (emin<<0);
    vtp->v7.ecTrigger[inst].Hit = val;
  VUNLOCK;

  return OK;
}

int
vtpGetECtrig_emin(int inst, int *emin)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_EC,0);

  VLOCK;
  *emin = vtp->v7.ecTrigger[inst].Hit & 0x1FFF;
  VUNLOCK;

  return OK;
}

int
vtpSetECtrig_peak_multmax(int inst, int mult_max)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_EC,0);

  VLOCK;
    val = vtp->v7.ecTrigger[inst].Hit;
    val = (val & 0xE0FFFFFF) | (mult_max<<24);
    vtp->v7.ecTrigger[inst].Hit = val;
  VUNLOCK;

  return OK;
}

int
vtpGetECtrig_peak_multmax(int inst, int *mult_max)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_EC,0);

  VLOCK;
  *mult_max = (vtp->v7.ecTrigger[inst].Hit & 0x1F000000)>>24;
  VUNLOCK;

  return OK;
}

int
vtpSetECtrig_dalitz(int inst, int min, int max)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_EC,0);

  VLOCK;
    vtp->v7.ecTrigger[inst].Dalitz = (max<<16) | (min<<0);
  VUNLOCK;

  return OK;
}

int
vtpGetECtrig_dalitz(int inst, int *min, int *max)
{
  int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_EC,0);

  VLOCK;
  val = vtp->v7.ecTrigger[inst].Dalitz;
  *min = (val>>0) & 0x3FF;
  *max = (val>>16) & 0x3FF;
  VUNLOCK;

  return OK;
}

int
vtpSetFadcSum_MaskEn(unsigned int mask[16])
{
  int i;
  unsigned int val;
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_ECS:
    case VTP_FW_TYPE_FTCAL:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type %d\n",__func__, VTP_FW_Type[0]);
      return ERROR;
  }

  VLOCK;
  for(i=0; i<8; i++)
  {
    val = mask[2*i+0] & 0xFFFF;
    val|= (mask[2*i+1] & 0xFFFF)<<16;
    vtp->v7.fadcSum.SumEn[i] = val;
  }
  VUNLOCK;

  return OK;
}

int
vtpGetFadcSum_MaskEn(unsigned int mask[16])
{
  int i;
  unsigned int val;
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_ECS:
    case VTP_FW_TYPE_FTCAL:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  for(i=0; i<8;i++)
  {
    val = vtp->v7.fadcSum.SumEn[i];
    mask[2*i+0] = val & 0xFFFF;
    mask[2*i+1] = (val>>16) & 0xFFFF;
  }
  VUNLOCK;

  return OK;
}

int
vtpSetECcosmic_emin(int inst, int emin)
{
  uint32_t val;
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_ECS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  val = vtp->v7.ecCosmic[inst].Ctrl;
  val = (val & ~VTP_ECCOSMIC_CTRL_EMIN_MASK) | emin;
  vtp->v7.ecCosmic[inst].Ctrl = val;
  VUNLOCK;

  return OK;

}

int
vtpGetECcosmic_emin(int inst, int *emin)
{
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_ECS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  *emin = vtp->v7.ecCosmic[inst].Ctrl & VTP_ECCOSMIC_CTRL_EMIN_MASK;
  VUNLOCK;

  return OK;
}

int
vtpSetECcosmic_multmax(int inst, int multmax)
{
  uint32_t val;
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_ECS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  val = vtp->v7.ecCosmic[inst].Ctrl;
  val = (val & ~VTP_ECCOSMIC_CTRL_MULTMAX_MASK) | (multmax<<24);
  vtp->v7.ecCosmic[inst].Ctrl = val;
  VUNLOCK;

  return OK;

}

int
vtpGetECcosmic_multmax(int inst, int *multmax)
{
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_ECS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  *multmax = (vtp->v7.ecCosmic[inst].Ctrl & VTP_ECCOSMIC_CTRL_MULTMAX_MASK)>>24;
  VUNLOCK;

  return OK;
}

int
vtpSetECcosmic_width(int inst, int hitwidth)
{
  uint32_t val;
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_ECS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  val = vtp->v7.ecCosmic[inst].Delay;
  val = (val & ~VTP_ECCOSMIC_DELAY_WIDTH_MASK) | (hitwidth<<0);
  vtp->v7.ecCosmic[inst].Delay = val;
  VUNLOCK;

  return OK;

}

int
vtpGetECcosmic_width(int inst, int *hitwidth)
{
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_ECS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  *hitwidth = (vtp->v7.ecCosmic[inst].Delay & VTP_ECCOSMIC_DELAY_WIDTH_MASK)>>0;
  VUNLOCK;

  return OK;
}

int
vtpSetECcosmic_delay(int inst, int evaldelay)
{
  uint32_t val;
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_ECS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  val = vtp->v7.ecCosmic[inst].Delay;
  val = (val & ~VTP_ECCOSMIC_DELAY_EVAL_MASK) | (evaldelay<<16);
  vtp->v7.ecCosmic[inst].Delay = val;
  VUNLOCK;

  return OK;

}

int
vtpGetECcosmic_delay(int inst, int *evaldelay)
{
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_EC:
    case VTP_FW_TYPE_ECS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  *evaldelay = (vtp->v7.ecCosmic[inst].Delay & VTP_ECCOSMIC_DELAY_EVAL_MASK)>>16;
  VUNLOCK;

  return OK;
}

int
vtpSetPCcosmic_emin(int emin)
{
  uint32_t val;
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_PCS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  val = vtp->v7.pcCosmic.Ctrl;
  val = (val & ~VTP_PCCOSMIC_CTRL_EMIN_MASK) | emin;
  vtp->v7.pcCosmic.Ctrl = val;
  VUNLOCK;

  return OK;

}

int
vtpGetPCcosmic_emin(int *emin)
{
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_PCS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  *emin = vtp->v7.pcCosmic.Ctrl & VTP_PCCOSMIC_CTRL_EMIN_MASK;
  VUNLOCK;

  return OK;
}

int
vtpSetPCcosmic_multmax(int multmax)
{
  uint32_t val;
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_PCS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  val = vtp->v7.pcCosmic.Ctrl;
  val = (val & ~VTP_PCCOSMIC_CTRL_MULTMAX_MASK) | (multmax<<24);
  vtp->v7.pcCosmic.Ctrl = val;
  VUNLOCK;

  return OK;

}

int
vtpGetPCcosmic_multmax(int *multmax)
{
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_PCS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  *multmax = (vtp->v7.pcCosmic.Ctrl & VTP_PCCOSMIC_CTRL_MULTMAX_MASK)>>24;
  VUNLOCK;

  return OK;
}

int
vtpSetPCcosmic_width(int hitwidth)
{
  uint32_t val;
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_PCS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  val = vtp->v7.pcCosmic.Delay;
  val = (val & ~VTP_PCCOSMIC_DELAY_WIDTH_MASK) | (hitwidth<<0);
  vtp->v7.pcCosmic.Delay = val;
  VUNLOCK;

  return OK;

}

int
vtpGetPCcosmic_width(int *hitwidth)
{
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_PCS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  *hitwidth = (vtp->v7.pcCosmic.Delay & VTP_PCCOSMIC_DELAY_WIDTH_MASK)>>0;
  VUNLOCK;

  return OK;
}

int
vtpSetPCcosmic_delay(int evaldelay)
{
  uint32_t val;
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_PCS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  val = vtp->v7.pcCosmic.Delay;
  val = (val & ~VTP_PCCOSMIC_DELAY_EVAL_MASK) | (evaldelay<<16);
  vtp->v7.pcCosmic.Delay = val;
  VUNLOCK;

  return OK;

}

int
vtpGetPCcosmic_delay(int *evaldelay)
{
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_PCS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  *evaldelay = (vtp->v7.pcCosmic.Delay & VTP_PCCOSMIC_DELAY_EVAL_MASK)>>16;
  VUNLOCK;

  return OK;
}



int
vtpSetPCcosmic_pixel(int enable)
{
  uint32_t val;
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_PCS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  if(enable)
    enable = 1;

  VLOCK;
  val = vtp->v7.pcCosmic.Ctrl;
  val = (val & ~VTP_PCCOSMIC_CTRL_PIXEL_MASK) | (enable<<16);
  vtp->v7.pcCosmic.Ctrl = val;
  VUNLOCK;

printf("%s(%d): 0x%08X, 0x%08X\n", __func__, enable, val, vtp->v7.pcCosmic.Ctrl);
  return OK;

}

int
vtpGetPCcosmic_pixel(int *enable)
{
  CHECKINIT;

  switch(VTP_FW_Type[0])
  {
    case VTP_FW_TYPE_PC:
    case VTP_FW_TYPE_PCS:
      break;
    default:
      printf("%s: ERROR: VTP wrong firmware type\n",__func__);
      return ERROR;
  }

  VLOCK;
  *enable = (vtp->v7.pcCosmic.Ctrl & VTP_PCCOSMIC_CTRL_PIXEL_MASK)>>16;
  VUNLOCK;

  return OK;
}

/* FT functions */

int
vtpSetFTCALseed_emin(int emin)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTCAL,0);

  VLOCK;
  val = vtp->v7.ftcalTrigger.Ctrl;
  val = (val & ~VTP_FTCAL_CTRL_SEEDTHR_MASK) | (emin<<0);
  vtp->v7.ftcalTrigger.Ctrl = val;
  VUNLOCK;

  return OK;

}

int
vtpGetFTCALseed_emin(int *emin)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTCAL,0);

  VLOCK;
  *emin = (vtp->v7.ftcalTrigger.Ctrl & VTP_FTCAL_CTRL_SEEDTHR_MASK)>>0;
  VUNLOCK;

  return OK;
}


int
vtpSetFTCALseed_dt(int dt)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTCAL,0);

  VLOCK;
  dt = dt/4;
  val = vtp->v7.ftcalTrigger.Ctrl;
  val = (val & ~VTP_FTCAL_CTRL_SEEDDT_MASK) | ((dt&0x7)<<16);
  vtp->v7.ftcalTrigger.Ctrl = val;
  VUNLOCK;

  return OK;

}

int
vtpGetFTCALseed_dt(int *dt)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTCAL,0);

  VLOCK;
  *dt = ((vtp->v7.ftcalTrigger.Ctrl & VTP_FTCAL_CTRL_SEEDDT_MASK)>>16)*4;
  VUNLOCK;

  return OK;
}

int
vtpSetFTCALhodo_dt(int dt)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTCAL,0);

  VLOCK;
  dt = dt/4;
  val = vtp->v7.ftcalTrigger.Ctrl;
  val = (val & ~VTP_FTCAL_CTRL_HODODT_MASK) | ((dt&0x7)<<24);
  vtp->v7.ftcalTrigger.Ctrl = val;
  VUNLOCK;

  return OK;

}

int
vtpGetFTCALhodo_dt(int *dt)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTCAL,0);

  VLOCK;
  *dt = ((vtp->v7.ftcalTrigger.Ctrl & VTP_FTCAL_CTRL_HODODT_MASK)>>24)*4;
  VUNLOCK;

  return OK;
}


int
vtpSetFTHODOemin(int emin)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTHODO,0);

  VLOCK;
  val = vtp->v7.fthodoTrigger.Ctrl;
  val = (emin & VTP_FTHODO_CTRL_EMIN_MASK)<<0;
  vtp->v7.fthodoTrigger.Ctrl = val;
  VUNLOCK;

  return OK;

}

int
vtpGetFTHODOemin(int *emin)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTHODO,0);

  VLOCK;
  *emin = (vtp->v7.fthodoTrigger.Ctrl & VTP_FTHODO_CTRL_EMIN_MASK)>>0;
  VUNLOCK;

  return OK;
}

int
vtpSetFTCALcluster_deadtime(int deadtime)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTCAL,0);

  VLOCK;
  deadtime = deadtime/4;
  val = vtp->v7.ftcalTrigger.DeadtimeCtrl;
  val = (val & ~VTP_FTCAL_DEADTIMECTRL_DEADTIME_MASK) | ((deadtime&0x3f)<<16);
  vtp->v7.ftcalTrigger.DeadtimeCtrl = val;
  VUNLOCK;

  return OK;

}

int
vtpGetFTCALcluster_deadtime(int *deadtime)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTCAL,0);

  VLOCK;
  *deadtime = ((vtp->v7.ftcalTrigger.DeadtimeCtrl & VTP_FTCAL_DEADTIMECTRL_DEADTIME_MASK)>>16)*4;
  VUNLOCK;

  return OK;
}

int
vtpSetFTCALcluster_deadtime_emin(int emin)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTCAL,0);

  VLOCK;
  val = vtp->v7.ftcalTrigger.DeadtimeCtrl;
  val = (emin & VTP_FTCAL_DEADTIMECTRL_EMIN_MASK)<<0;
  vtp->v7.ftcalTrigger.DeadtimeCtrl = val;
  VUNLOCK;

  return OK;

}

int
vtpGetFTCALcluster_deadtime_emin(int *emin)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTCAL,0);

  VLOCK;
  *emin = (vtp->v7.ftcalTrigger.DeadtimeCtrl & VTP_FTCAL_DEADTIMECTRL_EMIN_MASK)>>0;
  VUNLOCK;

  return OK;
}

#ifdef IPC
int
vtpFTSendErrors(char *host)
{
  char name[100];
  int data[NSERDES];
  unsigned int val;
  int i;
  CHECKINIT;

  for(i=0;i<4;i++)
  {
    vtpSerdesStatus(VTP_SERDES_QSFP, i, 0, data);

    if(data[4] & 0x1) // check channel up bit
      val |= (1<<i);
    else
      val &= ~(1<<i);
  }

  if(!(val & 0x1))  // Fiber 0 = trig2 SSP_SLOT9
  {
    sprintf(name, "err: crate=%s link to trig2 SSP_SLOT9 down", host);
    epics_json_msg_send(name, "int", 1, &data);
  }
  if(!(val & 0x2))  // Fiber 1 = adcftXvtp
  {
    sprintf(name, "err: crate=%s,adcft1vtp link to adcft2vtp down", host);
    epics_json_msg_send(name, "int", 1, &data);
  }
  if(!(val & 0x4) || !(val & 0x8))  // Fiber 2,3 = adcft3vtp
  {
    sprintf(name, "err: crate=%s,link to adcft3vtp down", host);
    epics_json_msg_send(name, "int", 1, &data);
  }

  return OK;
}

int
vtpFTSendScalers(char *host)
{
  char name[100];
  float ref, data[1024];
  unsigned int val;
  int i;
  CHECKINIT;

  printf("%s...", __func__);

  VLOCK;
#if VTP_FT_SENDHODOSCALERS
  vtp->v7.sd.ScalerLatch = 1;
  //Read/normalize reference
  val = vtp->v7.sd.Scaler_BusClk;
  if(!val) val = 1;
  ref = 33330000.0f / (float)val;

  for(i=0;i<256;i++)
  {
    data[i] = ref * (float)vtp->v7.fthodoScalers.Scalers[i];
    printf("vtp->v7.fthodoScalers.Scalers[%3d]=%9d\n", i, vtp->v7.fthodoScalers.Scalers[i]);
  }
  vtp->v7.sd.ScalerLatch = 0;
  sprintf(name, "%s_VTPFT_HODOSCALERS", host);
  epics_json_msg_send(name, "float", 256, data);
#endif

  vtp->v7.ftcalTrigger.HistCtrl = 0x60000000;
  val = vtp->v7.ftcalTrigger.HistTime;
  if(!val) val = 1;

  ref = 1000000.0f / (float)val;

  // Cluster position histogram (all)
  for(i=0;i<1024;i++)
    data[i] = ref * (float)vtp->v7.ftcalTrigger.HistPos;
  sprintf(name, "%s_VTPFT_CLUSTERPOSITION", host);
  epics_json_msg_send(name, "float", 1024, data);

  // Cluster energy histogram (all)
  for(i=0;i<1024;i++)
    data[i] = ref * (float)vtp->v7.ftcalTrigger.HistEnergy;
  sprintf(name, "%s_VTPFT_CLUSTERENERGY", host);
  epics_json_msg_send(name, "float", 1024, data);

  // Cluster nhits histogram (all)
  for(i=0;i<9;i++)
    data[i] = ref * (float)vtp->v7.ftcalTrigger.HistNHits;
  sprintf(name, "%s_VTPFT_CLUSTERHITS", host);
  epics_json_msg_send(name, "float", 9, data);

  // Cluster position histogram (hodo tagged)
  for(i=0;i<1024;i++)
    data[i] = ref * (float)vtp->v7.ftcalTrigger.HistPosHodo;
  sprintf(name, "%s_VTPFT_CLUSTERPOSITION_HODO", host);
  epics_json_msg_send(name, "float", 1024, data);

  // Cluster energy histogram (hodo tagged)
  for(i=0;i<1024;i++)
    data[i] = ref * (float)vtp->v7.ftcalTrigger.HistEnergyHodo;
  sprintf(name, "%s_VTPFT_CLUSTERENERGY_HODO", host);
  epics_json_msg_send(name, "float", 1024, data);

  // Cluster nhits histogram (hodo tagged)
  for(i=0;i<9;i++)
    data[i] = ref * (float)vtp->v7.ftcalTrigger.HistNHitsHodo;
  sprintf(name, "%s_VTPFT_CLUSTERNHITS_HODO", host);
  epics_json_msg_send(name, "float", 9, data);

  vtp->v7.ftcalTrigger.HistCtrl = 0x60000000 | 0x7F;
  VUNLOCK;

  return OK;
}

int
vtpFTHodoSendErrors(char *host)
{
  char name[100];
  int data[NSERDES];
  unsigned int val;
  int i;
  CHECKINIT;

  for(i=0;i<4;i++)
  {
    vtpSerdesStatus(VTP_SERDES_QSFP, i, 0, data);

    if(data[4] & 0x1) // check channel up bit
      val |= (1<<i);
    else
      val &= ~(1<<i);
  }

  if(!(val & 0x1) || !(val & 0x2))  // Fiber 0,1 = adcft1vtp
  {
    sprintf(name, "err: crate=%s link to adcft1vtp down", host);
    epics_json_msg_send(name, "int", 1, &data);
  }
  if(!(val & 0x4) || !(val & 0x8))  // Fiber 2,3 = adcft2vtp
  {
    sprintf(name, "err: crate=%s,link to adcft2vtp down", host);
    epics_json_msg_send(name, "int", 1, &data);
  }

  return OK;
}

int
vtpFTHodoSendScalers(char *host)
{
/*
  char name[100];
  float ref, data[1024];
  unsigned int val;
  int i;
  CHECKINIT;

  printf("%s...", __func__);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;
  //Read/normalize reference
  val = vtp->v7.sd.Scaler_BusClk;
  if(!val) val = 1;
  ref = 33330000.0f / (float)val;

  for(i=0;i<256;i++)
    data[i] = ref * (float)vtp->v7.fthodoScalers.Scalers[i];
  vtp->v7.sd.ScalerLatch = 0;
  sprintf(name, "%s_VTPFT_HODOSCALERS", host);
  epics_json_msg_send(name, "float", 256, data);
  VUNLOCK;
*/
  return OK;
}
#endif


/*
 *
 sel = 0: clusters, no hodo tag
       1: clusters, l1 hodo tag
       2: clusters, l2 hodo tag
       3: clusters, l1*l2 hodo tag
       4: l1 hodo hits (tagged cal pixels)
       5: l2 hodo hits (tagged cal pixels)
 *
 */

int
vtpFTSelectHist(int sel)
{
  VLOCK;
  vtp->v7.ftcalTrigger.HistCtrl &= 0x1FFFFFFF | (sel<<29);
  VUNLOCK;

  return OK;
}

/* HPS functions */

int
vtpSetHPS_Cluster(int top_nbottom, int hit_dt, int seed_thr)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  hit_dt/= 4;

  CHECKRANGE_INT(top_nbottom, 0,    1);
  CHECKRANGE_INT(hit_dt,      0,    4);
  CHECKRANGE_INT(seed_thr,    1, 8191);

  VLOCK;
  vtp->v7.hpsCluster.Ctrl = (top_nbottom<<31) | (hit_dt<<16) | (seed_thr<<0);
  VUNLOCK;

  return OK;
}

int
vtpGetHPS_Cluster(int *top_nbottom, int *hit_dt, int *seed_thr)
{
  int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  VLOCK;
  val = vtp->v7.hpsCluster.Ctrl;
  VUNLOCK;

  *top_nbottom = (val>>31) & 0x1;
  *hit_dt      = (val>>16) & 0x7;
  *seed_thr    = (val>>0)  & 0x1FFF;

  *hit_dt *= 4;

  return OK;
}

int
vtpSetHPS_Hodoscope(int hit_width, int fadchit_thr, int hodo_thr)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  hit_width/= 4;

  CHECKRANGE_INT(hit_width,   0,   15);
  CHECKRANGE_INT(fadchit_thr, 1, 8191);
  CHECKRANGE_INT(hodo_thr,    1, 8191);

  VLOCK;
  vtp->v7.hpsHodoscope.Ctrl = (hit_width<<26) | (hodo_thr<<13) | (fadchit_thr<<0);
  VUNLOCK;

  return OK;
}

int
vtpGetHPS_Hodoscope(int *hit_width, int *fadchit_thr, int *hodo_thr)
{
  int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  VLOCK;
  val = vtp->v7.hpsHodoscope.Ctrl;
  VUNLOCK;

  *hit_width   = (val>>26) & 0xF;
  *hodo_thr    = (val>>13) & 0x1FFF;
  *fadchit_thr = (val>>0)  & 0x1FFF;

  *hit_width*= 4;

  return OK;
}

int
vtpSetHPS_SingleTrigger(
    int inst, int top_nbottom, int cluster_emin, int cluster_emax,
    int cluster_nmin, int cluster_xmin, float cluster_pde_c[4],
    int enable_flags)
{
  int i, c[4];
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  CHECKRANGE_INT(inst         ,   0,    3);
  CHECKRANGE_INT(top_nbottom  ,   0,    1);
  CHECKRANGE_INT(cluster_emin ,   0, 8191);
  CHECKRANGE_INT(cluster_emax ,   0, 8191);
  CHECKRANGE_INT(cluster_nmin ,   0,    9);
  CHECKRANGE_INT(cluster_xmin , -31,   31);
  CHECKRANGE_FLOAT(cluster_pde_c[0], -32767.0, 32767.0);
  CHECKRANGE_FLOAT(cluster_pde_c[1], -32767.0, 32767.0);
  CHECKRANGE_FLOAT(cluster_pde_c[2], -32767.0, 32767.0);
  CHECKRANGE_FLOAT(cluster_pde_c[3], -32767.0, 32767.0);

  for(i=0;i<4;i++)
    c[i] = (int)(cluster_pde_c[i] * 65536.0);

  VLOCK;
  if(top_nbottom)
  {
    vtp->v7.hpsSingleTriggerTop[inst].Ctrl             = enable_flags;
    vtp->v7.hpsSingleTriggerTop[inst].Cluster_Emin     = cluster_emin;
    vtp->v7.hpsSingleTriggerTop[inst].Cluster_Emax     = cluster_emax;
    vtp->v7.hpsSingleTriggerTop[inst].Cluster_Nmin     = cluster_nmin;
    vtp->v7.hpsSingleTriggerTop[inst].Cluster_Xmin     = cluster_xmin;

    for(i=0;i<4;i++)
      vtp->v7.hpsSingleTriggerTop[inst].Cluster_PDE_C[i] = c[i];
  }
  else
  {
    vtp->v7.hpsSingleTriggerBot[inst].Ctrl             = enable_flags;
    vtp->v7.hpsSingleTriggerBot[inst].Cluster_Emin     = cluster_emin;
    vtp->v7.hpsSingleTriggerBot[inst].Cluster_Emax     = cluster_emax;
    vtp->v7.hpsSingleTriggerBot[inst].Cluster_Nmin     = cluster_nmin;
    vtp->v7.hpsSingleTriggerBot[inst].Cluster_Xmin     = cluster_xmin;

    for(i=0;i<4;i++)
      vtp->v7.hpsSingleTriggerBot[inst].Cluster_PDE_C[i] = c[i];
  }
  VUNLOCK;

  return OK;
}

int
vtpGetHPS_SingleTrigger(
    int inst, int top_nbottom, int *cluster_emin, int *cluster_emax,
    int *cluster_nmin, int *cluster_xmin, float cluster_pde_c[4],
    int *enable_flags)
{
  int i, c[4];
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  CHECKRANGE_INT(inst         ,   0,    3);

  VLOCK;
  if(top_nbottom)
  {
    *enable_flags = vtp->v7.hpsSingleTriggerTop[inst].Ctrl;
    *cluster_emin = vtp->v7.hpsSingleTriggerTop[inst].Cluster_Emin;
    *cluster_emax = vtp->v7.hpsSingleTriggerTop[inst].Cluster_Emax;
    *cluster_nmin = vtp->v7.hpsSingleTriggerTop[inst].Cluster_Nmin;
    *cluster_xmin = vtp->v7.hpsSingleTriggerTop[inst].Cluster_Xmin;

    if(*cluster_xmin & 0x20) *cluster_xmin|= 0xFFFFFFC0;

    for(i=0;i<4;i++)
      c[i] = vtp->v7.hpsSingleTriggerTop[inst].Cluster_PDE_C[i];
  }
  else
  {
    *enable_flags = vtp->v7.hpsSingleTriggerBot[inst].Ctrl;
    *cluster_emin = vtp->v7.hpsSingleTriggerBot[inst].Cluster_Emin;
    *cluster_emax = vtp->v7.hpsSingleTriggerBot[inst].Cluster_Emax;
    *cluster_nmin = vtp->v7.hpsSingleTriggerBot[inst].Cluster_Nmin;
    *cluster_xmin = vtp->v7.hpsSingleTriggerBot[inst].Cluster_Xmin;

    for(i=0;i<4;i++)
      c[i] = vtp->v7.hpsSingleTriggerBot[inst].Cluster_PDE_C[i];
  }
  VUNLOCK;

  for(i=0;i<4;i++)
    cluster_pde_c[i] = c[i] / 65536.0;

  return OK;
}

int
vtpSetHPS_PairTrigger(
    int inst, int cluster_emin, int cluster_emax, int cluster_nmin,
    int pair_dt, int pair_esum_min, int pair_esum_max, int pair_ediff_max,
    float pair_ed_factor, int pair_ed_thr, int pair_coplanarity_tol,
    int enable_flags
  )
{
  int f;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  pair_dt/= 4;

  CHECKRANGE_INT(inst                 ,   0,    3);
  CHECKRANGE_INT(cluster_emin         ,   0, 8191);
  CHECKRANGE_INT(cluster_emax         ,   0, 8191);
  CHECKRANGE_INT(cluster_nmin         ,   0,    9);
  CHECKRANGE_INT(pair_dt              ,   0,   15);
  CHECKRANGE_INT(pair_esum_min        ,   0, 8191);
  CHECKRANGE_INT(pair_esum_max        ,   0,16383);
  CHECKRANGE_INT(pair_ediff_max       ,   0, 8191);
  CHECKRANGE_INT(pair_ed_thr          ,   0, 8191);
  CHECKRANGE_INT(pair_coplanarity_tol ,   0,  255);
  CHECKRANGE_INT(pair_ed_thr          ,   0, 8191);
  CHECKRANGE_FLOAT(pair_ed_factor     , 0.0, 15.9375);

  f = (int)(pair_ed_factor * 16.0);

  VLOCK;
  vtp->v7.hpsPairTrigger[inst].Ctrl             = enable_flags | (pair_dt<<16);
  vtp->v7.hpsPairTrigger[inst].Pair_Esum        = (pair_esum_max<<16) | (pair_esum_min<<0);
  vtp->v7.hpsPairTrigger[inst].Pair_Ediff       = pair_ediff_max;
  vtp->v7.hpsPairTrigger[inst].Cluster_Eminmax  = (cluster_emax<<16)  | (cluster_emin<<0);
  vtp->v7.hpsPairTrigger[inst].Cluster_Nmin     = cluster_nmin;
  vtp->v7.hpsPairTrigger[inst].Pair_CoplanarTol = pair_coplanarity_tol;
  vtp->v7.hpsPairTrigger[inst].Pair_ED          = (pair_ed_thr<<16) | (f<<0);
  VUNLOCK;

  return OK;
}

int
vtpGetHPS_PairTrigger(
    int inst, int *cluster_emin, int *cluster_emax, int *cluster_nmin,
    int *pair_dt, int *pair_esum_min, int *pair_esum_max, int *pair_ediff_max,
    float *pair_ed_factor, int *pair_ed_thr, int *pair_coplanarity_tol,
    int *enable_flags
  )
{
  int f;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  CHECKRANGE_INT(inst         ,   0,    3);

  VLOCK;
  *enable_flags         = vtp->v7.hpsPairTrigger[inst].Ctrl & 0x8000FFFF;
  *pair_dt              = (vtp->v7.hpsPairTrigger[inst].Ctrl>>16) & 0xF;
  *pair_esum_min        = (vtp->v7.hpsPairTrigger[inst].Pair_Esum>>0)        & 0x3FFF;
  *pair_esum_max        = (vtp->v7.hpsPairTrigger[inst].Pair_Esum>>16)       & 0x3FFF;
  *pair_ediff_max       = vtp->v7.hpsPairTrigger[inst].Pair_Ediff;
  *cluster_emax         = (vtp->v7.hpsPairTrigger[inst].Cluster_Eminmax>>16) & 0x1FFF;
  *cluster_emin         = (vtp->v7.hpsPairTrigger[inst].Cluster_Eminmax>>0)  & 0x1FFF;
  *cluster_nmin         = vtp->v7.hpsPairTrigger[inst].Cluster_Nmin;
  *pair_coplanarity_tol = vtp->v7.hpsPairTrigger[inst].Pair_CoplanarTol;
  *pair_ed_thr          = (vtp->v7.hpsPairTrigger[inst].Pair_ED>>16)         & 0x1FFF;
  f                     = (vtp->v7.hpsPairTrigger[inst].Pair_ED>>0)          & 0xFF;
  VUNLOCK;

  *pair_ed_factor = f / 16.0;
  *pair_dt*= 4;

  return OK;
}

int
vtpSetHPS_MultiplicityTrigger(
    int inst,
    int cluster_emin, int cluster_emax, int cluster_nmin,
    int mult_dt, int mult_top_min, int mult_bot_min, int mult_tot_min,
    int enable_flags
  )
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  mult_dt/= 4;

  CHECKRANGE_INT(inst         ,   0,    1);
  CHECKRANGE_INT(cluster_emin ,   0, 8191);
  CHECKRANGE_INT(cluster_emax ,   0, 8191);
  CHECKRANGE_INT(cluster_nmin ,   0,    9);

  CHECKRANGE_INT(mult_dt      ,   0,   15);
  CHECKRANGE_INT(mult_top_min ,   0,   15);
  CHECKRANGE_INT(mult_bot_min ,   0,   15);
  CHECKRANGE_INT(mult_tot_min ,   0,   15);

  VLOCK;
  vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Emin  = cluster_emin;
  vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Emax  = cluster_emax;
  vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Nmin  = cluster_nmin;
  vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Mult  = enable_flags |
                                                       (mult_dt<<12) |
                                                       (mult_tot_min<<8) |
                                                       (mult_bot_min<<4) |
                                                       (mult_top_min<<0);
  VUNLOCK;

  return OK;
}

int
vtpGetHPS_MultiplicityTrigger(
    int inst,
    int *cluster_emin, int *cluster_emax, int *cluster_nmin,
    int *mult_dt, int *mult_top_min, int *mult_bot_min, int *mult_tot_min,
    int *enable_flags
  )
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  CHECKRANGE_INT(inst         ,   0,    1);

  VLOCK;
  *cluster_emin  = vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Emin;
  *cluster_emax  = vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Emax;
  *cluster_nmin  = vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Nmin;
  *enable_flags  = vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Mult & 0x80000000;
  *mult_dt       = (vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Mult>>12) & 0xF;
  *mult_tot_min  = (vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Mult>>8) & 0xF;
  *mult_bot_min  = (vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Mult>>4) & 0xF;
  *mult_top_min  = (vtp->v7.hpsMultiplicityTrigger[inst].Cluster_Mult>>0) & 0xF;
  VUNLOCK;

  *mult_dt = (*mult_dt) *4;
  return OK;
}

int
vtpSetHPS_CalibrationTrigger(
    int enable_flags, int cosmic_dt, float pulser_freq
  )
{
  float period;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  cosmic_dt/= 4;

  CHECKRANGE_INT(cosmic_dt,       0, 255);
  CHECKRANGE_FLOAT(pulser_freq, 0.0, 125.0E6);

  if(pulser_freq > 0)
    period = 250.0E6 / pulser_freq;
  else
    period = 0.0;

  VLOCK;
  vtp->v7.hpsCalibTrigger.Ctrl   = enable_flags | (cosmic_dt<<0);
  vtp->v7.hpsCalibTrigger.Pulser = (int)period;
  VUNLOCK;

  return OK;
}

int
vtpGetHPS_CalibrationTrigger(
    int *enable_flags, int *cosmic_dt, float *pulser_freq
  )
{
  int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  VLOCK;
  val = vtp->v7.hpsCalibTrigger.Ctrl;
  *enable_flags  = val & 0xFFFFFF00;
  *cosmic_dt     = val & 0x000000FF;

  val            = vtp->v7.hpsCalibTrigger.Pulser;
  if(val)
    *pulser_freq   = 250.0E6 / val;
  else
    *pulser_freq   = 0.0;

  VUNLOCK;

  *cosmic_dt*= 4;

  return OK;
}

int vtpSetHPS_FeeTrigger(
    int cluster_emin, int cluster_emax, int cluster_nmin,
    int *prescale_xmin, int *prescale_xmax, int *prescale, int enable_flags
  )
{
  int i;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  CHECKRANGE_INT(cluster_emin,  0, 8191);
  CHECKRANGE_INT(cluster_emax,  0, 8191);
  CHECKRANGE_INT(cluster_nmin,  0,    9);
  for(i=0;i<7;i++)
  {
    CHECKRANGE_INT(prescale_xmin[i], -31, 31);
    CHECKRANGE_INT(prescale_xmax[i], -31, 31);
    CHECKRANGE_INT(prescale[i],       0, 65535);
  }

  VLOCK;
  // Top
  vtp->v7.hpsFeeTriggerTop.Ctrl         = enable_flags;
  vtp->v7.hpsFeeTriggerTop.Cluster_Emin = cluster_emin;
  vtp->v7.hpsFeeTriggerTop.Cluster_Emax = cluster_emax;
  vtp->v7.hpsFeeTriggerTop.Cluster_Nmin = cluster_nmin;

  vtp->v7.hpsFeeTriggerTop.Prescale_Xmin[0] =
    ((prescale_xmin[0] & 0x3f)<<0)  | ((prescale_xmin[1] & 0x3f)<<6) |
    ((prescale_xmin[2] & 0x3f)<<12) | ((prescale_xmin[3] & 0x3f)<<18) |
    ((prescale_xmin[4] & 0x3f)<<24);

  vtp->v7.hpsFeeTriggerTop.Prescale_Xmin[1] =
    ((prescale_xmin[5] & 0x3f)<<0)  | ((prescale_xmin[6] & 0x3f)<<6);

  vtp->v7.hpsFeeTriggerTop.Prescale_Xmax[0] =
    ((prescale_xmax[0] & 0x3f)<<0)  | ((prescale_xmax[1] & 0x3f)<<6) |
    ((prescale_xmax[2] & 0x3f)<<12) | ((prescale_xmax[3] & 0x3f)<<18) |
    ((prescale_xmax[4] & 0x3f)<<24);

  vtp->v7.hpsFeeTriggerTop.Prescale_Xmax[1] =
    ((prescale_xmax[5] & 0x3f)<<0)  | ((prescale_xmax[6] & 0x3f)<<6);

  vtp->v7.hpsFeeTriggerTop.Prescale[0] = prescale[0] | (prescale[1]<<16);
  vtp->v7.hpsFeeTriggerTop.Prescale[1] = prescale[2] | (prescale[3]<<16);
  vtp->v7.hpsFeeTriggerTop.Prescale[2] = prescale[4] | (prescale[5]<<16);
  vtp->v7.hpsFeeTriggerTop.Prescale[3] = prescale[6];

  // Bottom
  vtp->v7.hpsFeeTriggerBot.Ctrl         = enable_flags;
  vtp->v7.hpsFeeTriggerBot.Cluster_Emin = cluster_emin;
  vtp->v7.hpsFeeTriggerBot.Cluster_Emax = cluster_emax;
  vtp->v7.hpsFeeTriggerBot.Cluster_Nmin = cluster_nmin;

  vtp->v7.hpsFeeTriggerBot.Prescale_Xmin[0] =
    ((prescale_xmin[0] & 0x3f)<<0)  | ((prescale_xmin[1] & 0x3f)<<6) |
    ((prescale_xmin[2] & 0x3f)<<12) | ((prescale_xmin[3] & 0x3f)<<18) |
    ((prescale_xmin[4] & 0x3f)<<24);

  vtp->v7.hpsFeeTriggerBot.Prescale_Xmin[1] =
    ((prescale_xmin[5] & 0x3f)<<0)  | ((prescale_xmin[6] & 0x3f)<<6);

  vtp->v7.hpsFeeTriggerBot.Prescale_Xmax[0] =
    ((prescale_xmax[0] & 0x3f)<<0)  | ((prescale_xmax[1] & 0x3f)<<6) |
    ((prescale_xmax[2] & 0x3f)<<12) | ((prescale_xmax[3] & 0x3f)<<18) |
    ((prescale_xmax[4] & 0x3f)<<24);

  vtp->v7.hpsFeeTriggerBot.Prescale_Xmax[1] =
    ((prescale_xmax[5] & 0x3f)<<0)  | ((prescale_xmax[6] & 0x3f)<<6);

  vtp->v7.hpsFeeTriggerBot.Prescale[0] = prescale[0] | (prescale[1]<<16);
  vtp->v7.hpsFeeTriggerBot.Prescale[1] = prescale[2] | (prescale[3]<<16);
  vtp->v7.hpsFeeTriggerBot.Prescale[2] = prescale[4] | (prescale[5]<<16);
  vtp->v7.hpsFeeTriggerBot.Prescale[3] = prescale[6];
  VUNLOCK;

  return OK;
}

int vtpGetHPS_FeeTrigger(
    int *cluster_emin, int *cluster_emax, int *cluster_nmin,
    int *prescale_xmin, int *prescale_xmax, int *prescale, int *enable_flags
  )
{
  int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  VLOCK;
  *enable_flags = vtp->v7.hpsFeeTriggerTop.Ctrl;
  *cluster_emin = vtp->v7.hpsFeeTriggerTop.Cluster_Emin;
  *cluster_emax = vtp->v7.hpsFeeTriggerTop.Cluster_Emax;
  *cluster_nmin = vtp->v7.hpsFeeTriggerTop.Cluster_Nmin;

  val = vtp->v7.hpsFeeTriggerTop.Prescale_Xmin[0];
  prescale_xmin[0] = ((val>> 0) & 0x3f) | (((val>> 0) & 0x20) ? 0xffffffc0 : 0);
  prescale_xmin[1] = ((val>> 6) & 0x3f) | (((val>> 6) & 0x20) ? 0xffffffc0 : 0);
  prescale_xmin[2] = ((val>>12) & 0x3f) | (((val>>12) & 0x20) ? 0xffffffc0 : 0);
  prescale_xmin[3] = ((val>>18) & 0x3f) | (((val>>18) & 0x20) ? 0xffffffc0 : 0);
  prescale_xmin[4] = ((val>>24) & 0x3f) | (((val>>24) & 0x20) ? 0xffffffc0 : 0);

  val = vtp->v7.hpsFeeTriggerTop.Prescale_Xmin[1];
  prescale_xmin[5] = ((val>> 0) & 0x3f) | (((val>> 0) & 0x20) ? 0xffffffc0 : 0);
  prescale_xmin[6] = ((val>> 6) & 0x3f) | (((val>> 6) & 0x20) ? 0xffffffc0 : 0);

  val = vtp->v7.hpsFeeTriggerTop.Prescale_Xmax[0];
  prescale_xmax[0] = ((val>> 0) & 0x3f) | (((val>> 0) & 0x20) ? 0xffffffc0 : 0);
  prescale_xmax[1] = ((val>> 6) & 0x3f) | (((val>> 6) & 0x20) ? 0xffffffc0 : 0);
  prescale_xmax[2] = ((val>>12) & 0x3f) | (((val>>12) & 0x20) ? 0xffffffc0 : 0);
  prescale_xmax[3] = ((val>>18) & 0x3f) | (((val>>18) & 0x20) ? 0xffffffc0 : 0);
  prescale_xmax[4] = ((val>>24) & 0x3f) | (((val>>24) & 0x20) ? 0xffffffc0 : 0);

  val = vtp->v7.hpsFeeTriggerTop.Prescale_Xmax[1];
  prescale_xmax[5] = ((val>> 0) & 0x3f) | (((val>> 0) & 0x20) ? 0xffffffc0 : 0);
  prescale_xmax[6] = ((val>> 6) & 0x3f) | (((val>> 6) & 0x20) ? 0xffffffc0 : 0);

  val = vtp->v7.hpsFeeTriggerTop.Prescale[0];
  prescale[0] = (val>> 0) & 0xffff;
  prescale[1] = (val>>16) & 0xffff;

  val = vtp->v7.hpsFeeTriggerTop.Prescale[1];
  prescale[2] = (val>> 0) & 0xffff;
  prescale[3] = (val>>16) & 0xffff;

  val = vtp->v7.hpsFeeTriggerTop.Prescale[2];
  prescale[4] = (val>> 0) & 0xffff;
  prescale[5] = (val>>16) & 0xffff;

  val = vtp->v7.hpsFeeTriggerTop.Prescale[3];
  prescale[6] = (val>> 0) & 0xffff;

  VUNLOCK;

  return OK;
}

int
vtpSetHPS_TriggerLatency(
    int latency
  )
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  latency/= 4;

  CHECKRANGE_INT(latency,   0, 1023);

  VLOCK;
  vtp->v7.hpsTriggerBits.Latency         = latency;
  vtp->v7.hpsMultiplicityTrigger[0].Latency = latency-25;
  vtp->v7.hpsMultiplicityTrigger[1].Latency = latency-25;
  VUNLOCK;

  return OK;
}

int
vtpGetHPS_TriggerLatency(
    int *latency
  )
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  VLOCK;
  *latency = vtp->v7.hpsTriggerBits.Latency;
  VUNLOCK;

  *latency*=4;

  return OK;
}

int
vtpSetHPS_TriggerPrescale(
    int inst, int prescale
  )
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  CHECKRANGE_INT(inst,   0, 31);

  VLOCK;
  vtp->v7.hpsTriggerBits.Prescale[inst] = prescale;
  VUNLOCK;

  return OK;
}

int
vtpGetHPS_TriggerPrescale(
    int inst, int *prescale
  )
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  CHECKRANGE_INT(inst,   0, 31);

  VLOCK;
  *prescale = vtp->v7.hpsTriggerBits.Prescale[inst];
  VUNLOCK;

  return OK;
}

int
vtpHPSPrintConfig()
{
  int i, j;
  int top_nbottom, hit_dt, seed_thr;
  int hit_width, fadchit_thr, hodo_thr;
  int cluster_emin, cluster_emax, cluster_nmin;
  int cluster_xmin, enable_flags;
  int pair_dt, pair_esum_min, pair_esum_max, pair_ediff_max, pair_ed_thr, pair_coplanarity_tol;
  int mult_dt, mult_top_min, mult_bot_min, mult_tot_min, latency, prescale[32], cosmic_dt;
  float cluster_pde_c[4], pair_ed_factor, pulser_freq;
  int prescale_xmin[7], prescale_xmax[7];

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  vtpGetHPS_Cluster(&top_nbottom, &hit_dt, &seed_thr);
  vtpGetHPS_Hodoscope(&hit_width, &fadchit_thr, &hodo_thr);

  printf("%s:\n", __func__);
  printf("Calorimeter:\n");
  printf("  Top/Bottom: %s\n", top_nbottom ? "Top" : "Bottom");
  printf("  Hit dt: +/-%dns\n", hit_dt);
  printf("  Seed Threshold: %dMeV\n", seed_thr);
  printf("\n");
  printf("Hodoscope:\n");
  printf("  Hit width: %dns\n", hit_width);
  printf("  FADC Hit Threshold: %d\n", fadchit_thr);
  printf("  Hodo Hit Threshold: %d\n", hodo_thr);
  printf("\n");

  for(i=0;i<4;i++)
  {
    for(j=0;j<2;j++)
    {
      vtpGetHPS_SingleTrigger(i,j,&cluster_emin, &cluster_emax, &cluster_nmin, &cluster_xmin, &cluster_pde_c[0], &enable_flags);
      printf("Singles Trigger %d %s:\n", i, j ? "Top" : "Bottom");
      printf("  Cluster emin (req=%d):            %dMeV\n",       (enable_flags & 0x00000001)?1:0, cluster_emin);
      printf("  Cluster emax (req=%d):            %dMeV\n",       (enable_flags & 0x00000002)?1:0, cluster_emax);
      printf("  Cluster nmin (req=%d):            %d\n",          (enable_flags & 0x00000004)?1:0, cluster_nmin);
      printf("  Cluster xmin (req=%d):            %d\n",          (enable_flags & 0x00000008)?1:0, cluster_xmin);
      printf("  Cluster pde_c (req=%d):           %f %f %f %f\n", (enable_flags & 0x00000010)?1:0, cluster_pde_c[0], cluster_pde_c[1], cluster_pde_c[2], cluster_pde_c[3]);
      printf("  Hodo l1 hit (req=%d)\n",                          (enable_flags & 0x00000020)?1:0);
      printf("  Hodo l2 hit (req=%d)\n",                          (enable_flags & 0x00000040)?1:0);
      printf("  Hodo l1<->l2 (req=%d)\n",                         (enable_flags & 0x00000080)?1:0);
      printf("  Hodo l1<->ClusterX<->l2 (req=%d)\n",              (enable_flags & 0x00000100)?1:0);
      printf("  Enabled:                      %d\n",              (enable_flags & 0x80000000)?1:0);
      printf("\n");
    }
  }

  for(i=0;i<4;i++)
  {
    vtpGetHPS_PairTrigger(i,&cluster_emin, &cluster_emax, &cluster_nmin, &pair_dt, &pair_esum_min, &pair_esum_max, &pair_ediff_max, &pair_ed_factor, &pair_ed_thr, &pair_coplanarity_tol, &enable_flags);
    printf("Pair Trigger %d:\n", i);
    printf("  Cluster emin:                  %dMeV\n",                  cluster_emin);
    printf("  Cluster emax:                  %dMeV\n",                  cluster_emax);
    printf("  Cluster nmin:                  %d\n",                     cluster_nmin);
    printf("  Pair dt:                       +/-%dns\n",                pair_dt);
    printf("  Pair esum min (req=%d):        %dMeV\n",                  (enable_flags & 0x00000001)?1:0, pair_esum_min);
    printf("  Pair esum max (req=%d):        %dMeV\n",                  (enable_flags & 0x00000001)?1:0, pair_esum_max);
    printf("  Pair ediff max (req=%d):       %dMeV\n",                  (enable_flags & 0x00000002)?1:0, pair_ediff_max);
    printf("  Pair coplanarity tol (req=%d): +/-%ddegrees\n",           (enable_flags & 0x00000004)?1:0, pair_coplanarity_tol);
    printf("  Pair energy,dist (req=%d):     %dMeV <= dist*%fMeV/mm\n", (enable_flags & 0x00000008)?1:0, pair_ed_thr, pair_ed_factor);
    printf("  Hodo l1 hit (req=%d)\n",                                 (enable_flags & 0x00000010)?1:0);
    printf("  Hodo l2 hit (req=%d)\n",                                 (enable_flags & 0x00000020)?1:0);
    printf("  Hodo l1<->l2 (req=%d)\n",                                (enable_flags & 0x00000040)?1:0);
    printf("  Hodo l1<->ClusterX<->l2 (req=%d)\n",                     (enable_flags & 0x00000080)?1:0);
    printf("  Enabled:                       %d\n",                    (enable_flags & 0x80000000)?1:0);
    printf("\n");
  }

  for(i=0;i<2;i++)
  {
    vtpGetHPS_MultiplicityTrigger(i, &cluster_emin, &cluster_emax, &cluster_nmin, &mult_dt, &mult_top_min, &mult_bot_min, &mult_tot_min, &enable_flags);
    printf("Cluster Multiplicitiy Trigger %d:\n", i);
    printf("  Cluster emin:           %dMeV\n", cluster_emin);
    printf("  Cluster emax:           %dMeV\n", cluster_emax);
    printf("  Cluster nmin:           %d\n",    cluster_nmin);
    printf("  Multiplicity window:    %dns\n",  mult_dt);
    printf("  Multiplicity Top Min:   %d\n",    mult_top_min);
    printf("  Multiplicity Bot Min:   %d\n",    mult_bot_min);
    printf("  Multiplicity Total Min: %d\n",    mult_tot_min);
    printf("  Enabled:                %d\n",    (enable_flags & 0x80000000)?1:0);
    printf("\n");
  }

  vtpGetHPS_FeeTrigger(&cluster_emin, &cluster_emax, &cluster_nmin, prescale_xmin, prescale_xmax, prescale, &enable_flags);
  printf("FEE Cluster Trigger Top:\n");
  printf("  Cluster emin:           %dMeV\n", cluster_emin);
  printf("  Cluster emax:           %dMeV\n", cluster_emax);
  printf("  Cluster nmin:           %d\n",    cluster_nmin);
  printf("  Region, Xmin, Xmax, Prescale:\n");
  for(i=0;i<7;i++)
    printf("    %d, %d, %d, %d\n", i, prescale_xmin[i], prescale_xmax[i], prescale[i]);
  printf("  Enabled:                %d\n",    (enable_flags & 0x80000000)?1:0);
  printf("\n");

  vtpGetHPS_CalibrationTrigger(&enable_flags, &cosmic_dt, &pulser_freq);
  char *cosmic_mode;
  if((enable_flags & 0x100) && (enable_flags & 0x200)) cosmic_mode = "Top && Bottom";
  else if(enable_flags & 0x100)                        cosmic_mode = "Top only";
  else if(enable_flags & 0x200)                        cosmic_mode = "Bottom only";
  else                                                 cosmic_mode = "Disabled";

  char *hodoscope_mode;
  if((enable_flags & 0x10000) && (enable_flags & 0x20000)) hodoscope_mode = "Top || Bottom";
  else if(enable_flags & 0x10000)                          hodoscope_mode = "Top only";
  else if(enable_flags & 0x20000)                          hodoscope_mode = "Bottom only";
  else                                                     hodoscope_mode = "Disabled";

  printf("Calibration Trigger:\n");
  printf("  Cosmic dt:       %dns\n", cosmic_dt);
  printf("  Cosmic mode:     %s\n", cosmic_mode);
  printf("  Hodoscope mode:  %s\n", hodoscope_mode);
  printf("  LED mode:        enabled\n");
  printf("  Pulser frequency: %fHz\n", (enable_flags & 0x80000000)?pulser_freq:0.0);
  printf("\n");

  vtpGetHPS_TriggerLatency(&latency);
  for(i=0;i<32;i++) vtpGetHPS_TriggerPrescale(i, &prescale[i]);
  printf("HPS Trigger:\n");
  printf("  Latency: %dns\n", latency);
  printf("  Prescalers:\n") ;
  printf("    Single 0 Top:   %d\n", prescale[0]);
  printf("    Single 1 Top:   %d\n", prescale[1]);
  printf("    Single 2 Top:   %d\n", prescale[2]);
  printf("    Single 3 Top:   %d\n", prescale[3]);
  printf("    Single 0 Bot:   %d\n", prescale[4]);
  printf("    Single 1 Bot:   %d\n", prescale[5]);
  printf("    Single 2 Bot:   %d\n", prescale[6]);
  printf("    Single 3 Bot:   %d\n", prescale[7]);
  printf("    Pair 0:         %d\n", prescale[8]);
  printf("    Pair 1:         %d\n", prescale[9]);
  printf("    Pair 2:         %d\n", prescale[10]);
  printf("    Pair 3:         %d\n", prescale[11]);
  printf("    LED:            %d\n", prescale[12]);
  printf("    Cosmic:         %d\n", prescale[13]);
  printf("    Hodoscope:      %d\n", prescale[14]);
  printf("    Pulser:         %d\n", prescale[15]);
  printf("    Multiplicity 0: %d\n", prescale[16]);
  printf("    Multiplicity 1: %d\n", prescale[17]);
  printf("    FEE Top:        %d\n", prescale[18]);
  printf("    FEE Bottom:     %d\n", prescale[19]);

  return OK;
}

int
vtpHPSPrintScalers()
{
  double ref, rate;
  int i;
  const char *scalers_name[] = {
    "BusClk",
    "Sync",
    "Trig1",
    "Trig2",
    "S0-T RawIn",
    "S0-T Accept",
    "S0-T EminMaxNminPass",
    "S0-T +XminPass",
    "S0-T +PDEPass",
    "S0-T +HodoL1Pass",
    "S0-T +HodoL2Pass",
    "S0-T +HodoL1L2Pass",
    "S0-T +HodoL1<->L2Pass",
    "S0-T +HodoL1<->X<->L2Pass",
    "S1-T RawIn",
    "S1-T Accept",
    "S1-T EminMaxNminPass",
    "S1-T +XminPass",
    "S1-T +PDEPass",
    "S1-T +HodoL1Pass",
    "S1-T +HodoL2Pass",
    "S1-T +HodoL1L2Pass",
    "S1-T +HodoL1<->L2Pass",
    "S1-T +HodoL1<->X<->L2Pass",
    "S2-T RawIn",
    "S2-T Accept",
    "S2-T EminMaxNminPass",
    "S2-T +XminPass",
    "S2-T +PDEPass",
    "S2-T +HodoL1Pass",
    "S2-T +HodoL2Pass",
    "S2-T +HodoL1L2Pass",
    "S2-T +HodoL1<->L2Pass",
    "S2-T +HodoL1<->X<->L2Pass",
    "S3-T RawIn",
    "S3-T Accept",
    "S3-T EminMaxNminPass",
    "S3-T +XminPass",
    "S3-T +PDEPass",
    "S3-T +HodoL1Pass",
    "S3-T +HodoL2Pass",
    "S3-T +HodoL1L2Pass",
    "S3-T +HodoL1<->L2Pass",
    "S3-T +HodoL1<->X<->L2Pass",
    "S0-B RawIn",
    "S0-B Accept",
    "S0-B EminMaxNminPass",
    "S0-B +XminPass",
    "S0-B +PDEPass",
    "S0-B +HodoL1Pass",
    "S0-B +HodoL2Pass",
    "S0-B +HodoL1L2Pass",
    "S0-B +HodoL1<->L2Pass",
    "S0-B +HodoL1<->X<->L2Pass",
    "S1-B RawIn",
    "S1-B Accept",
    "S1-B EminMaxNminPass",
    "S1-B +XminPass",
    "S1-B +PDEPass",
    "S1-B +HodoL1Pass",
    "S1-B +HodoL2Pass",
    "S1-B +HodoL1L2Pass",
    "S1-B +HodoL1<->L2Pass",
    "S1-B +HodoL1<->X<->L2Pass",
    "S2-B RawIn",
    "S2-B Accept",
    "S2-B EminMaxNminPass",
    "S2-B +XminPass",
    "S2-B +PDEPass",
    "S2-B +HodoL1Pass",
    "S2-B +HodoL2Pass",
    "S2-B +HodoL1L2Pass",
    "S2-B +HodoL1<->L2Pass",
    "S2-B +HodoL1<->X<->L2Pass",
    "S3-B RawIn",
    "S3-B Accept",
    "S3-B EminMaxNminPass",
    "S3-B +XminPass",
    "S3-B +PDEPass",
    "S3-B +HodoL1Pass",
    "S3-B +HodoL2Pass",
    "S3-B +HodoL1L2Pass",
    "S3-B +HodoL1<->L2Pass",
    "S3-B +HodoL1<->X<->L2Pass",
    "P0 RawIn",
    "P0 Accept",
    "P0 SumPass",
    "P0 DiffPass",
    "P0 EnergyDistPass",
    "P0 CoplanarityPass",
    "P1 RawIn",
    "P1 Accept",
    "P1 SumPass",
    "P1 DiffPass",
    "P1 EnergyDistPass",
    "P1 CoplanarityPass",
    "P2 RawIn",
    "P2 Accept",
    "P2 SumPass",
    "P2 DiffPass",
    "P2 EnergyDistPass",
    "P2 CoplanarityPass",
    "P3 RawIn",
    "P3 Accept",
    "P3 SumPass",
    "P3 DiffPass",
    "P3 EnergyDistPass",
    "P3 CoplanarityPass",
    "Cal CosmicTop",
    "Cal CosmicBot",
    "Cal CosmicTop&Bot",
    "Cal LED",
    "Cal HodoTop",
    "Cal HodoBot",
    "Cal HodoTop|Bot",
    "Cal Pulser",
    "Mult Accept"
    };
  unsigned int scalers[sizeof(scalers_name)/sizeof(scalers_name[0])], *pscalers = &scalers[0];

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HPS,0);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  *pscalers++ = vtp->v7.sd.Scaler_BusClk;
  *pscalers++ = vtp->v7.sd.Scaler_Sync;
  *pscalers++ = vtp->v7.sd.Scaler_Trig1;
  *pscalers++ = vtp->v7.sd.Scaler_Trig2;

  // Single top triggers
  for(i=0; i<4; i++)
  {
    *pscalers++ = vtp->v7.hpsSingleTriggerTop[i].ScalerTotal;
    *pscalers++ = vtp->v7.hpsSingleTriggerTop[i].ScalerAccept;
    *pscalers++ = vtp->v7.hpsSingleTriggerTop[i].ScalerCuts[0];
    *pscalers++ = vtp->v7.hpsSingleTriggerTop[i].ScalerCuts[1];
    *pscalers++ = vtp->v7.hpsSingleTriggerTop[i].ScalerCuts[2];
    *pscalers++ = vtp->v7.hpsSingleTriggerTop[i].ScalerCuts[3];
    *pscalers++ = vtp->v7.hpsSingleTriggerTop[i].ScalerCuts[4];
    *pscalers++ = vtp->v7.hpsSingleTriggerTop[i].ScalerCuts[5];
    *pscalers++ = vtp->v7.hpsSingleTriggerTop[i].ScalerCuts[6];
    *pscalers++ = vtp->v7.hpsSingleTriggerTop[i].ScalerCuts[7];
  }

  // Single bottom triggers
  for(i=0; i<4; i++)
  {
    *pscalers++ = vtp->v7.hpsSingleTriggerBot[i].ScalerTotal;
    *pscalers++ = vtp->v7.hpsSingleTriggerBot[i].ScalerAccept;
    *pscalers++ = vtp->v7.hpsSingleTriggerBot[i].ScalerCuts[0];
    *pscalers++ = vtp->v7.hpsSingleTriggerBot[i].ScalerCuts[1];
    *pscalers++ = vtp->v7.hpsSingleTriggerBot[i].ScalerCuts[2];
    *pscalers++ = vtp->v7.hpsSingleTriggerBot[i].ScalerCuts[3];
    *pscalers++ = vtp->v7.hpsSingleTriggerBot[i].ScalerCuts[4];
    *pscalers++ = vtp->v7.hpsSingleTriggerBot[i].ScalerCuts[5];
    *pscalers++ = vtp->v7.hpsSingleTriggerBot[i].ScalerCuts[6];
    *pscalers++ = vtp->v7.hpsSingleTriggerBot[i].ScalerCuts[7];
  }

  // Pair triggers
  for(i=0; i<4; i++)
  {
    *pscalers++ = vtp->v7.hpsPairTrigger[i].ScalerTotal;
    *pscalers++ = vtp->v7.hpsPairTrigger[i].ScalerAccept;
    *pscalers++ = vtp->v7.hpsPairTrigger[i].ScalerCuts[0];
    *pscalers++ = vtp->v7.hpsPairTrigger[i].ScalerCuts[1];
    *pscalers++ = vtp->v7.hpsPairTrigger[i].ScalerCuts[2];
    *pscalers++ = vtp->v7.hpsPairTrigger[i].ScalerCuts[3];
  }

  // Calibration triggers
  *pscalers++ = vtp->v7.hpsCalibTrigger.ScalerCosmicTop;
  *pscalers++ = vtp->v7.hpsCalibTrigger.ScalerCosmicBot;
  *pscalers++ = vtp->v7.hpsCalibTrigger.ScalerCosmicTopBot;
  *pscalers++ = vtp->v7.hpsCalibTrigger.ScalerLED;
  *pscalers++ = vtp->v7.hpsCalibTrigger.ScalerHodoscopeTop;
  *pscalers++ = vtp->v7.hpsCalibTrigger.ScalerHodoscopeBot;
  *pscalers++ = vtp->v7.hpsCalibTrigger.ScalerHodoscopeTopBot;
  *pscalers++ = vtp->v7.hpsCalibTrigger.ScalerPulser;

  // Multiplicity trigger
  for(i=0; i<2; i++)
  {
    *pscalers++ = vtp->v7.hpsMultiplicityTrigger[i].ScalerAccept;
  }

  // FEE trigger
  *pscalers++ = vtp->v7.hpsFeeTriggerTop.ScalerAccept;
  *pscalers++ = vtp->v7.hpsFeeTriggerBot.ScalerAccept;

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;

  printf("%s - \n", __FUNCTION__);
  if(!scalers[0])
  {
    printf("Error: %s reference time is 0. Reported rates will not be normalized.\n", __func__);
    ref = 1.0;
  }
  else
  {
    ref = (double)scalers[0] / (double)33330000;
  }

  for(i = 0; i < sizeof(scalers)/sizeof(scalers[0]); i++)
  {
    rate = (double)scalers[i];
    rate = rate / ref;
    if(scalers[i] == 0xFFFFFFFF)
     printf("   %-25s %10u,%.3fHz [OVERFLOW]\n", scalers_name[i], scalers[i], rate);
    else
     printf("   %-25s %10u,%.3fHz\n", scalers_name[i], scalers[i], rate);
  }
  return OK;
}

#ifdef IPC
int
vtpHPSSendErrors(char *host)
{
/*
  char name[100];
  int data[NSERDES];
  unsigned int val;
  int i;
  CHECKINIT;

  for(i=0;i<4;i++)
  {
    vtpSerdesStatus(VTP_SERDES_QSFP, i, 0, data);

    if(data[4] & 0x1) // check channel up bit
      val |= (1<<i);
    else
      val &= ~(1<<i);
  }

  if(!(val & 0x1))  // Fiber 0 = trig2 SSP_SLOT9
  {
    sprintf(name, "err: crate=%s link to trig2 SSP_SLOT9 down", host);
    epics_json_msg_send(name, "int", 1, &data);
  }
  if(!(val & 0x2))  // Fiber 1 = adcftXvtp
  {
    sprintf(name, "err: crate=%s,adcft1vtp link to adcft2vtp down", host);
    epics_json_msg_send(name, "int", 1, &data);
  }
  if(!(val & 0x4) || !(val & 0x8))  // Fiber 2,3 = adcft3vtp
  {
    sprintf(name, "err: crate=%s,link to adcft3vtp down", host);
    epics_json_msg_send(name, "int", 1, &data);
  }
*/
  return OK;
}

int
vtpHPSSendScalers(char *host)
{
  char name[100];
  float ref, data[1024];
  unsigned int val, idata[32];
  int i;
  CHECKINIT;

  printf("%s...", __func__);
  if(!strcmp(host,"hps2vtp"))
    return OK;

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;
  //Read/normalize reference
  val = vtp->v7.sd.Scaler_BusClk;
  if(!val) val = 1;
  ref = 33330000.0f / (float)val;

  for(i=0;i<32;i++)
  {
    if(i < 4)      data[i] = ref*vtp->v7.hpsSingleTriggerTop[i].ScalerAccept;
    else if(i<8)   data[i] = ref*vtp->v7.hpsSingleTriggerBot[i-4].ScalerAccept;
    else if(i<12)  data[i] = ref*vtp->v7.hpsPairTrigger[i-8].ScalerAccept;
    else if(i==12) data[i] = ref*vtp->v7.hpsCalibTrigger.ScalerLED;
    else if(i==13) data[i] = ref*vtp->v7.hpsCalibTrigger.ScalerCosmicTopBot;
    else if(i==14) data[i] = ref*vtp->v7.hpsCalibTrigger.ScalerHodoscopeTopBot;
    else if(i==15) data[i] = ref*vtp->v7.hpsCalibTrigger.ScalerPulser;
    else if(i==16) data[i] = ref*vtp->v7.hpsMultiplicityTrigger[0].ScalerAccept;
    else if(i==17) data[i] = ref*vtp->v7.hpsMultiplicityTrigger[1].ScalerAccept;
    else if(i==18) data[i] = ref*vtp->v7.hpsFeeTriggerTop.ScalerAccept;
    else if(i==19) data[i] = ref*vtp->v7.hpsFeeTriggerBot.ScalerAccept;
    else           data[i] = 0.0;

    idata[i] = vtp->v7.hpsTriggerBits.Prescale[i];
  }

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;

  sprintf(name, "%s_VTPHPS_TRIGGERBITS", host);
  epics_json_msg_send(name, "float", 32, data);

  sprintf(name, "%s_VTPHPS_PRESCALES", host);
  epics_json_msg_send(name, "int", 32, idata);

  return OK;
}

#endif

/* Hall A Compton functions */
int
vtpSetCompton_VetrocWidth(int vetroc_width)
{
  uint32_t reg;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMPTON,0);
  CHECKRANGE_INT(vetroc_width, 0, 0xFF);

  VLOCK;
  reg = vtp->v7.comptonTrigger.Ctrl[0];
  reg &= 0xFF00FFFF;
  reg |= (vetroc_width<<16);
  vtp->v7.comptonTrigger.Ctrl[0] = reg;
  VUNLOCK;

  return OK;
}

int
vtpGetCompton_VetrocWidth(int *vetroc_width)
{
  uint32_t reg = 0;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMPTON,0);

  VLOCK;
  reg = vtp->v7.comptonTrigger.Ctrl[0];
  *vetroc_width    = (reg & VTP_COMPTON_TRIGGER_CTRL_VETROC_PULSE_WIDTH_MASK) >> 16;
  VUNLOCK;

  return OK;
}

int
vtpSetCompton_EnableScalerReadout(int en)
{
  uint32_t reg;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMPTON,0);
  CHECKRANGE_INT(en, 0, 1);

  VLOCK;
  reg = vtp->v7.comptonTrigger.Ctrl[0];
  reg &= 0xFFFF7FFF;
  reg |= (en<<15);
  vtp->v7.comptonTrigger.Ctrl[0] = reg;
  VUNLOCK;

  return OK;
}

int
vtpGetCompton_EnableScalerReadout(int *en)
{
  uint32_t reg = 0;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMPTON,0);

  VLOCK;
  reg = vtp->v7.comptonTrigger.Ctrl[0];
  *en    = (reg & 0x8000) >> 15;
  VUNLOCK;

  return OK;
}

int
vtpSetCompton_Trigger(int inst, int fadc_threshold, int eplane_mult_min, int eplane_mask, int fadc_mask)
{
  int reg;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMPTON,0);
  CHECKRANGE_INT(inst, 0, 4);
  CHECKRANGE_INT(fadc_threshold, 0, 0x1FFF);
  CHECKRANGE_INT(eplane_mult_min, 0, 4);

printf("%s: inst=%d, fadc_thresold=%d, eplane_mult_min=%d, eplane_mask=%d, fadc_mask=%d\n", __func__,
    inst, fadc_threshold, eplane_mult_min, eplane_mask, fadc_mask);

  VLOCK;
  reg = vtp->v7.comptonTrigger.Ctrl[inst];
  reg &= 0xFF00FFFF;
  reg |= (fadc_threshold & VTP_COMPTON_TRIGGER_CTRL_FADC_THRESHOLD_MASK) |
         ((eplane_mult_min << 24) & VTP_COMPTON_TRIGGER_CTRL_EPLANE_MULT_MIN_MASK) |
		 ((eplane_mask << 28) & VTP_COMPTON_TRIGGER_CTRL_EPLANE_MASK);
  vtp->v7.comptonTrigger.Ctrl[inst] = reg;

  reg = vtp->v7.comptonTrigger.FadcMask[inst/2];
  if(inst & 0x1)
  {
    reg &= 0x0000FFFF;
	reg |= (fadc_mask<<16);
  }
  else
  {
    reg &= 0xFFFF0000;
	reg |= (fadc_mask<<0);
  }
  vtp->v7.comptonTrigger.FadcMask[inst/2] = reg;

  VUNLOCK;

  return OK;
}

int
vtpGetCompton_Trigger(int inst, int *fadc_threshold, int *eplane_mult_min, int *eplane_mask, int *fadc_mask)
{
  uint32_t reg = 0;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMPTON,0);
  CHECKRANGE_INT(inst, 0, 4);

  VLOCK;
  reg = vtp->v7.comptonTrigger.Ctrl[inst];
  *fadc_threshold  = (reg & VTP_COMPTON_TRIGGER_CTRL_FADC_THRESHOLD_MASK);
  *eplane_mult_min = (reg & VTP_COMPTON_TRIGGER_CTRL_EPLANE_MULT_MIN_MASK) >> 24;
  *eplane_mask     = (reg & VTP_COMPTON_TRIGGER_CTRL_EPLANE_MASK) >> 28;

  reg = vtp->v7.comptonTrigger.FadcMask[inst/2];
  if(inst & 0x1)
	*fadc_mask = (reg & 0xFFFF0000)>>16;
  else
	*fadc_mask = (reg & 0x0000FFFF)>>0;
  VUNLOCK;

printf("%s: inst=%d, fadc_thresold=%d, eplane_mult_min=%d, eplane_mask=%d, fadc_mask=0x%04X\n", __func__,
    inst, *fadc_threshold, *eplane_mult_min, *eplane_mask, *fadc_mask);

  return OK;
}

/* HTCC functions */

int
vtpSetHTCC_thresholds(int thr0, int thr1, int thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HTCC,0);

  VLOCK;
  vtp->v7.htccTrigger.Thresholds[0] = thr0;
  vtp->v7.htccTrigger.Thresholds[1] = thr1;
  vtp->v7.htccTrigger.Thresholds[2] = thr2;
  VUNLOCK;

  return OK;
}

int
vtpGetHTCC_thresholds(int *thr0, int *thr1, int *thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HTCC,0);

  VLOCK;
  *thr0 = vtp->v7.htccTrigger.Thresholds[0];
  *thr1 = vtp->v7.htccTrigger.Thresholds[1];
  *thr2 = vtp->v7.htccTrigger.Thresholds[2];
  VUNLOCK;

  return OK;
}

int
vtpSetHTCC_nframes(int nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HTCC,0);

  VLOCK;
  vtp->v7.htccTrigger.NFrames = nframes;
  VUNLOCK;

  return OK;
}

int
vtpGetHTCC_nframes(int *nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HTCC,0);

  VLOCK;
  *nframes = vtp->v7.htccTrigger.NFrames;
  VUNLOCK;

  return OK;
}

/* CTOF functions */
int
vtpSetCTOF_thresholds(int thr0, int thr1, int thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HTCC,0);

  VLOCK;
  vtp->v7.ctofTrigger.Thresholds[0] = thr0;
  vtp->v7.ctofTrigger.Thresholds[1] = thr1;
  vtp->v7.ctofTrigger.Thresholds[2] = thr2;
  VUNLOCK;

  return OK;
}

int
vtpGetCTOF_thresholds(int *thr0, int *thr1, int *thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HTCC,0);

  VLOCK;
  *thr0 = vtp->v7.ctofTrigger.Thresholds[0];
  *thr1 = vtp->v7.ctofTrigger.Thresholds[1];
  *thr2 = vtp->v7.ctofTrigger.Thresholds[2];
  VUNLOCK;

  return OK;
}

int
vtpSetCTOF_nframes(int nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HTCC,0);

  VLOCK;
  vtp->v7.ctofTrigger.NFrames = nframes;
  VUNLOCK;

  return OK;
}

int
vtpGetCTOF_nframes(int *nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HTCC,0);

  VLOCK;
  *nframes = vtp->v7.ctofTrigger.NFrames;
  VUNLOCK;

  return OK;
}

int
vtpHtccPrintScalers()
{
  double ref, rate;
  int i;
  unsigned int scalers[3];
  const char *scalers_name[3] = {
    "BusClk",
    "HTCCHit",
    "CTOFHit"
   };

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HTCC,0);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  scalers[0] = vtp->v7.sd.Scaler_BusClk;
  scalers[1] = vtp->v7.htccTrigger.ScalerHit;
  scalers[2] = vtp->v7.ctofTrigger.ScalerHit;

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;


  printf("%s - \n", __FUNCTION__);
  if(!scalers[0])
  {
    printf("Error: %s reference time is 0. Reported rates will not be normalized.\n", __func__);
    ref = 1.0;
  }
  else
  {
    ref = (double)scalers[0] / (double)33330000;
  }

  for(i=0; i<3; i++)
  {
    rate = (double)scalers[i];
    rate = rate / ref;
    if(scalers[i] == 0xFFFFFFFF)
     printf("   %-25s %10u,%.3fHz [OVERFLOW]\n", scalers_name[i], scalers[i], rate);
    else
     printf("   %-25s %10u,%.3fHz\n", scalers_name[i], scalers[i], rate);
  }
  return OK;
}

#ifdef IPC
int
vtpHtccSendErrors(char *host)
{
  char name[100];
  int data[NSERDES];
  unsigned int val;
  int i;
  CHECKINIT;

  for(i=0;i<4;i++)
  {
    vtpSerdesStatus(VTP_SERDES_QSFP, i, 0, data);

    if(data[4] & 0x1) // check channel up bit
      val |= (1<<i);
    else
      val &= ~(1<<i);
  }

  if(!(val & 0x1))  // Fiber 0 = trig2 SSP_SLOT10
  {
    sprintf(name, "err: crate=%s link to trig2 SSP_SLOT10 down", host);
    epics_json_msg_send(name, "int", 1, &data);
  }

  return OK;
}

int
vtpHtccSendScalers(char *host)
{
  char name[100];
  float ref, data[2];
  unsigned int val;
  CHECKINIT;

  printf("%s...", __func__);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  //Read/normalize reference
  val = vtp->v7.sd.Scaler_BusClk;
  if(!val) val = 1;
  ref = 33330000.0f / (float)val;

  data[0] = ref * (float)vtp->v7.htccTrigger.ScalerHit;
  data[1] = ref * (float)vtp->v7.ctofTrigger.ScalerHit;

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;

  sprintf(name, "%s_VTPHTCC_CLUSTERS", host);
  epics_json_msg_send(name, "float", 1, data);

  sprintf(name, "%s_VTPCTOF_CLUSTERS", host);
  epics_json_msg_send(name, "float", 1, data);

  return OK;
}
#endif







/* FTOF functions */

int
vtpSetFTOF_thresholds(int thr0, int thr1, int thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTOF,0);

  VLOCK;
  vtp->v7.ftofTrigger.Thresholds[0] = thr0;
  vtp->v7.ftofTrigger.Thresholds[1] = thr1;
  vtp->v7.ftofTrigger.Thresholds[2] = thr2;
  VUNLOCK;

  return OK;
}

int
vtpGetFTOF_thresholds(int *thr0, int *thr1, int *thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTOF,0);

  VLOCK;
  *thr0 = vtp->v7.ftofTrigger.Thresholds[0];
  *thr1 = vtp->v7.ftofTrigger.Thresholds[1];
  *thr2 = vtp->v7.ftofTrigger.Thresholds[2];
  VUNLOCK;

  return OK;
}

int
vtpSetFTOF_nframes(int nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTOF,0);

  VLOCK;
  vtp->v7.ftofTrigger.NFrames = nframes;
  VUNLOCK;

  return OK;
}

int
vtpGetFTOF_nframes(int *nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTOF,0);

  VLOCK;
  *nframes = vtp->v7.ftofTrigger.NFrames;
  VUNLOCK;

  return OK;
}

int
vtpFtofPrintScalers()
{
  double ref, rate;
  int i;
  unsigned int scalers[2];
  const char *scalers_name[2] = {
    "BusClk",
    "Hit"
   };

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_FTOF,0);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  scalers[0] = vtp->v7.sd.Scaler_BusClk;
  scalers[1] = vtp->v7.ftofTrigger.ScalerHit;

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;


  printf("%s - \n", __FUNCTION__);
  if(!scalers[0])
  {
    printf("Error: %s reference time is 0. Reported rates will not be normalized.\n", __func__);
    ref = 1.0;
  }
  else
  {
    ref = (double)scalers[0] / (double)33330000;
  }

  for(i=0; i<2; i++)
  {
    rate = (double)scalers[i];
    rate = rate / ref;
    if(scalers[i] == 0xFFFFFFFF)
     printf("   %-25s %10u,%.3fHz [OVERFLOW]\n", scalers_name[i], scalers[i], rate);
    else
     printf("   %-25s %10u,%.3fHz\n", scalers_name[i], scalers[i], rate);
  }
  return OK;
}

#ifdef IPC
int
vtpFtofSendErrors(char *host)
{
  char name[100];
  int data[NSERDES];
  unsigned int val;
  int i, slot;
  CHECKINIT;

  for(i=0;i<4;i++)
  {
    vtpSerdesStatus(VTP_SERDES_QSFP, i, 0, data);

    if(data[4] & 0x1) // check channel up bit
      val |= (1<<i);
    else
      val &= ~(1<<i);
  }

  slot = 3+host[7]-'0';
  if(slot>=3 && slot<=8)
  {
    if(!(val & 0x1))  // Fiber 0 = trig2 SSP_SLOTX
    {
      sprintf(name, "err: crate=%s link to trig2 SSP_SLOT%d down", host, slot);
      epics_json_msg_send(name, "int", 1, &data);
    }
  }

  return OK;
}

int
vtpFtofSendScalers(char *host)
{
  char name[100];
  float ref, data[1];
  unsigned int val;
  CHECKINIT;

  printf("%s...", __func__);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  //Read/normalize reference
  val = vtp->v7.sd.Scaler_BusClk;
  if(!val) val = 1;
  ref = 33330000.0f / (float)val;

  data[0] = ref * (float)vtp->v7.ftofTrigger.ScalerHit;

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;

  sprintf(name, "%s_VTPFTOF_CLUSTERS", host);
  epics_json_msg_send(name, "float", 1, data);

  return OK;
}
#endif





/* CND functions */

int
vtpSetCND_thresholds(int thr0, int thr1, int thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_CND,0);

  VLOCK;
  vtp->v7.cndTrigger.Thresholds[0] = thr0;
  vtp->v7.cndTrigger.Thresholds[1] = thr1;
  vtp->v7.cndTrigger.Thresholds[2] = thr2;
  VUNLOCK;

  return OK;
}

int
vtpGetCND_thresholds(int *thr0, int *thr1, int *thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_CND,0);

  VLOCK;
  *thr0 = vtp->v7.cndTrigger.Thresholds[0];
  *thr1 = vtp->v7.cndTrigger.Thresholds[1];
  *thr2 = vtp->v7.cndTrigger.Thresholds[2];
  VUNLOCK;

  return OK;
}

int
vtpSetCND_nframes(int nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_CND,0);

  VLOCK;
  vtp->v7.cndTrigger.NFrames = nframes;
  VUNLOCK;

  return OK;
}

int
vtpGetCND_nframes(int *nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_CND,0);

  VLOCK;
  *nframes = vtp->v7.cndTrigger.NFrames;
  VUNLOCK;

  return OK;
}

int
vtpCndPrintScalers()
{
  double ref, rate;
  int i;
  unsigned int scalers[2];
  const char *scalers_name[2] = {
    "BusClk",
    "Hit"
   };

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_CND,0);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  scalers[0] = vtp->v7.sd.Scaler_BusClk;
  scalers[1] = vtp->v7.cndTrigger.ScalerHit;

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;


  printf("%s - \n", __FUNCTION__);
  if(!scalers[0])
  {
    printf("Error: %s reference time is 0. Reported rates will not be normalized.\n", __func__);
    ref = 1.0;
  }
  else
  {
    ref = (double)scalers[0] / (double)33330000;
  }

  for(i=0; i<2; i++)
  {
    rate = (double)scalers[i];
    rate = rate / ref;
    if(scalers[i] == 0xFFFFFFFF)
     printf("   %-25s %10u,%.3fHz [OVERFLOW]\n", scalers_name[i], scalers[i], rate);
    else
     printf("   %-25s %10u,%.3fHz\n", scalers_name[i], scalers[i], rate);
  }
  return OK;
}

#ifdef IPC
int
vtpCndSendErrors(char *host)
{
  char name[100];
  int data[NSERDES];
  unsigned int val;
  int i;
  CHECKINIT;

  for(i=0;i<4;i++)
  {
    vtpSerdesStatus(VTP_SERDES_QSFP, i, 0, data);

    if(data[4] & 0x1) // check channel up bit
      val |= (1<<i);
    else
      val &= ~(1<<i);
  }

  if(!(val & 0x1))  // Fiber 0 = trig2 SSP_SLOT10
  {
    sprintf(name, "err: crate=%s link to trig2 SSP_SLOT10 down", host);
    epics_json_msg_send(name, "int", 1, &data);
  }

  return OK;
}

int
vtpCndSendScalers(char *host)
{
  char name[100];
  float ref, data[1];
  unsigned int val;
  CHECKINIT;

  printf("%s...", __func__);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  //Read/normalize reference
  val = vtp->v7.sd.Scaler_BusClk;
  if(!val) val = 1;
  ref = 33330000.0f / (float)val;

  data[0] = ref * (float)vtp->v7.cndTrigger.ScalerHit;

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;

  sprintf(name, "%s_VTPCND_CLUSTERS", host);
  epics_json_msg_send(name, "float", 1, data);

  return OK;
}
#endif













/* PCS functions */

int
vtpSetPCS_thresholds(int thr0, int thr1, int thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  VLOCK;
  vtp->v7.pcsTrigger.Thresholds[0] = thr0;
  vtp->v7.pcsTrigger.Thresholds[1] = thr1;
  vtp->v7.pcsTrigger.Thresholds[2] = thr2;
  VUNLOCK;

  return OK;
}

int
vtpGetPCS_thresholds(int *thr0, int *thr1, int *thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  VLOCK;
  *thr0 = vtp->v7.pcsTrigger.Thresholds[0];
  *thr1 = vtp->v7.pcsTrigger.Thresholds[1];
  *thr2 = vtp->v7.pcsTrigger.Thresholds[2];
  VUNLOCK;

  return OK;
}

int
vtpSetPCS_nframes(int nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  VLOCK;
  vtp->v7.pcsTrigger.NFrames = nframes;
  VUNLOCK;

  return OK;
}

int
vtpGetPCS_nframes(int *nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  VLOCK;
  *nframes = vtp->v7.pcsTrigger.NFrames;
  VUNLOCK;

  return OK;
}

int
vtpSetPCS_dipfactor(int dipfactor)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  VLOCK;
  vtp->v7.pcsTrigger.Dipfactor = dipfactor;
  VUNLOCK;

  return OK;
}

int
vtpGetPCS_dipfactor(int *dipfactor)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  VLOCK;
  *dipfactor = vtp->v7.pcsTrigger.Dipfactor;
  VUNLOCK;

  return OK;
}

int
vtpSetPCS_nstrip(int nstripmin, int nstripmax)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  /* nstripmin is not implemented */
  VLOCK;
  vtp->v7.pcsTrigger.NstripMax = nstripmax;
  VUNLOCK;

  return OK;
}

int
vtpGetPCS_nstrip(int *nstripmin, int *nstripmax)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  /* nstripmin is not implemented */
  *nstripmax = 0;

  VLOCK;
  *nstripmax = vtp->v7.pcsTrigger.NstripMax;
  VUNLOCK;

  return OK;
}

int
vtpSetPCS_dalitz(int dalitz_min, int dalitz_max)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  VLOCK;
  vtp->v7.pcsTrigger.DalitzMin = dalitz_min;
  vtp->v7.pcsTrigger.DalitzMax = dalitz_max;
  VUNLOCK;

  return OK;
}

int
vtpGetPCS_dalitz(int *dalitz_min, int *dalitz_max)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  VLOCK;
  *dalitz_min = vtp->v7.pcsTrigger.DalitzMin;
  *dalitz_max = vtp->v7.pcsTrigger.DalitzMax;
  VUNLOCK;

  return OK;
}

#ifdef IPC
int
vtpPcsSendErrors(char *host)
{
  char name[100];
  int data[NSERDES];
  unsigned int val;
  int i, slot;
  CHECKINIT;

  for(i=0;i<4;i++)
  {
    vtpSerdesStatus(VTP_SERDES_QSFP, i, 0, data);

    if(data[4] & 0x1) // check channel up bit
      val |= (1<<i);
    else
      val &= ~(1<<i);
  }

  slot = 3+host[7]-'0';
  if(slot>=3 && slot<=8)
  {
    if(!(val & 0x1))  // Fiber 0 = trig2 SSP_SLOTX
    {
      sprintf(name, "err: crate=%s link to trig2 SSP_SLOT%d down", host, slot);
      epics_json_msg_send(name, "int", 1, &data);
    }
  }

  return OK;
}

int
vtpPcsSendScalers(char *host)
{
  char name[100];
  float ref, data[4], pcudata[1];
  unsigned int val;
  CHECKINIT;

  printf("%s...", __func__);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  //Read/normalize reference
  val = vtp->v7.sd.Scaler_BusClk;
  if(!val) val = 1;
  ref = 33330000.0f / (float)val;

  data[0] = ref * (float)vtp->v7.pcsTrigger.ScalerPeakU;
  data[1] = ref * (float)vtp->v7.pcsTrigger.ScalerPeakV;
  data[2] = ref * (float)vtp->v7.pcsTrigger.ScalerPeakW;
  data[3] = ref * (float)vtp->v7.pcsTrigger.ScalerHit;

  pcudata[0] = ref * (float)vtp->v7.pcuTrigger.ScalerHit;

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;

  sprintf(name, "%s_VTPPCS_CLUSTERS", host);
  epics_json_msg_send(name, "float", 4, data);

  sprintf(name, "%s_VTPPCU_SCALER", host);
  epics_json_msg_send(name, "float", 1, pcudata);

  return OK;
}
#endif

int
vtpSetPCU_thresholds(int thr0, int thr1, int thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  VLOCK;
  vtp->v7.pcuTrigger.Thresholds[0] = thr0;
  vtp->v7.pcuTrigger.Thresholds[1] = thr1;
  vtp->v7.pcuTrigger.Thresholds[2] = thr2;
  VUNLOCK;

  return OK;
}

int
vtpGetPCU_thresholds(int *thr0, int *thr1, int *thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  VLOCK;
  *thr0 = vtp->v7.pcuTrigger.Thresholds[0];
  *thr1 = vtp->v7.pcuTrigger.Thresholds[1];
  *thr2 = vtp->v7.pcuTrigger.Thresholds[2];
  VUNLOCK;

  return OK;
}




/* ECS functions */


int
vtpSetECS_thresholds(int thr0, int thr1, int thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_ECS,0);

  VLOCK;
  vtp->v7.ecsTrigger.Thresholds[0] = thr0;
  vtp->v7.ecsTrigger.Thresholds[1] = thr1;
  vtp->v7.ecsTrigger.Thresholds[2] = thr2;
  VUNLOCK;

  return OK;
}

int
vtpGetECS_thresholds(int *thr0, int *thr1, int *thr2)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_ECS,0);

  VLOCK;
  *thr0 = vtp->v7.ecsTrigger.Thresholds[0];
  *thr1 = vtp->v7.ecsTrigger.Thresholds[1];
  *thr2 = vtp->v7.ecsTrigger.Thresholds[2];
  VUNLOCK;

  return OK;
}

int
vtpSetECS_nframes(int nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_ECS,0);

  VLOCK;
  vtp->v7.ecsTrigger.NFrames = nframes;
  VUNLOCK;

  return OK;
}

int
vtpGetECS_nframes(int *nframes)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_ECS,0);

  VLOCK;
  *nframes = vtp->v7.ecsTrigger.NFrames;
  VUNLOCK;

  return OK;
}

int
vtpSetECS_dipfactor(int dipfactor)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_ECS,0);

  VLOCK;
  vtp->v7.ecsTrigger.Dipfactor = dipfactor;
  VUNLOCK;

  return OK;
}

int
vtpGetECS_dipfactor(int *dipfactor)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_ECS,0);

  VLOCK;
  *dipfactor = vtp->v7.ecsTrigger.Dipfactor;
  VUNLOCK;

  return OK;
}

int
vtpSetECS_nstrip(int nstripmin, int nstripmax)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_ECS,0);

  /* nstripmin is not implemented */
  VLOCK;
  vtp->v7.ecsTrigger.NstripMax = nstripmax;
  VUNLOCK;

  return OK;
}

int
vtpGetECS_nstrip(int *nstripmin, int *nstripmax)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_ECS,0);

  /* nstripmin is not implemented */
  *nstripmax = 0;

  VLOCK;
  *nstripmax = vtp->v7.ecsTrigger.NstripMax;
  VUNLOCK;

  return OK;
}

int
vtpSetECS_dalitz(int dalitz_min, int dalitz_max)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_ECS,0);

  VLOCK;
  vtp->v7.ecsTrigger.DalitzMin = dalitz_min<<3;
  vtp->v7.ecsTrigger.DalitzMax = dalitz_max<<3;
  VUNLOCK;

  return OK;
}

int
vtpGetECS_dalitz(int *dalitz_min, int *dalitz_max)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_ECS,0);

  VLOCK;
  *dalitz_min = vtp->v7.ecsTrigger.DalitzMin>>3;
  *dalitz_max = vtp->v7.ecsTrigger.DalitzMax>>3;
  VUNLOCK;

  return OK;
}


int
vtpEcsPrintScalers()
{
  double ref, rate;
  int i;
  unsigned int scalers[5];
  const char *scalers_name[5] = {
    "BusClk",
    "PeakU",
    "PeakV",
    "PeakW",
    "Hit"
   };

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_ECS,0);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  scalers[0] = vtp->v7.sd.Scaler_BusClk;
  scalers[1] = vtp->v7.ecsTrigger.ScalerPeakU;
  scalers[2] = vtp->v7.ecsTrigger.ScalerPeakV;
  scalers[3] = vtp->v7.ecsTrigger.ScalerPeakW;
  scalers[4] = vtp->v7.ecsTrigger.ScalerHit;

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;


  printf("%s - \n", __FUNCTION__);
  if(!scalers[0])
  {
    printf("Error: %s reference time is 0. Reported rates will not be normalized.\n", __func__);
    ref = 1.0;
  }
  else
  {
    ref = (double)scalers[0] / (double)33330000;
  }

  for(i = 0; i < 5; i++)
  {
    rate = (double)scalers[i];
    rate = rate / ref;
    if(scalers[i] == 0xFFFFFFFF)
     printf("   %-25s %10u,%.3fHz [OVERFLOW]\n", scalers_name[i], scalers[i], rate);
    else
     printf("   %-25s %10u,%.3fHz\n", scalers_name[i], scalers[i], rate);
  }
  return OK;
}

#ifdef IPC
int
vtpEcsSendErrors(char *host)
{
  char name[100];
  int data[NSERDES];
  unsigned int val;
  int i, slot;
  CHECKINIT;

  for(i=0;i<4;i++)
  {
    vtpSerdesStatus(VTP_SERDES_QSFP, i, 0, data);

    if(data[4] & 0x1) // check channel up bit
      val |= (1<<i);
    else
      val &= ~(1<<i);
  }

  slot = 3+host[7]-'0';
  if(slot>=3 && slot<=8)
  {
    if(!(val & 0x1))  // Fiber 0 = trig2 SSP_SLOTX
    {
      sprintf(name, "err: crate=%s link to trig2 SSP_SLOT%d down", host, slot);
      epics_json_msg_send(name, "int", 1, &data);
    }
  }

  return OK;
}

int
vtpEcsSendScalers(char *host)
{
  char name[100];
  float ref, data[4];
  unsigned int val;
  CHECKINIT;

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  //Read/normalize reference
  val = vtp->v7.sd.Scaler_BusClk;
  if(!val) val = 1;
  ref = 33330000.0f / (float)val;

  data[0] = ref * (float)vtp->v7.ecsTrigger.ScalerPeakU;
  data[1] = ref * (float)vtp->v7.ecsTrigger.ScalerPeakV;
  data[2] = ref * (float)vtp->v7.ecsTrigger.ScalerPeakW;
  data[3] = ref * (float)vtp->v7.ecsTrigger.ScalerHit;

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;

  sprintf(name, "%s_VTPECS_CLUSTERS", host);
  epics_json_msg_send(name, "float", 4, data);

  return OK;
}
#endif




/**********************/


int
vtpSetPCScosmic_pixel(int enable)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PC,0);

  if(enable)
    enable = 1;

  VLOCK;
  val = vtp->v7.pcCosmic.Ctrl;
  val = (val & ~VTP_PCCOSMIC_CTRL_PIXEL_MASK) | (enable<<16);
  vtp->v7.pcCosmic.Delay = val;
  VUNLOCK;

  return OK;

}

int
vtpGetPCScosmic_pixel(int *enable)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PC,0);

  VLOCK;
  *enable = (vtp->v7.pcCosmic.Ctrl & VTP_PCCOSMIC_CTRL_PIXEL_MASK)>>16;
  VUNLOCK;

  return OK;
}




int
vtpSetDc_SegmentThresholdMin(int inst, int threshold)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_DC,0);

  if(inst < 0 || inst > 1)
  {
    printf("%s: ERROR - invalid instance %d\n", __func__, inst);
    return ERROR;
  }

  VLOCK;
  vtp->v7.dcrbSegFind[inst].Ctrl = threshold;
  VUNLOCK;

  return OK;
}

int
vtpGetDc_SegmentThresholdMin(int inst, int *threshold)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_DC,0);

  if(inst < 0 || inst > 1)
  {
    printf("%s: ERROR - invalid instance %d\n", __func__, inst);
    return ERROR;
  }

  VLOCK;
  *threshold = vtp->v7.dcrbSegFind[inst].Ctrl;
  VUNLOCK;

  return OK;
}

int
vtpSetGt_latency(int latency)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMMON,0);

  VLOCK;
  vtp->v7.trigOut.Latency = latency/4;
  VUNLOCK;

  return OK;
}

int
vtpGetGt_latency()
{
  int latency;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMMON,0);

  VLOCK;
  latency = vtp->v7.trigOut.Latency;
  VUNLOCK;

  return latency*4;
}

int
vtpSetGt_width(int width)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMMON,0);

  VLOCK;
  vtp->v7.trigOut.Width = width;
  VUNLOCK;

  return OK;
}

int
vtpGetGt_width()
{
  int width;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMMON,0);

  VLOCK;
  width = vtp->v7.trigOut.Width;
  VUNLOCK;

  return width;
}

int
vtpSetTriggerBitDelay(int inst, int delay)
{
  int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMMON,0);

  if((inst < 0) || (inst > 32))
    {
      printf("%s: ERROR - invalid trigger bit %d\n", __func__, inst);
      return ERROR;
    }

  CHECKRANGE_INT(delay, 0, 1020);
  delay = delay/4;

  VLOCK;
  val = vtp->v7.trigOut.Prescaler[inst] & 0xFF00FFFF;
  val|= delay<<16;
  vtp->v7.trigOut.Prescaler[inst] = val;
  VUNLOCK;

  return OK;
}

int
vtpGetTriggerBitDelay(int inst, int *delay)
{
  int rval = 0;

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMMON,0);

  if((inst < 0) || (inst > 32))
    {
      printf("%s: ERROR - invalid trigger bit %d\n", __func__, inst);
      return ERROR;
    }

  VLOCK;
  rval = (vtp->v7.trigOut.Prescaler[inst] >> 16) & 0xFF;
  VUNLOCK;

  *delay = rval*4;

  return OK;
}

int
vtpSetTriggerBitPrescaler(int inst, int prescale)
{
  int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMMON,0);

  if((inst < 0) || (inst > 32))
    {
      printf("%s: ERROR - invalid trigger bit %d\n", __func__, inst);
      return ERROR;
    }

  CHECKRANGE_INT(prescale, 0, 65535);

  VLOCK;
  val = vtp->v7.trigOut.Prescaler[inst] & 0xFFFF0000;
  val|= prescale;
  vtp->v7.trigOut.Prescaler[inst] = val;
  VUNLOCK;

  return OK;
}

int
vtpGetTriggerBitPrescaler(int inst)
{
  int rval = 0;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_COMMON,0);

  if((inst < 0) || (inst > 32))
    {
      printf("%s: ERROR - invalid trigger bit %d\n", __func__, inst);
      return ERROR;
    }

  VLOCK;
  rval = vtp->v7.trigOut.Prescaler[inst] & 0xFFFF;
  VUNLOCK;

  return rval;
}




int
vtpPrintGtTriggerBitRegs()
{
  int strig, strigmask, ctrig, pulser, i, prescaler;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_GT,0);

  for(i=0; i<32; i++)
  {
    VLOCK;
    strig = vtp->v7.gtBit[i].STrigger;
    strigmask= vtp->v7.gtBit[i].STriggerMask;
    ctrig = vtp->v7.gtBit[i].CTrigger;
    pulser = vtp->v7.gtBit[i].Pulser;
    prescaler = vtp->v7.trigOut.Prescaler[i];
    VUNLOCK;
    printf("Bit %d: STrigger = 0x%08X, STriggerMask = 0x%08X, CTrigger = 0x%08X, Pulser = 0x%08X, Prescaler = %d\n", i, strig, strigmask, ctrig, pulser, prescaler);
  }

  return OK;
}

int
vtpSetGtTriggerBit(int inst, int strigger_mask0, int sector_mask0, int mult_min0, int strigger_mask1, int sector_mask1, int mult_min1, int coin_width, int ctrigger_mask, int delay, float pulser_freq, int prescale)
{
  float f;
  int strig, strigmask, strig1, strig1mask, ctrig, pulser;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_GT,0);

  if(inst < 0 || inst > 32)
  {
    printf("%s: ERROR - invalid trigger bit %d\n", __func__, inst);
    return ERROR;
  }

  ctrig = ((ctrigger_mask & 0xF) <<0);

  strig = ((mult_min0     & 0x7) <<4) |
          ((sector_mask0  & 0x3F)<<8) |
          ((coin_width    & 0xFF)<<16) |
          ((delay         & 0xFF)<<24);

  strigmask = ((strigger_mask0 & 0xFFFF) <<0);

  strig1 = ((mult_min1     & 0x7) <<4) |
           ((sector_mask1  & 0x3F)<<8);

  strig1mask = ((strigger_mask1 & 0xFFFF) <<0);

  // convert freq to 250MHz period ticks
  if(pulser_freq > 0.0f)
  {
    f = 250000000.0f / pulser_freq;
    pulser = 0x80000000 | (int)f;
  }
  else
    pulser = 0x00000000;

  VLOCK;
  vtp->v7.gtBit[inst].STrigger = strig;
  vtp->v7.gtBit[inst].STrigger1 = strig1;
  vtp->v7.gtBit[inst].CTrigger = ctrig;
  vtp->v7.gtBit[inst].Pulser = pulser;
  vtp->v7.gtBit[inst].STriggerMask = strigmask;
  vtp->v7.gtBit[inst].STrigger1Mask = strig1mask;
  vtp->v7.trigOut.Prescaler[inst] = prescale;
  VUNLOCK;

  return OK;
}

int
vtpGetGtTriggerBit(int inst, int *strigger_mask0, int *sector_mask0, int *mult_min0, int *strigger_mask1, int *sector_mask1, int *mult_min1, int *coin_width, int *ctrigger_mask, int *delay, float *pulser_freq, int *prescale)
{
  int strig, strigmask, strig1, strig1mask, ctrig, pulser;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_GT,0);

  if(inst < 0 || inst > 32)
  {
    printf("%s: ERROR - invalid trigger bit %d\n", __func__, inst);
    return ERROR;
  }

  VLOCK;
  strig = vtp->v7.gtBit[inst].STrigger;
  strig1 = vtp->v7.gtBit[inst].STrigger1;
  ctrig = vtp->v7.gtBit[inst].CTrigger;
  pulser = vtp->v7.gtBit[inst].Pulser;
  strigmask = vtp->v7.gtBit[inst].STriggerMask;
  strig1mask = vtp->v7.gtBit[inst].STrigger1Mask;
  *prescale = vtp->v7.trigOut.Prescaler[inst];
  VUNLOCK;

  *coin_width     = (strig>>16)&0xFF;
  *delay          = (strig>>24)&0xFF;
  *ctrigger_mask  = (ctrig>>0)&0xF;

  *mult_min0      = (strig>>4)&0x7;
  *sector_mask0   = (strig>>8)&0x3F;
  *strigger_mask0 = (strigmask>>0)&0xFFFF;

  *mult_min1      = (strig1>>4)&0x7;
  *sector_mask1   = (strig1>>8)&0x3F;
  *strigger_mask1 = (strig1mask>>0)&0xFFFF;

  // convert 250MHz period ticks to freq
  if(pulser & 0x80000000)
  {
    pulser &= 0x7FFFFFFF;
    if(pulser)
      *pulser_freq = 250000000.0f / ((float)pulser);
    else
      *pulser_freq = 0.0;
  }
  else
    *pulser_freq = 0.0f;

  return OK;
}

#ifdef IPC
int
vtpGtSendScalers(char *host)
{
  char name[100];
  float ref, data[32];
  int idata[32];
  unsigned int val;
  int i;
  CHECKINIT;

  printf("%s...", __func__);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  //Read/normalize reference
  val = vtp->v7.sd.Scaler_BusClk;
  if(!val) val = 1;
  ref = 33330000.0f / (float)val;

  //Trigger bit scalers
  for(i=0; i<32; i++)
    data[i] = ref * (float)vtp->v7.gtBit[i].TriggerScaler;
  vtp->v7.sd.ScalerLatch = 0;

  //Trigger bit prescalers
  for(i=0; i<32; i++)
    idata[i] = vtp->v7.trigOut.Prescaler[i];
  VUNLOCK;

  sprintf(name, "%s_VTPGT_TRIGGERBITS", host);
  epics_json_msg_send(name, "float", 32, data);

  sprintf(name, "%s_VTPGT_PRESCALES", host);
  epics_json_msg_send(name, "int", 32, idata);

  return OK;
}
#endif

int
vtpSDPrintScalers()
{
  int i;
  unsigned int status, gtscalers[4];
  const char *scalers_name[4] = {
    "BusClk",
    "Sync",
    "Trig1",
    "Trig2"};

  CHECKINIT;


  VLOCK;
  status = vtp->v7.sd.Status;
  vtp->v7.sd.ScalerLatch = 1;
  gtscalers[0] = vtp->v7.sd.Scaler_BusClk;
  gtscalers[1] = vtp->v7.sd.Scaler_Sync;
  gtscalers[2] = vtp->v7.sd.Scaler_Trig1;
  gtscalers[3] = vtp->v7.sd.Scaler_Trig2;
  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;


  printf("%s - \n", __FUNCTION__);
  printf("   Status = 0x%x\n",status);
  for(i = 0; i < 4; i++)
    {
      printf("   %-25s %10u\n", scalers_name[i], gtscalers[i]);
    }
  return OK;
}

int
vtpGtPrintScalers()
{
  double ref, rate;
  int i;
  unsigned int gtscalers[36];
  const char *scalers_name[36] = {
    "BusClk",
    "Sync",
    "Trig1",
    "Trig2",
    "Trigger0",
    "Trigger1",
    "Trigger2",
    "Trigger3",
    "Trigger4",
    "Trigger5",
    "Trigger6",
    "Trigger7",
    "Trigger8",
    "Trigger9",
    "Trigger10",
    "Trigger11",
    "Trigger12",
    "Trigger13",
    "Trigger14",
    "Trigger15",
    "Trigger16",
    "Trigger17",
    "Trigger18",
    "Trigger19",
    "Trigger20",
    "Trigger21",
    "Trigger22",
    "Trigger23",
    "Trigger24",
    "Trigger25",
    "Trigger26",
    "Trigger27",
    "Trigger28",
    "Trigger29",
    "Trigger30",
    "Trigger31"
    };

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_GT,0);


  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;
  gtscalers[0] = vtp->v7.sd.Scaler_BusClk;
  gtscalers[1] = vtp->v7.sd.Scaler_Sync;
  gtscalers[2] = vtp->v7.sd.Scaler_Trig1;
  gtscalers[3] = vtp->v7.sd.Scaler_Trig2;
  for(i=0; i<32; i++)
    gtscalers[4+i] = vtp->v7.gtBit[i].TriggerScaler;
//    gtscalers[4+i] = vtp->v7.sd.Scaler_Trigger[i];
  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;


  printf("%s - \n", __FUNCTION__);
  if(!gtscalers[0])
  {
    printf("Error: %s reference time is 0. Reported rates will not be normalized.\n", __func__);
    ref = 1.0;
  }
  else
  {
    ref = (double)gtscalers[0] / (double)33330000;
  }

  for(i = 0; i < 36; i++)
  {
    rate = (double)gtscalers[i];
    rate = rate / ref;
    if(gtscalers[i] == 0xFFFFFFFF)
     printf("   %-25s %10u,%.3fHz [OVERFLOW]\n", scalers_name[i], gtscalers[i], rate);
    else
     printf("   %-25s %10u,%.3fHz\n", scalers_name[i], gtscalers[i], rate);
  }
  return OK;
}

int
vtpPcsPrintScalers()
{
  double ref, rate;
  int i;
  unsigned int scalers[6];
  const char *scalers_name[6] = {
    "BusClk",
    "PeakU",
    "PeakV",
    "PeakW",
    "Hit",
    "Pcu"
   };

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_PCS,0);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  scalers[0] = vtp->v7.sd.Scaler_BusClk;
  scalers[1] = vtp->v7.pcsTrigger.ScalerPeakU;
  scalers[2] = vtp->v7.pcsTrigger.ScalerPeakV;
  scalers[3] = vtp->v7.pcsTrigger.ScalerPeakW;
  scalers[4] = vtp->v7.pcsTrigger.ScalerHit;
  scalers[5] = vtp->v7.pcuTrigger.ScalerHit;

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;


  printf("%s - \n", __FUNCTION__);
  if(!scalers[0])
  {
    printf("Error: %s reference time is 0. Reported rates will not be normalized.\n", __func__);
    ref = 1.0;
  }
  else
  {
    ref = (double)scalers[0] / (double)33330000;
  }

  for(i = 0; i < 6; i++)
  {
    rate = (double)scalers[i];
    rate = rate / ref;
    if(scalers[i] == 0xFFFFFFFF)
     printf("   %-25s %10u,%.3fHz [OVERFLOW]\n", scalers_name[i], scalers[i], rate);
    else
     printf("   %-25s %10u,%.3fHz\n", scalers_name[i], scalers[i], rate);
  }
  return OK;
}

int
vtpPrintHist_PeakPosition(int inst)
{
  uint32_t val;
  float hist_u[36], hist_v[36], hist_w[36];
  float tot_u = 0.0f, tot_v = 0.0f, tot_w = 0.0f;
  float scale, fval;
  int i;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_EC,0);

  VLOCK;
    vtp->v7.ecTrigger[inst].HistCtrl &= ~0x00000003;
    val = vtp->v7.ecTrigger[inst].HistTime;
    scale = (float)val;
    if(scale != 0)
    {
      scale = scale * 256.0f / 250.0E6;
      scale = 1.0f / scale;
    }
    else
    {
      scale = 1.0f;
      printf("%s: Error - normalization invalid. Raw counts will be displayed.\n", __func__);
    }

    for(i=0;i<256;i++)
    {
      val = vtp->v7.ecTrigger[inst].HistPeakPosition;
      fval = scale * (float)val;
      if(i>=0 && i<=35)
      {
        hist_u[i-0] = fval;
        tot_u+= fval;
      }
      else if(i>=64 && i<=99)
      {
        hist_v[i-64] = fval;
        tot_v+= fval;
      }
      else if(i>=128 && i<=163)
      {
        hist_w[i-128] = fval;
        tot_w+= fval;
      }
    }
    vtp->v7.ecTrigger[inst].HistCtrl |= 0x00000003;
  VUNLOCK;

  printf("ecTrig Peak Position Histogram(u,v,w):\n");
  for(i=0;i<36;i++)
    printf("strip %2d: %9.2f, %9.2f, %9.2f\n", i, hist_u[i], hist_v[i], hist_w[i]);
  printf("    total: %9.2f, %9.2f, %9.2f\n", tot_u, tot_v, tot_w);

  return OK;
}

int
vtpPrintHist_ClusterPosition(int inst)
{
  float hist_uv[36][36];
  float tot=0;
  float scale, fval;
  uint32_t val;
  int i,u,v;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_EC,0);

  VLOCK;
    vtp->v7.ecTrigger[inst].HistCtrl &= ~0x00000005;
    val = vtp->v7.ecTrigger[inst].HistTime;
    scale = (float)val;
    if(scale != 0)
    {
      scale = scale * 256.0f / 250.0E6;
      scale = 1.0f / scale;
    }
    else
    {
      scale = 1.0f;
      printf("%s: Error - normalization invalid. Raw counts will be displayed.\n", __func__);
    }

    for(i=0;i<4096;i++)
    {
      val = vtp->v7.ecTrigger[inst].HistClusterPosition;
      fval = scale * (float)val;

      tot+= fval;
      u = i%64;
      v = i/64;
      if(u<36 && v<36)
        hist_uv[u][v] = fval;
    }
    vtp->v7.ecTrigger[inst].HistCtrl |= 0x00000005;
  VUNLOCK;

  printf("ecTrig Cluster Position Histogram:\n");
  for(u=0;u<36;u++)
  {
    for(v=0;v<36;v++)
      printf("%4.0f ", hist_uv[u][v]);
    printf("\n");
  }
  printf("total = %f\n", tot);

  return OK;
}

int
vtpSetHcal_ClusterCoincidence(int coin)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HCAL,0);

  if(coin > 7)
  {
    printf("%s: Error - invalid coincidence specified %dns\n", __func__, coin);
    coin = 7;
  }

  VLOCK;
  vtp->v7.hcal.ClusterPulseCoincidence = coin/4;
  VUNLOCK;

  return OK;
}

int
vtpGetHcal_ClusterCoincidence(int *coin)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HCAL,0);

  VLOCK;
  *coin = vtp->v7.hcal.ClusterPulseCoincidence * 4;
  VUNLOCK;

  return OK;
}

int
vtpSetHcal_ClusterThreshold(int thr)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HCAL,0);

  if(thr > 8191)
  {
    printf("%s: Error - invalid threshold specified %dns\n", __func__, thr);
    thr = 8191;
  }

  VLOCK;
  vtp->v7.hcal.ClusterPulseThreshold = thr;
  VUNLOCK;

  return OK;
}

int
vtpGetHcal_ClusterThreshold(int *thr)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_HCAL,0);

  VLOCK;
  *thr = vtp->v7.hcal.ClusterPulseThreshold;
  VUNLOCK;

  return OK;
}


int
vtpDcPrintScalers()
{
  double ref, rate;
  int i;
  unsigned int scalers[7];
  const char *scalers_name[7] = {
    "BusClk",
    "SL1",
    "SL2",
    "SL3",
    "SL4",
    "SL5",
    "SL6"
   };

  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_DC,0);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  scalers[0] = vtp->v7.sd.Scaler_BusClk;
  for(i=0;i<6;i++)
    scalers[i+1] = vtp->v7.dcrbRoadFind.Scalers[i];

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;


  printf("%s - \n", __FUNCTION__);
  if(!scalers[0])
  {
    printf("Error: %s reference time is 0. Reported rates will not be normalized.\n", __func__);
    ref = 1.0;
  }
  else
  {
    ref = (double)scalers[0] / (double)33330000;
  }

  for(i=0; i<7; i++)
  {
    rate = (double)scalers[i];
    rate = rate / ref;
    if(scalers[i] == 0xFFFFFFFF)
     printf("   %-25s %10u,%.3fHz [OVERFLOW]\n", scalers_name[i], scalers[i], rate);
    else
     printf("   %-25s %10u,%.3fHz\n", scalers_name[i], scalers[i], rate);
  }
  return OK;
}

int
vtpGetDc_RoadId(char *id_str)
{
  int i, id[2];
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_DC,0);

  VLOCK;
  id[0] = vtp->v7.dcrbRoadFind.Id[0];
  id[1] = vtp->v7.dcrbRoadFind.Id[1];
  VUNLOCK;

  for(i=0;i<8;i++)
  {
    id_str[i] = (id[i/4]>>(i%4)) & 0xff;
    if(!isprint(id_str[i]))
      id_str[i] = '!';
  }
  id_str[8] = 0;

  return OK;
}

#ifdef IPC
int
vtpDcSendErrors(char *host)
{
  char name[100];
  int data[NSERDES];
  unsigned int val;
  int i, sector, region;
  CHECKINIT;

  for(i=0;i<4;i++)
  {
    vtpSerdesStatus(VTP_SERDES_QSFP, i, 0, data);

    if(data[4] & 0x1) // check channel up bit
      val |= (1<<i);
    else
      val &= ~(1<<i);
  }

  sector = host[2]-'0';
  region = host[3]-'0';
  if(sector>=1 && sector<=6)
  {
    if(region==1 || region==2)
    {
      if(!(val & 0x2))  // Fiber 1 = dcX3vtp
      {
        sprintf(name, "err: crate=%s link to dc%d3vtp down", host, sector);
        epics_json_msg_send(name, "int", 1, &data);
      }
    }
    if(region==3)
    {
      if(!(val & 0x1))  // Fiber 0 = trig2 SSP_SLOTX
      {
        sprintf(name, "err: crate=%s link to trig2 SSP_SLOT%d down", host, 2+sector);
        epics_json_msg_send(name, "int", 1, &data);
      }
    }
  }

  return OK;
}

int
vtpDcSendScalers(char *host)
{
  char name[100];
  float ref, data[6];
  int i;
  unsigned int val;
  CHECKINIT;

  printf("%s...", __func__);

  VLOCK;
  vtp->v7.sd.ScalerLatch = 1;

  //Read/normalize reference
  val = vtp->v7.sd.Scaler_BusClk;
  if(!val) val = 1;
  ref = 33330000.0f / (float)val;

  for(i=0;i<6;i++)
    data[i] = ref * (float)vtp->v7.dcrbRoadFind.Scalers[i];

  vtp->v7.sd.ScalerLatch = 0;
  VUNLOCK;

  sprintf(name, "%s_VTPDC_SUPERLAYER", host);
  epics_json_msg_send(name, "float", 6, data);

  return OK;
}
#endif



/* TI LINK Functions */


/* vtpTiAck() Only used for mode=0 to acknowledge a trigger */
int
vtpTiAck()
{
  CHECKINIT;

  VLOCK;
  vtp->tiLink.Ctrl = VTP_TI_CTRL_ACK;
  VUNLOCK;

  return OK;
}

int
vtpTiLinkSetMode(int mode)
{
  /* mode=0 is full software ROC mode. All TI and Event data must be read out and acknowledged
            by a software trigger routine 
     mode=1 is hardware mode. TI and Event data are sent by FPGAs. The CPU can optionally
            include a data bank by enabling CPUSyncEvents (in the ROC Ctrl Register)
  */

  CHECKINIT;

  VLOCK;

  if(mode)
    vtp->tiLink.Ctrl = (1<<4);
  else
    vtp->tiLink.Ctrl = 0;

  VUNLOCK;

  return OK;
}


int
vtpTiLinkGetBlockLevel(int print)
{
  int val = 0;
  unsigned int reg=0;
  CHECKINIT;

  /* Need to do a RMW here t opreserve the Ctrl Register */
  VLOCK;
  reg = vtp->tiLink.Ctrl;
  vtp->tiLink.Ctrl = (reg|VTP_TI_CTRL_BL_REQ);
  VUNLOCK;

  usleep(1000);

  VLOCK;
  val = vtp->tiLink.Status & 0xFF;
  VUNLOCK;

  if(print)
    printf("%s: returned %d\n", __func__, val);

  return val;
}


#define TI_LINK_INIT_TRIES    3
int
vtpTiLinkInit()
{
  int i, val;
  CHECKINIT;

  for(i = 0; i < TI_LINK_INIT_TRIES; i++)
  {
    VLOCK;
    vtp->tiLink.LinkReset = VTP_TI_LINKRESET_RX | VTP_TI_LINKRESET_PLL | VTP_TI_LINKRESET_RX_FIFO;
    vtp->tiLink.LinkReset = VTP_TI_LINKRESET_RX | VTP_TI_LINKRESET_RX_FIFO;
    vtp->tiLink.LinkReset = VTP_TI_LINKRESET_RX_FIFO;
    vtp->tiLink.LinkReset = 0;
    VUNLOCK;

    usleep(10000);

    VLOCK;
    val = vtp->tiLink.LinkStatus;
    VUNLOCK;

    if(val & VTP_TI_LINKSTATUS_RX_READY)
    {
      printf("%s: VTP <-> TI Link RX Ready (status=0x%08X)\n", __func__, val);
      break;
    }
    else
      printf("%s: *** Warning *** VTP <-> TI Link NOT Ready (status=0x%08X)...", __func__, val);

    if(i != TI_LINK_INIT_TRIES-1)
      printf("trying again.\n");
    else
    {
      printf("failed.\n");
      printf("%s: *** ERROR *** VTP <-> TI Link problem.\n", __func__);
    }
  }

  return OK;
}

int
vtpTiLinkStatus()
{
  int val, rval = OK;
  CHECKINIT;

  VLOCK;
  val = vtp->tiLink.LinkStatus;
  VUNLOCK;

  printf("%s: LinkStatus   = 0x%08X\n"
	 "      RxReady    = %u\n"
	 "      RxLocked   = %u\n"
	 "      RxErrorCnt = %u\n",
         __func__, val,
         (val & VTP_TI_LINKSTATUS_RX_READY) ? 1:0,
         (val & VTP_TI_LINKSTATUS_RX_LOCKED) ? 1:0,
         (val & VTP_TI_LINKSTATUS_RX_ERROR_CNT_MASK)
        );

  if((val & VTP_TI_LINKSTATUS_RX_ERROR_CNT_MASK) > 1000)
    {
      rval = ERROR;
    }

  return rval;
}

int
vtpTiLinkResetFifo(int rx)
{
  CHECKINIT;

  VLOCK;
  if(rx) {
  vtp->tiLink.LinkReset |= VTP_TI_LINKRESET_RX_FIFO;
  vtp->tiLink.LinkReset &= ~VTP_TI_LINKRESET_RX_FIFO;
  }else{
  vtp->tiLink.LinkReset |= VTP_TI_LINKRESET_FIFO;
  vtp->tiLink.LinkReset &= ~VTP_TI_LINKRESET_FIFO;
  }
  VUNLOCK;

  return OK;
}

AXI_DMA_REGS *vtpDmaGet(int id)
{
  if(id == VTP_DMA_TI)
    return (AXI_DMA_REGS *)&vtp->dma_ti;
  else if(id == VTP_DMA_VTP)
    return (AXI_DMA_REGS *)&vtp->dma_vtp;

  return NULL;
}

int
vtpDmaStatus(int id)
{
  AXI_DMA_REGS *pDma = vtpDmaGet(id);
  uint32_t cr, sr, len, da;
  uint32_t eb_stat, eb_ctrl;
  CHECKINIT;

  if(!pDma)
    return ERROR;

  VLOCK;
  cr  = pDma->S2MM_DMACR;
  sr  = pDma->S2MM_DMASR;
  len = pDma->S2MM_LENGTH;
  da  = pDma->S2MM_DA;
  eb_stat = vtp->eb.EbStatus;
  eb_ctrl = vtp->eb.EbCtrl;
  VUNLOCK;

  printf("\n");
  printf("  DMA Control       : 0x%08x\n", cr);
  printf("      Status        : 0x%08x (%s)\n",
	 sr,
	 (sr & AXI_DMA_STATUS_IDLE) ? "IDLE" : "NOT IDLE" );
  if(sr & AXI_DMA_STATUS_ERROR_MASK)
    printf("         * ERROR *\n");
  if(sr & AXI_DMA_STATUS_IRQ_MASK)
    printf("         * INTERRUPT GENERATED *\n");
  printf("      Buffer Length : 0x%08x (%d)\n", len, len);
  printf("      Dest Address  : 0x%08x\n", da);
  printf("\n");
  printf("  Event Builder Status  : 0x%08x\n", eb_stat);
  printf("                Control : 0x%08x\n", eb_ctrl);
  printf("\n");

  return OK;
}


int
vtpDmaInit(int id)
{
  AXI_DMA_REGS *pDma = vtpDmaGet(id);
  CHECKINIT;

  if(!pDma)
    return ERROR;

  printf("%s: start\n", __func__);
  vtpDmaStatus(id);

  VLOCK;
  pDma->S2MM_DMACR =
    (0<<0)  |   // 0-stops, 1-starts DMA engine
    (1<<1)  |   // reserved, defaults to 1
    (1<<2);     // 1-reset DMA engine

  pDma->S2MM_DMACR =
    (0<<0)  |   // 0-stops, 1-starts DMA engine
    (1<<1)  |   // reserved, defaults to 1
    (0<<2);     // 1-reset DMA engine
  VUNLOCK;

  printf("%s: end \n", __func__);
  vtpDmaStatus(id);

  return OK;
}

int
vtpDmaStart(int id, unsigned int destAddr, int maxLength)
{
  AXI_DMA_REGS *pDma = vtpDmaGet(id);
  CHECKINIT;

  if(!pDma)
    return ERROR;

  VLOCK;
  pDma->S2MM_DMACR =
    (1<<0)  |   // 0-stops, 1-starts DMA engine
    (1<<1)  |   // reserved, defaults to 1
    (0<<2);     // 1-reset DMA engine

  pDma->S2MM_DA_MSB = 0;
  pDma->S2MM_DA = destAddr;
  pDma->S2MM_LENGTH = maxLength;

  VUNLOCK;

  return OK;
}

int
vtpDmaWaitDone(int id)
{
  AXI_DMA_REGS *pDma = vtpDmaGet(id);
  int rval = 0, status;
  unsigned int cnt = 0;
  CHECKINIT;

  if(!pDma)
    return ERROR;


  while(1)
  {
    VLOCK;
    status = pDma->S2MM_DMASR;
    VUNLOCK;

    if((status & 0x3) == 0x2)
    {
      VLOCK;
      rval = pDma->S2MM_LENGTH;
      VUNLOCK;
      break;
    }
    else if(++cnt > 1000000)
    {
      printf("%s(%d): *** timeout ***\n", __func__, id);
      //vtpDmaStatus(id);

      /*disabling dma engine*/
      vtpDmaInit(id);

      break;
    }
  }

  return rval;
}


int
vtpTiLinkReadEvent(uint32_t *pBuf, uint32_t maxsize)
{
  int status, cnt = 0;
  CHECKINIT;

  if(vtpTiLinkEventReadErrors)
    printf("{vtpTiLinkEventReadErrors=%d}\n", vtpTiLinkEventReadErrors);

  int retry=100;
  while(cnt < maxsize)
    {
      VLOCK;
      status = vtp->tiLink.EBStatus;
      VUNLOCK;

      if(status & 0x1)
	{
	  if(retry-- > 0)
	    {
	      continue;
	    }
	  else
	    {
	      vtpTiLinkEventReadErrors++;
	      printf("vtpTiLinkReadEvent: TIMEOUT ERROR (cnt=%d)\n", vtpTiLinkEventReadErrors);
	      break;
	    }
	}

      VLOCK;
      *pBuf++ = vtp->tiLink.EB_TiFifo;
      VUNLOCK;

      if(status & 0x10000)
	break;

      if(++cnt > maxsize)
	{
	  printf("too many event words...exiting\n");
	  break;
	}
    }

  return cnt;
}

int
vtpTIData2TriggerBank(volatile uint32_t *data, int ndata)
{
  uint32_t word;
  int iword=0, iblkhead, iblktrl, rval = OK;

#define TI_DATA_TYPE_DEFINE_MASK           0x80000000
#define TI_WORD_TYPE_MASK                  0x78000000
#define TI_FILLER_WORD_TYPE                0x78000000
#define TI_BLOCK_HEADER_WORD_TYPE          0x00000000
#define TI_BLOCK_TRAILER_WORD_TYPE         0x08000000

  /* Work down to find index of block header */
  while(iword<ndata)
    {

      word = data[iword];

      if(word & TI_DATA_TYPE_DEFINE_MASK)
	{
	  if(((word & TI_WORD_TYPE_MASK)) == TI_BLOCK_HEADER_WORD_TYPE)
	    {
	      iblkhead = iword;
	      break;
	    }
	}
      iword++;
    }

  /* Check if the index is valid */
  if(iblkhead == -1)
    {
      printf("%s: ERROR: Failed to find TI Block Header\n",
	     __func__);

      return ERROR;
    }

  if(iblkhead != 0)
    {
      printf("%s: WARN: Invalid index (%d) for the TI Block header.\n",
	     __func__, iblkhead);
    }

  /* Work up to find index of block trailer */
  iword=ndata-1;
  while(iword>=0)
    {

      word = data[iword];

      if(word & TI_DATA_TYPE_DEFINE_MASK)
	{
	  if(((word & TI_WORD_TYPE_MASK)) == TI_BLOCK_TRAILER_WORD_TYPE)
	    {
	      iblktrl = iword;
	      break;
	    }
	}
      iword--;
    }

  /* Check if the index is valid */
  if(iblktrl == -1)
    {
      printf("%s: ERROR: Failed to find TI Block Trailer\n",
	     __func__);

      return ERROR;
    }

  /* Get the block trailer, and check the number of words contained in it */
  word = data[iblktrl];

  if((iblktrl - iblkhead + 1) != (word & 0x3fffff))
    {
      printf("%s: Number of words inconsistent (index count = %d, block trailer count = %d\n",
	     __func__,
	     (iblktrl - iblkhead + 1), word & 0x3fffff);

      return ERROR;
    }

  /* Modify the total words returned */
  rval = iblktrl - iblkhead;

  /* Write in the Trigger Bank Length */
  data[iblkhead] = rval-1;

  return rval;
}


#define VTP_EB_NRETRIES   10000

int
vtpEbReadEvent(uint32_t *pBuf, uint32_t maxsize)
{
  int status, cnt = 0;
  CHECKINIT;

  if(vtpEbEventReadErrors)
    printf("{vtpEbEventReadErrors=%d}\n", vtpEbEventReadErrors);

  int retry=VTP_EB_NRETRIES;
  while(cnt < maxsize)
  {
    VLOCK;
    status = vtp->eb.EbStatus;
    VUNLOCK;

    if(status & 0x2)
    {
      if(retry-- > 0)
      {
        continue;
      }
      else
      {
        vtpEbEventReadErrors++;
        printf("vtpEbReadEvent: TIMEOUT ERROR (cnt=%d)\n", vtpEbEventReadErrors);
      break;
      }
    }

    VLOCK;
    *pBuf++ = vtp->eb.VtpFifo;
    VUNLOCK;

    if(status & 0x20000)
      break;

    if(++cnt > maxsize)
    {
      printf("too many event words...exitting\n");
      break;
    }
  }

  return cnt;
}

int
vtpEbReadEvent_test(uint32_t *pBuf, uint32_t maxsize)
{
  int status, cnt = 0, tries = 0;
  CHECKINIT;

  while(cnt < maxsize)
  {
    VLOCK;
    status = vtp->eb.EbStatus;
    VUNLOCK;

    // if buffer is empty, try again until data is ready
    if(status & 0x2)
    {
      tries++;
      printf("{cnt=%d}", cnt);
      if(tries > 20)
        return cnt;
      continue;
    }
    tries = 0;

    VLOCK;
    *pBuf++ = vtp->eb.VtpFifo;
    VUNLOCK;

    if(status & 0x20000)
      break;

    if(++cnt > maxsize)
    {
      printf("too many event words...exitting\n");
      break;
    }
  }

  return cnt;
}

int
vtpEbReadTestEvent(uint32_t *pBuf, uint32_t maxsize)
{
  int status, cnt = 0;
  CHECKINIT;

  while(cnt < maxsize)
  {
    VLOCK;
    status = vtp->eb.EbStatus;
    VUNLOCK;

    if(status & 0x4)
      break;

    VLOCK;
    *pBuf++ = vtp->eb.TestFifo;
    VUNLOCK;

    if(status & 0x40000)
      break;

    if(++cnt > maxsize)
    {
      printf("too many event words...exitting\n");
      break;
    }
  }

  return cnt;
}

int
vtpBReady()
{
  int status;
  CHECKINIT;

  VLOCK;
  status = vtp->eb.EbStatus;
  VUNLOCK;

  // bit 0 = ti event buffer empty flag
  // bit 1 = vtp event buffer empty flag
  //if(status & 0x3) return(0); /* both TI and VTP: not ready */

  if(status & 0x1) return(0); /* TI only: not ready */

  return(1);
}

int
vtpEbDecodeEvent(uint32_t *pBuf, uint32_t size)
{
  uint32_t tag = 0, index = 0, val, last_val, v;
  char view_array[4] = {'U', 'V', 'W', '?'};

  printf("%s: Decoding VTP event buffer:\n", __func__);
  while(size--)
  {
    last_val = val;
    val = *pBuf++;

    if(val & 0x80000000)
    {
      index = 0;
      tag = (val>>27) & 0xF;
    }
    else
      index++;


    printf("%08X: ", val);

    switch(tag)
    {
      case 0:  // Block Header Tag
        printf("Block Header:");
        printf(" Block Count = %d,", (val>>8) & 0x3FF);
        printf(" Block Size = %d\n", (val>>0) & 0xFF);
        break;

      case 1:  // Block Trailer Tag
        printf("Block Trailer:");
        printf(" Word Count = %d\n", (val>>0) & 0x3FFFFF);
        break;

      case 2:  // Event Header Tag
        printf("Event Header:");
        printf(" Event Number: %d\n", (val>>0) & 0x3FFFFF);
        break;

      case 3:  // Trigger Time Tag
        if(index == 1)
        {
          printf("Trigger Time:");
          printf(" 23:0: %d,", (last_val>>0) & 0xFFFFFF);
          printf(" 47:24: %d\n", (val>>0) & 0xFFFFFF);
        }
        break;

      case 4:  // ECtrig Peak Tag
        if(index == 1)
        {
          printf("ECtrig Peak:");
          printf(" Inst = %1d", (last_val>>26) & 0x1);
          printf(" View = %c", view_array[(last_val>>24) & 0x3]);
          printf(" Coord = %2.2f", ((float)((last_val>>15) & 0x1ff)) / 8.0f);
          printf(" Energy = %4d", (last_val>>2) & 0x1fff);
          printf(" Time = %4d\n", (val>>0) & 0x7ff);
        }
        break;

      case 5:  // ECtrig Cluster Tag
        if(index == 1)
        {
          printf("ECtrig Cluster:");
          printf(" Inst = %1d", (last_val>>26) & 0x1);
          printf(" Coord W = %2.2f", ((float)((last_val>>17) & 0x1ff)) / 8.0f);
          printf(" Coord V = %2.2f", ((float)((last_val>>8) & 0x1ff)) / 8.0f);

          v = ((last_val<<1)&0x1fe) | ((val>>30)&0x1);
          printf(" Coord U = %2.2f", ((float)v) / 8.0f);
          printf(" Energy = %4d", (val>>17) & 0x1fff);
          printf(" Time = %4d\n", (val>>0) & 0x7ff);
        }
        break;

      case 6:  // Trigger bit Tag
        printf("Trigger bit:");
        printf(" Inst = %1d", (val>>16)&0x1);
        printf(" Lane = %2d", (val>>11)&0x1f);
        printf(" Time = %4d\n", (val>>0)&0x7ff);
        break;

      default:
        printf("*** UNKNOWN TAG/WORD *** 0x%08X\n", val);
        break;
    }
  }

  return OK;
}

int
vtpEbReadAndDecodeEvent()
{
  uint32_t buf[1000], size;

  size = vtpEbReadEvent(buf, sizeof(buf)/sizeof(buf[0]));
  return vtpEbDecodeEvent(buf, size);

  return OK;
}

static int
vtpFPGAOpen()
{

  if(vtpFPGAFD > 0)
    {
      VTP_DBGN(VTP_DEBUG_INIT, "VTP FPGA already opened\n");
      return OK;
    }

  VTP_DBGN(VTP_DEBUG_INIT, "open FPGA device = %s\n", vtpFPGADev);
  vtpFPGAFD = open(vtpFPGADev, O_RDWR | O_SYNC);

  if(vtpFPGAFD < 0)
    {
      printf("%s: ERROR from open: %s (%d)\n",
	     __func__, strerror(errno), errno);
      return ERROR;
    }

  printf(" size = %d\n", (int)sizeof(ZYNC_REGS)); /* should be 131072 */

  vtp = (volatile ZYNC_REGS *) mmap((void *)VTP_ZYNC_PHYSMEM_BASE, sizeof(ZYNC_REGS),
				    PROT_READ|PROT_WRITE, MAP_SHARED,
				    vtpFPGAFD, 0);

  if(vtp == MAP_FAILED)
    {
      printf("%s: ERROR from mmap: %s (%d)\n",
	     __func__, strerror(errno), errno);
      return ERROR;
    }

  return OK;
}

static int
vtpFPGAClose()
{
  if(vtpFPGAFD < 0)
    {
      printf("%s: ERROR: VTP FPGA not opened.\n",
	     __func__);
      return ERROR;
    }

  if(vtp)
    {
      if(munmap((void *)vtp, sizeof(ZYNC_REGS)) < 0)
	printf("%s: ERROR from munmap: %s (%d)\n",
	       __func__, strerror(errno), errno);

      vtp = NULL;
    }

  close(vtpFPGAFD);
  return OK;
}


int
vtpOpen(int dev_mask)
{
  VTP_DBGN(VTP_DEBUG_INIT, "dev_mask = 0x%x\n", dev_mask);

  if(dev_mask & VTP_FPGA_OPEN)
    {
      if(vtpDevOpenMASK & VTP_FPGA_OPEN)
	{
	  VTP_DBGN(VTP_DEBUG_INIT, "FPGA Already Opened\n");
	}
      else
	{
	  if(vtpFPGAOpen() == OK)
	    {
	      vtpDevOpenMASK |= VTP_FPGA_OPEN;
	    }
	  else
	    {
	      printf("%s: ERROR opening V7 map\n",
		     __func__);
	    }
	}
    }

  if(dev_mask & VTP_I2C_OPEN)
    {
      if(vtpDevOpenMASK & VTP_I2C_OPEN)
	{
	  VTP_DBGN(VTP_DEBUG_INIT, "I2C Already Opened\n");
	}
      else
	{
	  if(vtpI2COpen() == OK)
	    {
	      vtpDevOpenMASK |= VTP_I2C_OPEN;
	    }
	  else
	    {
	      printf("%s: ERROR opening I2C device\n",
		     __func__);
	    }
	}
    }

  if(dev_mask & VTP_SPI_OPEN)
    {
      if(vtpDevOpenMASK & VTP_SPI_OPEN)
	{
	  VTP_DBGN(VTP_DEBUG_INIT, "SPI Already Opened\n");
	}
      else
	{
	  if(vtpSPIOpen() == OK)
	    {
	      vtpDevOpenMASK |= VTP_SPI_OPEN;
	    }
	  else
	    {
	      printf("%s: ERROR opening SPI device\n",
		     __func__);
	    }
	}
    }

  if(addr_shm==NULL)
    vtpCreateLockShm();

  return vtpDevOpenMASK;
}

int
vtpClose(int dev_mask)
{
  if(dev_mask & VTP_SPI_OPEN)
    {
      if(vtpSPIClose() == OK)
	{
	  vtpDevOpenMASK &= ~VTP_SPI_OPEN;
	}
    }

  if(dev_mask & VTP_I2C_OPEN)
    {
      if(vtpI2CClose() == OK)
	{
	  vtpDevOpenMASK &= ~VTP_I2C_OPEN;
	}
    }

  if(dev_mask & VTP_FPGA_OPEN)
    {
      if(vtpFPGAClose() == OK)
	{
	  vtpDevOpenMASK &= ~VTP_FPGA_OPEN;
	}
    }

  vtpKillLockShm(0);

  return vtpDevOpenMASK;
}


static int
vtpMutexInit()
{
  printf("%s: Initializing vtp mutex\n",__FUNCTION__);
  if(pthread_mutexattr_init(&p_sync->m_attr)<0)
    {
      perror("pthread_mutexattr_init");
      printf("%s: ERROR:  Unable to initialized mutex attribute\n",__FUNCTION__);
      return ERROR;
    }
  if(pthread_mutexattr_setpshared(&p_sync->m_attr, PTHREAD_PROCESS_SHARED)<0)
    {
      perror("pthread_mutexattr_setpshared");
      printf("%s: ERROR:  Unable to set shared attribute\n",__FUNCTION__);
      return ERROR;
    }
  if(pthread_mutexattr_setrobust_np(&p_sync->m_attr, PTHREAD_MUTEX_ROBUST_NP)<0)
    {
      perror("pthread_mutexattr_setrobust_np");
      printf("%s: ERROR:  Unable to set robust attribute\n",__FUNCTION__);
      return ERROR;
    }
  if(pthread_mutex_init(&(p_sync->mutex), &p_sync->m_attr)<0)
    {
      perror("pthread_mutex_init");
      printf("%s: ERROR:  Unable to initialize shared mutex\n",__FUNCTION__);
      return ERROR;
    }

  memset(&p_sync->vtp, 0, sizeof(VTPSHMDATA));

  return OK;
}

/*!
  Routine to create (if needed) a shared mutex for VME Bus locking

  @return 0, if successful. -1, otherwise.
*/
int
vtpCreateLockShm()
{
  int fd_shm;
  int needMutexInit=0, stat=0;
  mode_t prev_mode;

  /* First check to see if the file already exists */
  fd_shm = shm_open(shm_name_vtp, O_RDWR,
		    S_IRUSR | S_IWUSR |
		    S_IRGRP | S_IWGRP |
		    S_IROTH | S_IWOTH );
  if(fd_shm<0)
    {
      /* Bad file handler.. */
      if(errno == ENOENT)
	{
	  needMutexInit=1;
	}
      else
	{
	  perror("shm_open");
	  printf(" %s: ERROR: Unable to open shared memory\n",__FUNCTION__);
	  return ERROR;
	}
    }

  if(needMutexInit)
    {
      printf("%s: Creating VTP shared memory file\n",__FUNCTION__);
      prev_mode = umask(0); /* need to override the current umask, if necessary */
      /* Create and map 'mutex' shared memory */
      fd_shm = shm_open(shm_name_vtp, O_CREAT|O_RDWR,
			S_IRUSR | S_IWUSR |
			S_IRGRP | S_IWGRP |
			S_IROTH | S_IWOTH );
      umask(prev_mode);
      if(fd_shm<0)
	{
	  perror("shm_open");
	  printf(" %s: ERROR: Unable to open shared memory\n",__FUNCTION__);
	  return ERROR;
	}
      ftruncate(fd_shm, sizeof(struct shared_memory_struct));
    }

  addr_shm = mmap(0, sizeof(struct shared_memory_struct), PROT_READ|PROT_WRITE, MAP_SHARED, fd_shm, 0);
  if(addr_shm<0)
    {
      perror("mmap");
      printf("%s: ERROR: Unable to mmap shared memory\n",__FUNCTION__);
      return ERROR;
    }
  p_sync = addr_shm;

  if(needMutexInit)
    {
      p_sync->shmSize = sizeof(struct shared_memory_struct);

      stat = vtpMutexInit();
      if(stat==ERROR)
	{
	  printf("%s: ERROR Initializing VTP Mutex\n",
		 __FUNCTION__);
	  return ERROR;
	}
    }
  else
    {
      if(p_sync->shmSize != sizeof(struct shared_memory_struct))
	{
	  printf("%s: ERROR: Inconsistency in size of shared memory structure!\n",
		 __func__);
	  printf("\t File = %d  Library = %d\n",
		 p_sync->shmSize, (int)sizeof(struct shared_memory_struct));
	  printf("\t Possible version mismatch!\n");
	  return ERROR;
	}
    }

  return OK;
}

/*!
  Routine to destroy the shared mutex created by vtpCreateLockShm()

  @return 0, if successful. -1, otherwise.
*/
int
vtpKillLockShm(int kflag)
{
  if(addr_shm)
    {
      if(munmap(addr_shm, sizeof(struct shared_memory_struct))<0)
	perror("munmap");

      if(kflag==1)
	{
	  if(pthread_mutexattr_destroy(&p_sync->m_attr)<0)
	    perror("pthread_mutexattr_destroy");

	  if(pthread_mutex_destroy(&p_sync->mutex)<0)
	    perror("pthread_mutex_destroy");

	  if(shm_unlink(shm_name_vtp)<0)
	    perror("shm_unlink");

	  printf("%s: VTP shared memory mutex destroyed\n",__FUNCTION__);
	}
      addr_shm = NULL;
    }

  return OK;
}

/*!
  Routine to lock the shared mutex created by vtpCreateLockShm()

  @return 0, if successful. -1 or other error code otherwise.
*/
int
vtpLock()
{
  int rval;

  if(p_sync!=NULL)
    {
      rval = pthread_mutex_lock(&(p_sync->mutex));
      if(rval<0)
	{
	  perror("pthread_mutex_lock");
	  printf("%s: ERROR locking VTP\n",__FUNCTION__);
	}
      else if (rval>0)
	{
	  printf("%s: ERROR: %s\n",__FUNCTION__,
		 (rval==EINVAL)?"EINVAL":
		 (rval==EBUSY)?"EBUSY":
		 (rval==EAGAIN)?"EAGAIN":
		 (rval==EPERM)?"EPERM":
		 (rval==EOWNERDEAD)?"EOWNERDEAD":
		 (rval==ENOTRECOVERABLE)?"ENOTRECOVERABLE":
		 "Undefined");
	  if(rval==EOWNERDEAD)
	    {
	      printf("%s: WARN: Previous owner of VTP (mutex) died unexpectedly\n",
		     __FUNCTION__);
	      printf("  Attempting to recover..\n");
	      if(pthread_mutex_consistent_np(&(p_sync->mutex))<0)
		{
		  perror("pthread_mutex_consistent_np");
		}
	      else
		{
		  printf("  Successful!\n");
		  rval=OK;
		}
	    }
	  if(rval==ENOTRECOVERABLE)
	    {
	      printf("%s: ERROR: VTP mutex in an unrecoverable state!\n",
		     __FUNCTION__);
	    }
	}
    }
  else
    {
      printf("%s: ERROR: vtpLock not initialized.\n",__FUNCTION__);
      return ERROR;
    }

  return rval;
}

/*!
  Routine to try to lock the shared mutex created by vtpCreateLockShm()

  @return 0, if successful. -1 or other error code otherwise.
*/
int
vtpTryLock()
{
  int rval=ERROR;

  if(p_sync!=NULL)
    {
      rval = pthread_mutex_trylock(&(p_sync->mutex));
      if(rval<0)
	{
	  perror("pthread_mutex_trylock");
	}
      else if(rval>0)
	{
	  printf("%s: ERROR: %s\n",__FUNCTION__,
		 (rval==EINVAL)?"EINVAL":
		 (rval==EBUSY)?"EBUSY":
		 (rval==EAGAIN)?"EAGAIN":
		 (rval==EPERM)?"EPERM":
		 (rval==EOWNERDEAD)?"EOWNERDEAD":
		 (rval==ENOTRECOVERABLE)?"ENOTRECOVERABLE":
		 "Undefined");
	  if(rval==EOWNERDEAD)
	    {
	      printf("%s: WARN: Previous owner of VTP (mutex) died unexpectedly\n",
		     __FUNCTION__);
	      printf("  Attempting to recover..\n");
	      if(pthread_mutex_consistent_np(&(p_sync->mutex))<0)
		{
		  perror("pthread_mutex_consistent_np");
		}
	      else
		{
		  printf("  Successful!\n");
		  rval=OK;
		}
	    }
	  if(rval==ENOTRECOVERABLE)
	    {
	      printf("%s: ERROR: VTP mutex in an unrecoverable state!\n",
		     __FUNCTION__);
	    }
	}
    }
  else
    {
      printf("%s: ERROR: VTP mutex not initialized\n",__FUNCTION__);
      return ERROR;
    }

  return rval;

}

/*!
  Routine to lock the shared mutex created by vtpCreateLockShm()

  @return 0, if successful. -1 or other error code otherwise.
*/

int
vtpTimedLock(int time_seconds)
{
  int rval=ERROR;
  struct timespec timeout;

  if(p_sync!=NULL)
    {
      clock_gettime(CLOCK_REALTIME, &timeout);
      timeout.tv_nsec = 0;
      timeout.tv_sec += time_seconds;

      rval = pthread_mutex_timedlock(&p_sync->mutex,&timeout);
      if(rval<0)
	{
	  perror("pthread_mutex_timedlock");
	}
      else if(rval>0)
	{
	  printf("%s: ERROR: %s\n",__FUNCTION__,
		 (rval==EINVAL)?"Invalid Argument":
		 (rval==EBUSY)?"Device or Resource Busy":
		 (rval==EAGAIN)?"Maximum number of recursive locks exceeded":
		 (rval==ETIMEDOUT)?"Not locked before specified timeout":
		 (rval==EPERM)?"Operation Not Permitted":
		 (rval==EOWNERDEAD)?"EOWNERDEAD":
		 (rval==ENOTRECOVERABLE)?"Mutex Not Recoverable":
		 "Undefined");
	  if(rval==EOWNERDEAD)
	    {
	      printf("%s: WARN: Previous owner of VTP (mutex) died unexpectedly\n",
		     __FUNCTION__);
	      printf("  Attempting to recover..\n");
	      if(pthread_mutex_consistent_np(&(p_sync->mutex))<0)
		{
		  perror("pthread_mutex_consistent_np");
		}
	      else
		{
		  printf("  Successful!\n");
		  rval=OK;
		}
	    }
	}
    }
  else
    {
      printf("%s: ERROR: VTP mutex not initialized\n",__FUNCTION__);
      return ERROR;
    }

  return rval;

}

/*!
  Routine to unlock the shared mutex created by vtpCreateLockShm()

  @return 0, if successful. -1 or other error code otherwise.
*/
int
vtpUnlock()
{
  int rval=0;
  if(p_sync!=NULL)
    {
      rval = pthread_mutex_unlock(&p_sync->mutex);
      if(rval<0)
	{
	  perror("pthread_mutex_unlock");
	}
      else if(rval>0)
	{
	  printf("%s: ERROR: %s \n",__FUNCTION__,
		   (rval==EINVAL)?"EINVAL":
		   (rval==EBUSY)?"EBUSY":
		   (rval==EAGAIN)?"EAGAIN":
		   (rval==EPERM)?"EPERM":
		   "Undefined");
	}
    }
  else
    {
      printf("%s: ERROR: VTP mutex not initialized.\n",__FUNCTION__);
      return ERROR;
    }
  return rval;
}

/*!
  Routine to check the "health" of the mutex created with vtpCreateLockShm()

  If the mutex is found to be stale (Owner of the lock has died), it will
  be recovered.

  @param time_seconds     How many seconds to wait for mutex to unlock when testing

  @return 0, if successful. -1, otherwise.
*/
int
vtpCheckMutexHealth(int time_seconds)
{
  int rval=0, busy_rval=0;

  if(p_sync!=NULL)
    {
      printf("%s: Checking health of VTP shared mutex...\n",
	     __FUNCTION__);
      /* Try the Mutex to see if it's state (locked/unlocked) */
      printf(" * ");
      rval = vtpTryLock();
      switch (rval)
	{
	case -1: /* Error */
	  printf("%s: rval = %d: Not sure what to do here\n",
		 __FUNCTION__,rval);
	  break;
	case 0:  /* Success - Got the lock */
	  printf(" * ");
	  rval = vtpUnlock();
	  break;

	case EAGAIN: /* Bad mutex attribute initialization */
	case EINVAL: /* Bad mutex attribute initialization */
	  /* Re-Init here */
	  printf(" * ");
	  rval = vtpMutexInit();
	  break;

	case EBUSY: /* It's Locked */
	  {
	    /* Check to see if we can unlock it */
	    printf(" * ");
	    busy_rval = vtpUnlock();
	    switch(busy_rval)
	      {
	      case OK:     /* Got the unlock */
		rval=busy_rval;
		break;

	      case EAGAIN: /* Bad mutex attribute initialization */
	      case EINVAL: /* Bad mutex attribute initialization */
		/* Re-Init here */
		printf(" * ");
		rval = vtpMutexInit();
		break;

	      case EPERM: /* Mutex owned by another thread */
		{
		  /* Check to see if we can get the lock within 5 seconds */
		  printf(" * ");
		  busy_rval = vtpTimedLock(time_seconds);
		  switch(busy_rval)
		    {
		    case -1: /* Error */
		      printf("%s: rval = %d: Not sure what to do here\n",
			     __FUNCTION__,busy_rval);
		      break;

		    case 0:  /* Success - Got the lock */
		      printf(" * ");
		      rval = vtpUnlock();
		      break;

		    case EAGAIN: /* Bad mutex attribute initialization */
		    case EINVAL: /* Bad mutex attribute initialization */
		      /* Re-Init here */
		      printf(" * ");
		      rval = vtpMutexInit();
		      break;

		    case ETIMEDOUT: /* Timeout getting the lock */
		      /* Re-Init here */
		      printf(" * ");
		      rval = vtpMutexInit();
		      break;

		    default:
		      printf("%s: Undefined return from pthread_mutex_timedlock (%d)\n",
			     __FUNCTION__,busy_rval);
		      rval=busy_rval;

		    }

		}
		break;

	      default:
		printf("%s: Undefined return from vtpUnlock (%d)\n",
		       __FUNCTION__,busy_rval);
		      rval=busy_rval;

	      }

	  }
	  break;

	default:
	  printf("%s: Undefined return from vtpTryLock (%d)\n",
		 __FUNCTION__,rval);

	}

      if(rval==OK)
	{
	  printf("%s: Mutex Clean and Unlocked\n",__FUNCTION__);
	}
      else
	{
	  printf("%s: Mutex is NOT usable\n",__FUNCTION__);
	}

    }
  else
    {
      printf("%s: INFO: VTP Mutex not initialized\n",
	     __FUNCTION__);
      return ERROR;
    }

  return rval;
}

#define MEMALLOC_BUFFER_MAX_NUMBER 16

#define MEMALLOC_IOCTL_BASE 1
#define MEMALLOC_RESERVE_CMD         _IO(MEMALLOC_IOCTL_BASE, 0)
#define MEMALLOC_RELEASE_CMD         _IO(MEMALLOC_IOCTL_BASE, 1)
#define MEMALLOC_GET_PHYSICAL_CMD    _IO(MEMALLOC_IOCTL_BASE, 2)
#define MEMALLOC_ACTIVATE_BUFFER_CMD _IO(MEMALLOC_IOCTL_BASE, 3)

static int vtpDmaMemFD = -1;
const char vtpDmaMemDev[256] = "/dev/memalloc";

typedef struct memalloc_ioctl_arg_t
{
  size_t        buffer_size;	/* in */
  int           buffer_id;	/* in, out */
  unsigned long phys_addr;	/* out */
  unsigned long virt_addr;	/* userspace (mmap result) */
} DMA_BUF_INFO;


/* Routine to handle ioctl access to memalloc driver
   cmd:  0 for allocation, 1 for free
   *info: Pointer to memalloc structure that defines the allocated memory
*/
static int
vtpDmaMem(int cmd, DMA_BUF_INFO *info)
{
  int rval=0;

  if(cmd == 0) /* Allocate */
    {
      rval = ioctl(vtpDmaMemFD, MEMALLOC_RESERVE_CMD, info);
      if(rval == -1)
	{
	  perror("ioctl");
	  printf("%s: ERROR reserving memory\n",
		 __func__);
	  return ERROR;
	}

      rval = ioctl(vtpDmaMemFD, MEMALLOC_ACTIVATE_BUFFER_CMD, info);
      if(rval == -1)
	{
	  perror("ioctl");
	  printf("%s: ERROR activating memory (id = %d)\n",
		 __func__, info->buffer_id);
	  return ERROR;
	}

      rval = ioctl(vtpDmaMemFD, MEMALLOC_GET_PHYSICAL_CMD, info);
      if(rval == -1)
	{
	  perror("ioctl");
	  printf("%s: ERROR getting physical address (id = %d)\n",
		 __func__, info->buffer_id);
	  return ERROR;
	}

    }
  else if (cmd == 1) /* Release */
    {
      rval = ioctl(vtpDmaMemFD, MEMALLOC_RELEASE_CMD, info);
      if(rval == -1)
	{
	  perror("ioctl");
	  printf("%s: ERROR releasing memory (id = %d)\n",
		 __func__, info->buffer_id);
	  return ERROR;
	}
    }
  else
    {
      printf("%s: ERROR: Invalid cmd (%d)\n",
	     __func__, cmd);
      return ERROR;
    }

#ifdef DEBUGMEM
  printf("            cmd = %d\n",cmd);
  printf("             id = %d\n",(int)info->buffer_id);
  printf("      phys_addr = %#lx\n",(unsigned long)info->phys_addr);
  printf("      virt_addr = %#lx\n",(unsigned long)info->virt_addr);
  printf("           size = %d\n",(int)info->buffer_size);
#endif

  return OK;
}

/*
  static DMA memory allocator.
  size: size of individual buffer to allocate (in bytes)
  returns a valid DMA_BUF_INFO structure (buffer_id > 0) if successful.
 */
static DMA_BUF_INFO
vtpAllocDmaMemory(int size)
{
  int stat=OK;
  DMA_BUF_INFO rval, info =
    {
      .buffer_size    = size,
      .buffer_id      = -1,
      .phys_addr      = 0,
      .virt_addr      = 0,
    };
  volatile char *tmp_addr;

  stat = vtpDmaMem(0, &info);
  if(stat == -1)
    {
      rval.buffer_id = -1;
      rval.phys_addr = 0;
      rval.virt_addr = 0;
      rval.buffer_size = 0;

      return rval;
    }

  /* Do an mmap here */
  tmp_addr = (volatile char *)mmap(0, size, PROT_READ | PROT_WRITE,
		  MAP_SHARED, vtpDmaMemFD, 0);

  if(tmp_addr == (void*) -1)
    {
      perror("mmap");
      printf("%s: ERROR: mmap failed\n",
	     __func__);

      rval.buffer_id = -1;
      rval.phys_addr = 0;
      rval.virt_addr = 0;
      rval.buffer_size = 0;
    }
  else
    {
      rval.buffer_id = info.buffer_id;
      rval.phys_addr = info.phys_addr;
      rval.virt_addr = (unsigned long)tmp_addr;
      rval.buffer_size = info.buffer_size;
    }

  return rval;
}

/*
  static DMA memory free'r.
  mapInfo: valid DMA_BUF_INFO structure
  returns OK if successful, otherwise ERROR
 */
static int
vtpFreeDmaMemory(DMA_BUF_INFO mapInfo)
{
  int stat=OK;

  stat = vtpDmaMem(1, &mapInfo);

  /* Do an munmap here */
  if(stat != -1)
    {
      stat = munmap((char*)mapInfo.virt_addr, mapInfo.buffer_size);
      if(stat != 0)
	{
	  perror("munmap");
	  stat = ERROR;
	}
    }

  return stat;
}

typedef struct vtpDmaBuffer_t
{
  DMA_BUF_INFO info;
  volatile unsigned int *data;
} vtpDmaBuffer;

vtpDmaBuffer vtpData[MEMALLOC_BUFFER_MAX_NUMBER];

/* User routine to allocate DMA memory

   nbuffer: How many buffers to allocate
   size: size of individual buffers (in bytes)

   returns OK if successful, otherwise error
*/

int
vtpDmaMemOpen(int nbuffer, int size)
{
  int rval = OK, ibuf;

  if(vtpDmaMemFD > 0)
    {
      printf("%s: ERROR: Memory device already open\n",
	     __func__);
      return ERROR;
    }

  if(nbuffer > MEMALLOC_BUFFER_MAX_NUMBER)
    {
      printf("%s: ERROR: Invalid nbuffer (%d). Max = %d\n",
	     __func__, nbuffer, MEMALLOC_BUFFER_MAX_NUMBER);
      return ERROR;
    }

  /* Init */
  for(ibuf = 0; ibuf < MEMALLOC_BUFFER_MAX_NUMBER; ibuf++)
    {
      vtpData[ibuf].info.buffer_id = -1;
    }

  vtpDmaMemFD = open(vtpDmaMemDev, O_RDWR);
  if(vtpDmaMemFD < 0)
    {
      perror("open");
      printf("%s: ERROR opening memory device\n",
	     __func__);
      return ERROR;
    }

  for(ibuf = 0; ibuf < nbuffer; ibuf++)
    {
      vtpData[ibuf].info.buffer_id = -1;

      vtpData[ibuf].info = vtpAllocDmaMemory(size);

      if(vtpData[ibuf].info.buffer_id == -1)
	{
	  printf("%s: Error allocating for buffer %d\n",
		 __func__, ibuf);
	  rval = ERROR;
	  continue;
	}
      vtpData[ibuf].data =
	(volatile unsigned int *) vtpData[ibuf].info.virt_addr;
    }

  return rval;
}

/* User routine to free DMA memory

   returns OK if successful, otherwise error
*/

int
vtpDmaMemClose()
{
  int stat = 0, ibuf;

  if(vtpDmaMemFD < 0)
    {
      printf("%s: ERROR: memory device not open\n",
	     __func__);
      return ERROR;
    }

  for(ibuf = 0; ibuf < MEMALLOC_BUFFER_MAX_NUMBER; ibuf++)
    {
      if(vtpData[ibuf].info.buffer_id != -1)
	vtpFreeDmaMemory(vtpData[ibuf].info);
    }


  stat = close(vtpDmaMemFD);
  if(stat < 0)
    {
      perror("close");
      printf("%s: ERROR closing memory device\n",
	     __func__);
      return ERROR;
    }

  vtpDmaMemFD = -1;

  return OK;
}

/* User routine to return the Physical (Bus) address of specified memory buffer

   buffer_id: ID of buffer

   returns Physical Memory address, if successful.  Otherwise, ERROR.
*/

unsigned long
vtpDmaMemGetPhysAddress(int buffer_id)
{
  return vtpData[buffer_id].info.phys_addr;
}

/* User routine to return the Local (Userspace) address of specified memory buffer

   buffer_id: ID of buffer

   returns Local Memory address, if successful.  Otherwise, ERROR.
*/

unsigned long
vtpDmaMemGetLocalAddress(int buffer_id)
{
  return vtpData[buffer_id].info.virt_addr;
}
