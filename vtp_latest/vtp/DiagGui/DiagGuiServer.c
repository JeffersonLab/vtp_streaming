#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>

#include "CrateMsgTypes.h"
#include "vtpLib.h"

void sig_handler(int signo);

ServerCBFunctions gCrateServerCBFcn;

int swap32(int val)
{
  return DW_SWAP(val);
}

short swap16(short val)
{
  return HW_SWAP(val);
}

int
Vtp_Read32(Cmd_Read32 *pCmd_Read32, Cmd_Read32_Rsp *pCmd_Read32_Rsp)
{
	unsigned int pRd = pCmd_Read32->addr; /*vtpRead32(volatile unsigned int *addr)*/
	unsigned int *pWr = (unsigned int *)pCmd_Read32_Rsp->vals;
	int c = pCmd_Read32->cnt;
	int size = 4+4*c;

	pCmd_Read32_Rsp->cnt = c;
	if(pCmd_Read32->flags & CRATE_MSG_FLAGS_ADRINC)
	  while(c--) {*pWr++ = vtpRead32((volatile unsigned int *)pRd); pRd++;}
	else
	  while(c--) {*pWr++ = vtpRead32((volatile unsigned int *)pRd);}

	return size;
}

int
Vtp_Write32(Cmd_Write32 *pCmd_Write32)
{
  unsigned int *pRd = (unsigned int *)pCmd_Write32->vals;
	unsigned int pWr = pCmd_Write32->addr; /*vtpWrite32(volatile unsigned int *addr, unsigned int val)*/
	int c = pCmd_Write32->cnt;
	unsigned int val;

	if(pCmd_Write32->flags & CRATE_MSG_FLAGS_ADRINC)
	  while(c--) {val = *pRd++; vtpWrite32((volatile unsigned int *)pWr, val); pWr++;}
	else
	  while(c--) {val = *pRd++; vtpWrite32((volatile unsigned int *)pWr, val);}

	return 0;
}

int
Vme_Delay(Cmd_Delay *pCmd_Delay)
{
  usleep(1000*pCmd_Delay->ms);

  return(0);
}




int
main(int argc, char *argv[])
{
  int stat;

  if(signal(SIGINT, sig_handler) == SIG_ERR)
  {
	perror("signal");
	exit(0);
  }

  stat = vtpOpen(VTP_FPGA_OPEN);
  if(stat != VTP_FPGA_OPEN)
    {
      printf(" stat = %d\n", stat);
      goto CLOSE;
    }
  else
    {
      printf("vtpOpen'ed\n");
    }

  vtpCheckMutexHealth(1);

  gCrateServerCBFcn.Read16 = NULL;
  gCrateServerCBFcn.Write16 = NULL;
  gCrateServerCBFcn.Read32 = Vtp_Read32;
  gCrateServerCBFcn.Write32 = Vtp_Write32;
  gCrateServerCBFcn.Delay = NULL;
  gCrateServerCBFcn.ReadScalers = NULL;
  gCrateServerCBFcn.ReadData = NULL;
  gCrateServerCBFcn.GetCrateMap = NULL;
  gCrateServerCBFcn.GetBoardParams = NULL;
  gCrateServerCBFcn.GetChannelParams = NULL;
  gCrateServerCBFcn.SetChannelParams = NULL;

  printf("Starting CrateMsgServer...");fflush(stdout);
  CrateMsgServerStart(&gCrateServerCBFcn, CRATEMSG_LISTEN_PORT);
  printf("Done.\n");fflush(stdout);


  while(1) sleep(1);


CLOSE:

  vtpClose(VTP_FPGA_OPEN);
  printf("vtpClose'd\n");

  return 0;
}

void
closeup()
{
  vtpClose(VTP_FPGA_OPEN);

  printf("DiagGUI server closed...\n");
}

void
sig_handler(int signo)
{
	printf("%s: signo = %d\n",__FUNCTION__,signo);
	switch(signo)
	{
		case SIGINT:
			closeup();
			exit(1);  /* exit if CRTL/C is issued */
	}
}
