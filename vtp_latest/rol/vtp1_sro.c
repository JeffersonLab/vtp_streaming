
/* vtp1.c - first readout list for VTP boards (polling mode) */


#ifdef Linux_armv7l

#define DMA_TO_BIGBUF /*if want to dma directly to the big buffers*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/types.h>
#ifndef VXWORKS
#include <sys/time.h>
#endif

#include "daqLib.h"
#include "vtpLib.h"
#include "vtpConfig.h"

#include "circbuf.h"

/*****************************/
/* former 'crl' control keys */

/* readout list VTP1_SRO */
#define ROL_NAME__ "VTP1_SRO"

/* polling */
#define POLLING_MODE


/* name used by loader */
#define INIT_NAME vtp1_sro__init

#include "rol.h"

void usrtrig(unsigned long, unsigned long);
void usrtrig_done();

/* vtp readout */
#include "VTP_source.h"

/************************/
/************************/

static char rcname[5];
static int block_level = 1;


#define ABS(x)      ((x) < 0 ? -(x) : (x))

#define TIMERL_VAR \
  static hrtime_t startTim, stopTim, dTim; \
  static int nTim; \
  static hrtime_t Tim, rmsTim, minTim=10000000, maxTim, normTim=1

#define TIMERL_START \
{ \
  startTim = gethrtime(); \
}

#define TIMERL_STOP(whentoprint_macros,histid_macros) \
{ \
  stopTim = gethrtime(); \
  if(stopTim > startTim) \
  { \
    nTim ++; \
    dTim = stopTim - startTim; \
    /*if(histid_macros >= 0)   \
    { \
      uthfill(histi, histid_macros, (int)(dTim/normTim), 0, 1); \
    }*/														\
    Tim += dTim; \
    rmsTim += dTim*dTim; \
    minTim = minTim < dTim ? minTim : dTim; \
    maxTim = maxTim > dTim ? maxTim : dTim; \
    /*logMsg("good: %d %ud %ud -> %d\n",nTim,startTim,stopTim,Tim,5,6);*/ \
    if(nTim == whentoprint_macros) \
    { \
      logMsg("timer: %7llu microsec (min=%7llu max=%7llu rms**2=%7llu)\n", \
                Tim/nTim/normTim,minTim/normTim,maxTim/normTim, \
                ABS(rmsTim/nTim-Tim*Tim/nTim/nTim)/normTim/normTim,5,6); \
      nTim = Tim = 0; \
    } \
  } \
  else \
  { \
    /*logMsg("bad:  %d %ud %ud -> %d\n",nTim,startTim,stopTim,Tim,5,6);*/ \
  } \
}

/* for compatibility */
int
getTdcTypes(int *typebyslot)
{
  return(0);
}
int
getTdcSlotNumbers(int *slotnumbers)
{
  return(0);
}

static void
__download()
{

#ifdef POLLING_MODE
  rol->poll = 1;
#else
  rol->poll = 0;
#endif

  printf("\n>>>>>>>>>>>>>>> ROCID=%d, CLASSID=%d <<<<<<<<<<<<<<<<\n",rol->pid,rol->classid);
  printf("CONFFILE >%s<\n\n",rol->confFile);

  /* Clear some global variables etc for a clean start */
  CTRIGINIT;

  /* init trig source VTP */
  CDOINIT(VTP, 1);

  /************/
  /* init daq */

  daqInit();
  DAQ_READ_CONF_FILE;

  printf("INFO: User Download 1 Executed\n");

  return;
}


static void
__prestart()
{
  int i, inst;
  unsigned long jj, adc_id, sl;
  char *env;

  *(rol->nevents) = 0;

#ifdef POLLING_MODE
  /* Register a sync trigger source (polling mode)) */
  CTRIGRSS(VTP, 1, usrtrig, usrtrig_done);
  rol->poll = 1; /* not needed here ??? */
#else
  /* Register a async trigger source (interrupt mode) */
  CTRIGRSA(VTP, 1, usrtrig, usrtrig_done);
  rol->poll = 0; /* not needed here ??? */
#endif

  sprintf(rcname,"RC%02d",rol->pid);
  printf("rcname >%4.4s<\n",rcname);

  printf("calling VTP_READ_CONF_FILE ..\n");fflush(stdout);

  VTP_READ_CONF_FILE;

//  vtpStreamingTcpConnect(0,1);
//  vtpStreamingTcpConnect(1,1);

  for(inst=0;inst<2;inst++)
	//for(inst=0;inst<1;inst++)
  {
    char name[128], host[128], *ch, *myname;
    int port_in, port_out[21];

    int connect=1;
    unsigned char ipaddr[4];
	  unsigned char subnet[4];
	  unsigned char gateway[4];
	  unsigned char mac[6];
	  unsigned char destip[4];
	  unsigned short destipport;

	  myname = getenv("HOST");
	  printf("myname befor >%s<\n",myname);
    // remove everything starting from first dot
    ch = strstr(myname,".");
    if(ch != NULL) *ch = '\0';
	  printf("myname after >%s<\n",myname);
    sprintf(name,"%s-s%d",myname, inst+1);
	  printf("name >%s<\n",name);

    port_in = 0;
    codaGetStreams(name, host, &port_in, port_out);
    printf("From Port: %d\n",port_in);
    printf("To Port: %d\n",port_out[0]);

    if(port_in>0)
	  {
      printf("port_in=%d - connecting\n",port_in);

      vtpStreamingGetTcpCfg(
          inst,
          ipaddr,
          subnet,
          gateway,
          mac,
          destip,
          &destipport
        );

      destipport = port_in;

      printf("ipaddr=%d.%d.%d.%d\n",ipaddr[0],ipaddr[1],ipaddr[2],ipaddr[3]);
      printf("subnet=%d.%d.%d.%d\n",subnet[0],subnet[1],subnet[2],subnet[3]);
      printf("gateway=%d.%d.%d.%d\n",gateway[0],gateway[1],gateway[2],gateway[3]);
      printf("mac=%02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
      printf("destip=%d.%d.%d.%d\n",destip[0],destip[1],destip[2],destip[3]);
      printf("destipport=%d\n",destipport);

      vtpStreamingSetTcpCfg(
          inst,
          ipaddr,
          subnet,
          gateway,
          mac,
          destip,
          destipport
        );

      vtpStreamingTcpConnect(
          inst,
          connect
        );
	  }
    else
	  {
      printf("port_in=%d - NOT connecting\n",port_in);
	  }

  }

  printf("INFO: User Prestart 1 executed\n");

  /* from parser (do we need that in rol2 ???) */
  *(rol->nevents) = 0;
  rol->recNb = 0;

  return;
}

static void
__end()
{
  int ii, total_count, rem_count;

  CDODISABLE(VTP,1,0);

  vtpStreamingTcpConnect(0,0);  //disconnect vtp-s1 socket
  vtpStreamingTcpConnect(1,0);  //disconnect vtp-s2 socket


  printf("INFO: User End 1 Executed\n");

  return;
}

static void
__pause()
{
  CDODISABLE(VTP,1,0);

  printf("INFO: User Pause 1 Executed\n");

  return;
}

static void
__go()
{
  int i;
  char *env;

//  vtpStreamingTcpConnect(0,1);
//  vtpStreamingTcpConnect(1,1);
  vtpStreamingTcpGo();

  printf("INFO: User Go 1 Enabling\n");
  CDOENABLE(VTP,1,1);
  printf("INFO: User Go 1 Enabled\n");

  return;
}

void
usrtrig(unsigned long EVTYPE, unsigned long EVSOURCE)
{
  return;

}

void
usrtrig_done()
{
  return;
}

void
__done()
{
  /* from parser */
  poolEmpty = 0; /* global Done, Buffers have been freed */

  /*printf("__done reached\n");*/


  /* Acknowledge TI */
  CDOACK(VTP,1,1);

  return;
}
  
static void
__status()
{
  return;
}  

/* This routine is automatically executed just before the shared libary
 *    is unloaded.
 *
 *       Clean up memory that was allocated 
 *       */
__attribute__((destructor)) void end (void)
{
  static int ended=0;

  if(ended==0)
    {
      printf("ROC Cleanup\n");

      ended=1;
    }

}

#else

void
vtp1_dummy()
{
  return;
}

#endif

