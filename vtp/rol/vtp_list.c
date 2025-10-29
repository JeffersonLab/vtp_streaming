/*************************************************************************
 *
 *  vtp_list.c -      Library of routines for readout of events using a
 *                    JLAB Trigger Interface V3 (TI) with a VTP in
 *                    CODA 3.0.
 *
 *                    This is for a VTP with serial connection to a TI
 *
 */

/* Event Buffer definitions */
#define MAX_EVENT_LENGTH 40960
#define MAX_EVENT_POOL   100

#include <VTP_source.h>

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
/**
                        DOWNLOAD
**/
void
rocDownload()
{
  /* vtpOpen(VTP_FPGA_OPEN|VTP_I2C_OPEN|VTP_SPI_OPEN); */
#ifdef USE_DMA
  if(vtpDmaMemOpen(2, MAXBUFSIZE * 4) == OK)
    {
      printf("%s: VTP Memory allocation successful.\n", __func__);
    }
  else
    {
      daLogMsg("ERROR","VTP Memory allocation failed");
      return;
    }
#endif
	firstEvent = 1;
}

/**
                        PRESTART
**/
void
rocPrestart()
{
  VTPflag = 0;

  printf("calling VTP_READ_CONF_FILE ..\n");fflush(stdout);

  printf("%s: rol->usrConfig = %s\n",
	 __func__, rol->usrConfig);
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
      daLogMsg("ERROR","VTP DMA Init Failed");
      return;
    }
#endif

  vtpTiLinkStatus();

  vtpGetCompton_EnableScalerReadout(&vtpComptonEnableScalerReadout);
  vtpSetCompton_EnableScalerReadout(0);
}

/**
                        PAUSE
**/
void
rocPause()
{
  VTPflag = 0;
  CDODISABLE(VTP, 1, 0);
}

/**
                        GO
**/
void
rocGo()
{
  if(vtpSerdesCheckLinks() == ERROR)
    {
      daLogMsg("ERROR","VTP Serdes links not up");
      return;
    }

 vtpSerdesStatusAll();

  /* If there's an error in the status, re-initialize */
  if(vtpTiLinkStatus() == ERROR)
    {
      printf("%s: WARN: Error from TI Link status.  Resetting.\n",
	     __func__);
      vtpTiLinkInit();
    }

  blklevel = vtpTiLinkGetBlockLevel(0);
  printf("Block level from TI: %d\n", blklevel);
//  blklevel = 1;
  printf("Setting VTP block level to: %d\n", blklevel);
  vtpSetBlockLevel(blklevel);

  vtpV7SetResetSoft(1);
  vtpV7SetResetSoft(0);
  vtpEbResetFifo();

/* Do DMA readout before Go enabled to clear out any buffered data
   - hack fix until problem with extra TI block header from past run is found */
#ifdef READOUT_TI
  #ifdef USE_DMA
    vtpDmaStart(VTP_DMA_TI, vtpDmaMemGetPhysAddress(0), MAXBUFSIZE*4);
    vtpDmaWaitDone(VTP_DMA_TI);
  #else
    vtpEbTiReadEvent(gpDmaBuf, MAXBUFSIZE);
  #endif
#endif




  vtpSDPrintScalers();

  vtpSetCompton_EnableScalerReadout(vtpComptonEnableScalerReadout);

  VTPflag = 1;
  CDOENABLE(VTP, 1, 0);
}

/**
                        END
**/
void
rocEnd()
{
  VTPflag = 0;
  CDODISABLE(VTP, 1, 0);
  vtpDmaStatus(0);
  vtpSDPrintScalers();
  vtpTiLinkStatus();
}

/**
                        READOUT
**/
void
rocTrigger(int EVTYPE)
{
  int ii;
#if defined(READOUT_TI) | defined(READOUT_VTP)
  int len;
  volatile unsigned int *pBuf;
#endif

#ifndef READOUT_TI
  unsigned long evtnum;
  evtnum = *(rol->nevents);
#endif

  /* Open an event, containing Banks */
  CEOPEN(ROCID, BT_BANK, blklevel);

#ifdef READOUT_TI
#ifdef USE_DMA
  vtpDmaStart(VTP_DMA_TI, vtpDmaMemGetPhysAddress(0), MAXBUFSIZE * 4);
#endif
#endif

#ifdef READOUT_VTP
#ifdef USE_DMA
  vtpDmaStart(VTP_DMA_VTP, vtpDmaMemGetPhysAddress(1), MAXBUFSIZE * 4);
#endif
#endif

#ifdef READOUT_TI
#ifdef USE_DMA
  len = vtpDmaWaitDone(VTP_DMA_TI) >> 2;
  if(len)
    len--;
  pBuf = (volatile unsigned int *) vtpDmaMemGetLocalAddress(0);
#else
  len = vtpEbTiReadEvent(gpDmaBuf, MAXBUFSIZE);
  pBuf = (volatile unsigned int *) gFixedBuf;
#endif
  if(len > 1000)
    {
      printf("LEN1=%d\n", len);
      for(ii = 0; ii < len; ii++)
	printf("vtpti[%2d] = 0x%08x\n", (int)ii, pBuf[ii]);
    }

  len = vtpTIData2TriggerBank(pBuf, len);

  for(ii = 0; ii < len; ii++)
    {
      *rol->dabufp++ = pBuf[ii];
    }
#else
  /* Open a trigger bank */
  CBOPEN(trigBankType, BT_SEG, blklevel);
  for(ii = 0; ii < blklevel; ii++)
    {
      if(trigBankType == 0xff11)
	{
	  *rol->dabufp++ = (EVTYPE << 24) | (0x01 << 16) | (3);
	}
      else
	{
	  *rol->dabufp++ = (EVTYPE << 24) | (0x01 << 16) | (1);
	}

      *rol->dabufp++ = (blklevel * (evtnum - 1) + (ii + 1));

      if(trigBankType == 0xff11)
	{
	  *rol->dabufp++ = 0x12345678;
	  *rol->dabufp++ = 0;
	}
    }

  /* Close trigger bank */
  CBCLOSE;
#endif

#ifdef READOUT_VTP
#ifdef USE_DMA
  len = vtpDmaWaitDone(VTP_DMA_VTP) >> 2;
  if(len)
    len--;
  pBuf = (volatile unsigned int *) vtpDmaMemGetLocalAddress(1);
#else
  len = vtpEbReadEvent(pBuf, MAXBUFSIZE);
  pBuf = (volatile unsigned int *) gFixedBuf;
#endif

  if(len > (MAXBUFSIZE / 4))	/* if we are using more then 25% of the buffer, print message */
    {
      printf("LEN2=%d\n", len);
      for(ii = 0; ii < len; ii++)
	printf("vtp[%2d] = 0x%08x\n", (int)ii, pBuf[ii]);
    }

  CBOPEN(0x56, BT_UI4, blklevel);
  for(ii = 0; ii < len; ii++)
    {
      *rol->dabufp++ = pBuf[ii];
    }
  CBCLOSE;
#endif
  CBOPEN(0x11, BT_UI4, blklevel);
  for(ii = 0; ii < 10; ii++)
    {
      *rol->dabufp++ = ii;
    }
  CBCLOSE;

  
	if(firstEvent)
	{
		char str[10000];
		int len = vtpUploadAll(str, sizeof(str)-4);
	    str[len] = 0;	
	    str[len+1] = 0;	
	    str[len+2] = 0;	
	    str[len+3] = 0;	
		firstEvent = 0;
//  		CBOPEN(0x12, BT_UB1, blklevel);
  		CBOPEN(0x12, BT_UI4, blklevel);
		for(ii = 0; ii < (len+3)/4; ii++)
		{
		  unsigned int val;
		  val = ((str[ii*4+0])<<0) |
				((str[ii*4+1])<<8) |
				((str[ii*4+2])<<16) |
				((str[ii*4+3])<<24);
		 *rol->dabufp++ = val;
		}
		CBCLOSE;
	}
	



  /* Close event */
  CECLOSE;

}

/**
                        READOUT ACKNOWLEDGE
**/
void
rocTrigger_done()
{
  CDOACK(VTP, 0, 0);
}

/**
                        RESET
**/
void
rocReset()
{
#ifdef USE_DMA
  vtpDmaMemClose();
#endif
  vtpClose(VTP_FPGA_OPEN|VTP_I2C_OPEN|VTP_SPI_OPEN);
}

/*
  Local Variables:
  compile-command: "make -k vtp_list.so"
  End:
 */
