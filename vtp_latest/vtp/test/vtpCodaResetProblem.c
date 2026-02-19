/*
 * File:
 *    vtpCodaResetProblem.c
 *
 * Description:
 *    Try to replicate problem observed by David when the ROC resets
 *
 *  Symptom:
 *     roc crashes if prestart executed, the reset twice
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "vtpLib.h"

int errnum=0;

extern int vtpConfig(char *fname);
extern void vtpInitGlobals();

/* Event Buffer definitions */
#define MAX_EVENT_LENGTH 40960
#define MAX_EVENT_POOL   100

char *rol_usrConfig = NULL;

#define VTP_READ_CONF_FILE {				\
    vtpInitGlobals();					\
    vtpConfig("");					\
    if(rol_usrConfig)					\
      vtpConfig(rol_usrConfig);				\
  }

#define USE_DMA
#define READOUT_TI
#define READOUT_VTP


#ifdef USE_DMA
#define MAXBUFSIZE 100000
unsigned long gDmaBufPhys_TI;
unsigned long gDmaBufPhys_VTP;
#else
#define MAXBUFSIZE 4000
unsigned int gFixedBuf[MAXBUFSIZE];
#endif

int blklevel = 5;
int maxdummywords = 200;
int vtpComptonEnableScalerReadout = 0;

/* trigBankType:
   Type 0xff10 is RAW trigger No timestamps
   Type 0xff11 is RAW trigger with timestamps (64 bits)
*/
int trigBankType = 0xff10;
int firstEvent;

/* CODA routines */
void
coda_download()
{
  printf("%s: called\n",__func__);
  /* vtpOpen(VTP_FPGA_OPEN|VTP_I2C_OPEN|VTP_SPI_OPEN); */
#ifdef USE_DMA
  if(vtpDmaMemOpen(2, MAXBUFSIZE * 4) == OK)
    {
      printf("%s: VTP Memory allocation successful.\n", __func__);
    }
  else
    {
      printf("ERROR: VTP Memory allocation failed");
      return;
    }
#endif
  firstEvent = 1;


  printf("%s: exit\n",__func__);
}

void
coda_prestart()
{
  printf("%s: called\n",__func__);

  printf("calling VTP_READ_CONF_FILE ..\n");fflush(stdout);

  printf("%s: rol->usrConfig = %s\n",
	 __func__, rol_usrConfig);
  VTP_READ_CONF_FILE;

#ifdef USE_DMA
  printf("%s: Initialize DMA\n",
	 __func__);
  if((vtpDmaInit(VTP_DMA_TI)==OK) &&
     (vtpDmaInit(VTP_DMA_VTP) == OK) )
    {
      printf("%s: VTP DMA Initialized\n", __func__);
    }
  else
    {
      printf("ERROR: VTP DMA Init Failed");
      return;
    }
#endif

  vtpTiLinkStatus();

  vtpGetCompton_EnableScalerReadout(&vtpComptonEnableScalerReadout);
  vtpSetCompton_EnableScalerReadout(0);

  printf("%s: exit\n",__func__);
}
void
coda_reset()
{
  printf("%s: called\n",__func__);
#ifdef USE_DMA
  printf("errnum = %d\n",errnum++);
  vtpDmaMemClose();
#endif
  printf("errnum = %d\n",errnum++);
  vtpClose(VTP_FPGA_OPEN|VTP_I2C_OPEN|VTP_SPI_OPEN);
  printf("errnum = %d\n",errnum++);
  printf("%s: exit\n",__func__);
}

int
main(int argc, char *argv[])
{
  coda_download();

  coda_prestart();

  coda_reset();
  coda_reset();

  printf("exit me\n");
  exit(0);
}

/*
  Local Variables:
  compile-command: "make vtpCodaResetProblem"
  End:
 */
