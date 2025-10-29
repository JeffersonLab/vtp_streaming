/*************************************************************************
 *
 *  test_fadc.c - Library of routines for readout and buffering of 
 *                     events using a JLAB Trigger Interface V3 (TI) with 
 *                     a Linux VME controller in CODA 3.0.
 *
 *                     This for a TI in Master Mode controlling multiple ROCs
 */

/* Event Buffer definitions */
#define MAX_EVENT_POOL     100
#define MAX_EVENT_LENGTH   1024*64      /* Size in Bytes */

/* Define TI Type (TI_MASTER or TI_SLAVE) */
#define TI_SLAVE
/* EXTernal trigger source (e.g. front panel ECL input), POLL for available data */
#define TI_READOUT TI_READOUT_TS_POLL 
/* TI VME address, or 0 for Auto Initialize (search for TI by slot) */
#define TI_ADDR  0           

/* Define Streaming mode */
#define STREAMING_MODE

/* Measured longest fiber length in system */
#define FIBER_LATENCY_OFFSET 0x4A  

#include "dmaBankTools.h"   /* Macros for handling CODA banks */
#include "tiprimary_list.c" /* Source required for CODA readout lists using the TI */
#include "fadcLib.h"        /* library of FADC250 routines */
#include "sdLib.h"          /* VXS Signal Distribution board header */

/* Define initial blocklevel and buffering level */
#define BLOCKLEVEL 1
#define BUFFERLEVEL 4

/* FADC Library Variables */
extern int fadcA32Base, nfadc;
/* Program FADCs to get trigger/clock from SDC or VXS */
#define FADC_VXS

#define NFADC     8
/* Address of first fADC250 */
#define FADC_ADDR 0x180000
/* Increment address to find next fADC250 */
#define FADC_INCR 0x080000

#define FADC_WINDOW_LAT    375
#define FADC_WINDOW_WIDTH   24
#define FADC_MODE           1   /* Hall B modes 1-8 */

/* for the calculation of maximum data words in the block transfer */
unsigned int MAXFADCWORDS=0;


/****************************************
 *  DOWNLOAD
 ****************************************/
void
rocDownload()
{
  int ifa, stat;
  unsigned short iflag;

  /* Setup Address and data modes for DMA transfers
   *   
   *  vmeDmaConfig(addrType, dataType, sstMode);
   *
   *  addrType = 0 (A16)    1 (A24)    2 (A32)
   *  dataType = 0 (D16)    1 (D32)    2 (BLK32) 3 (MBLK) 4 (2eVME) 5 (2eSST)
   *  sstMode  = 0 (SST160) 1 (SST267) 2 (SST320)
   */
  vmeDmaConfig(2,5,1); 


  /*****************
   *   SD SETUP
   *****************/
  /* Init the SD library so we can get status info */
  stat = sdInit(0);
  if(stat==0) 
    {
      tiSetBusySource(TI_BUSY_SWB,1);
      sdSetActiveVmeSlots(0);
      sdStatus(0);
    }
  else
    { /* No SD in this crate or the TI is not in the Proper slot */
      printf("Assuming no SD board in this crate\n"); 
      tiSetBusySource(TI_BUSY_LOOPBACK,1);
    }

  /* Print status for FADCs and TI*/
  tiStatus(0);

  printf("rocDownload: User Download Executed\n");

}

/****************************************
 *  PRESTART
 ****************************************/
void
rocPrestart()
{
  unsigned short iflag;
  int ifa, stat;
  int islot;


  /* Unlock the VME Mutex */
  vmeBusUnlock();

#ifndef FADC_VXS  
  /* Init FADC Library and modules here id using a SDC board in a non-VXS crate */
  iflag = 0xea00; /* SDC Board address A16 */
  iflag |= FA_INIT_EXT_SYNCRESET;  /* Front panel sync-reset */
  iflag |= FA_INIT_FP_TRIG;  /* Front Panel Input trigger source */
  iflag |= FA_INIT_FP_CLKSRC;  /* Internal 250MHz Clock source */
#else
  /* If this is in a VXS crate Get CLK, TRIG, and Sync Reset from VXS */
  iflag = 0x25;
#endif

  /* Initialize FADC library */
  fadcA32Base = 0x09000000;
  vmeSetQuietFlag(0);
  faInit(FADC_ADDR, FADC_INCR, NFADC, (iflag|FA_INIT_SKIP_FIRMWARE_CHECK));
  vmeSetQuietFlag(0);


  /* Program/Init VME Modules Here */
  for(ifa=0; ifa < nfadc; ifa++) 
    {
      faSoftReset(faSlot(ifa),0);
      faResetTriggerCount(faSlot(ifa));

      faEnableSyncReset(faSlot(ifa));

      faEnableBusError(faSlot(ifa));

      /* Set input DAC level - 3250 basically corresponds to 0 shift in the baseline. */
      faSetDAC(faSlot(ifa), 3250, 0);

      /*  Set All channel thresholds to 300 */
      faSetThreshold(faSlot(ifa), 300, 0xffff);
  
      /*********************************************************************************
       * faSetProcMode(int id, int pmode, unsigned int PL, unsigned int PTW, 
       *    int NSB, unsigned int NSA, unsigned int NP, 
       *    unsigned int NPED, unsigned int MAXPED, unsigned int NSAT);
       *
       *  id    : fADC250 Slot number
       *  pmode : Processing Mode
       *          9 - Pulse Parameter (ped, sum, time)
       *         10 - Debug Mode (9 + Raw Samples)
       *    PL : Window Latency
       *   PTW : Window Width

       *   NSB : Number of samples before pulse over threshold
       *   NSA : Number of samples after pulse over threshold
       *    NP : Number of pulses processed per window
       *  BANK : (Hall B option - replaces NPED,MAXPED,NSAT)
       *  NPED : Number of samples to sum for pedestal (4)
       *MAXPED : Maximum value of sample to be included in pedestal sum (250)
       *  NSAT : Number of consecutive samples over threshold for valid pulse (2)
       */
      faSetProcMode(faSlot(ifa),
		    FADC_MODE,   /* Processing Mode */
		    FADC_WINDOW_LAT, /* PL */
		    FADC_WINDOW_WIDTH,  /* PTW */
		    3,   /* NSB */
		    6,   /* NSA */
		    1,   /* NP */
		    0   /* BANK */
		    );

    }

  /* Print Status for TI and SD */
  tiStatus(0);
  sdStatus(0);

  /* Print status for FADCS*/
#ifndef FADC_VXS
  faSDC_Status(0);
#endif
  faGStatus(0);

  printf("rocPrestart: User Prestart Executed\n");

}

/****************************************
 *  GO
 ****************************************/
void
rocGo()
{
  int islot;

  /* Print out the Run Number and Run Type (config id) */
  printf("rocGo: Activating Run Number %d, Config id = %d\n",rol->runNumber,rol->runType);

  /* Get the broadcasted Block and Buffer Levels from TS or TI Master */
  blockLevel = tiGetCurrentBlockLevel();
  bufferLevel = tiGetBroadcastBlockBufferLevel();
  printf("rocGo: Block Level set to %d  Buffer Level set to %d\n",blockLevel,bufferLevel);

  /* In case of slave, set TI busy to be enabled for full buffer level */
  tiUseBroadcastBufferLevel(1);
  /*  tiBusyOnBufferLevel(1); */


  /* Enable/Set Block Level on modules, if needed, here */
  faGSetBlockLevel(blockLevel);

  if(FADC_MODE == 9)
    MAXFADCWORDS = 2 + 4 + blockLevel * 8;
  else /* FADC_MODE == 10 */
    /* MAXFADCWORDS = 2 + 4 + blockLevel * (8 + FADC_WINDOW_WIDTH/2); */
    MAXFADCWORDS = 2000;
  
  /*  Enable FADC */
  faGEnable(0, 0);

}

/****************************************
 *  END
 ****************************************/
void
rocEnd()
{
  int islot;

  /* FADC Disable */
  faGDisable(0);

  /* FADC Event status - Is all data read out */
  faGStatus(0);
  /* Prints status of TI */
  tiStatus(0);
  /* Reset FADC - This will mess up the clock source programming*/
  //  faGReset(0);

  printf("rocEnd: Ended after %d blocks\n",tiGetIntCount());
  
}

/****************************************
 *  TRIGGER
 ****************************************/
void
rocTrigger(int arg)
{
  int ii, islot;
  int ifa, nwords, blockError, stat, dCnt, len=0, idata;
  unsigned int val;
  unsigned int *start;
  unsigned int datascan, scanmask, roCount;

  /* Set TI output 1 high for diagnostics */
  tiSetOutputPort(1,0,0,0);

  roCount = tiGetIntCount(); //Get the TI trigger count

  /* Readout the trigger block from the TI 
     Trigger Block MUST be reaodut first */
  dCnt = tiReadTriggerBlock(dma_dabufp);

  if(dCnt<=0)
    {
      printf("No TI Trigger data or error.  dCnt = %d\n",dCnt);
    }
  else
    { /* TI Data is already in a bank structure.  Bump the pointer */
      dma_dabufp += dCnt;
    }

  /* EXAMPLE: How to open a bank (type=5) and add data words by hand */
  BANKOPEN(5,BT_UI4,blockLevel);
  *dma_dabufp++ = tiGetIntCount();
  *dma_dabufp++ = 0xdead;
  *dma_dabufp++ = 0xcebaf111;
  *dma_dabufp++ = 0xcebaf222;
  BANKCLOSE;

  /* fADC250 Readout */
  BANKOPEN(3,BT_UI4,blockLevel);

  /* Mask of initialized modules */
  scanmask = faScanMask();
  /* Check scanmask for block ready up to 100 times */
  datascan = faGBready(scanmask, 100); 
  stat = (datascan == scanmask);

  if(stat) 
    {
      for(ifa = 0; ifa < nfadc; ifa++)
	{
	  nwords = faReadBlock(faSlot(ifa), dma_dabufp, MAXFADCWORDS, 1);
	  
	  /* Check for ERROR in block read */
	  blockError = faGetBlockError(1);
	  
	  if(blockError) 
	    {
	      printf("ERROR: Slot %d: in transfer (event = %d), nwords = 0x%x\n",
		     faSlot(ifa), roCount, nwords);

	      if(nwords > 0)
		dma_dabufp += nwords;
	    } 
	  else 
	    {
	      dma_dabufp += nwords;
	    }
	}
    }
  else 
    {
      printf("ERROR: Event %d: Datascan != Scanmask  (0x%08x != 0x%08x)\n",
	     roCount, datascan, scanmask);
      *dma_dabufp++ = 0xda0bad003;
      *dma_dabufp++ = roCount;
      *dma_dabufp++ = datascan;
      *dma_dabufp++ = scanmask;
    }
  BANKCLOSE;

  /* Set TI output 0 low */
  tiSetOutputPort(0,0,0,0);

}

void
rocCleanup()
{
  int islot=0;

  printf("%s: Reset all FADCs\n",__FUNCTION__);

  faGReset(1);  
}
