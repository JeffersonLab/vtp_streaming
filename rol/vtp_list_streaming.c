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

#undef USE_DMA
#undef READOUT_TI
#undef READOUT_VTP


#define MAXBUFSIZE 10000
unsigned int gFixedBuf[MAXBUFSIZE];

int blklevel = 1;
int maxdummywords = 200;

/* trigBankType:
   Type 0xff10 is RAW trigger No timestamps
   Type 0xff11 is RAW trigger with timestamps (64 bits)
*/
int trigBankType = 0xff10;
int firstEvent;

#define NUM_VTP_CONNECTIONS 2   /* can be up to 4 */

/**
                        DOWNLOAD
**/
void
rocDownload()
{
  int stat;
  char buf[1000];
  /* Streaming firmware files for VTP */
  const char *z7file="fe_vtp_z7_streaming_may22_1411.bin";
  //  const char *v7file="fe_vtp_v7_fadc_streaming_aug20_0853.bin";
  const char *v7file="fe_vtp_v7_buff_streaming_feb15.bin";

  firstEvent = 1;

  /* Open VTP library */
  stat = vtpOpen(VTP_FPGA_OPEN | VTP_I2C_OPEN | VTP_SPI_OPEN);
  if(stat < 0)
    {
      printf(" Unable to Open VTP driver library.\n");
    }


  /* Load firmware here */
  sprintf(buf, "/usr/local/src/vtp/firmware/%s", z7file);
  if(vtpZ7CfgLoad(buf) != OK)
    {
      printf("Z7 programming failed... (%s)\n", buf);
    }

  sprintf(buf, "/usr/local/src/vtp/firmware/%s", v7file);
  if(vtpV7CfgLoad(buf) != OK)
    {
      printf("V7 programming failed... (%s)\n", buf);
    }


  ltm4676_print_status();

  if(vtpInit(VTP_INIT_CLK_INT))
  {
    printf("vtpInit() **FAILED**. User should not continue.\n");
    return;
  }

}

/**
                        PRESTART
**/
void
rocPrestart()
{
  VTPflag = 0;

  printf("calling VTP_READ_CONF_FILE ..\n");fflush(stdout);

  printf("%s: rol->usrConfig = %s\n", __func__, rol->usrConfig);

  /* Read Config file and Intialize VTP */
  vtpInitGlobals();
  if(rol->usrConfig)
    vtpConfig(rol->usrConfig);


  /* Get Stream connection info from file. Then Setup the VTP connection registers manually and connect */
  {
    int inst;
    unsigned char ipaddr[4];
    unsigned char subnet[4];
    unsigned char gateway[4];
    unsigned char mac[6];
    unsigned char destip[4];
    unsigned short destipport;

    for (inst=0;inst<NUM_VTP_CONNECTIONS;inst++) { 
      printf("Stream # %d\n",inst);
      vtpStreamingGetTcpCfg(
	  inst,
          ipaddr,
          subnet,
          gateway,
          mac,
          destip,
          &destipport
      );
      printf(" ipaddr=%d.%d.%d.%d\n",ipaddr[0],ipaddr[1],ipaddr[2],ipaddr[3]);
      printf(" subnet=%d.%d.%d.%d\n",subnet[0],subnet[1],subnet[2],subnet[3]);
      printf(" gateway=%d.%d.%d.%d\n",gateway[0],gateway[1],gateway[2],gateway[3]);
      printf(" mac=%02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
      printf(" destip=%d.%d.%d.%d\n",destip[0],destip[1],destip[2],destip[3]);
      printf(" destipport=%d\n",destipport);

      /* Set VTP connection registers */
      vtpStreamingSetTcpCfg(
          inst,
          ipaddr,
          subnet,
          gateway,
          mac,
          destip,
          destipport
      );

      /* Make the Connection */
      vtpStreamingTcpConnect(inst,1);
    }
  }

  vtpStatus(1);

  // vtpTiLinkStatus();

  printf(" Done with User Prestart\n");

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
    }

  printf("Calling vtpSerdesStatusAll()\n");
  vtpSerdesStatusAll();

  /*
  blklevel = vtpTiLinkGetBlockLevel(0);
  printf("Block level read from TI: %d\n", blklevel);
  */
  printf("Setting VTP block level to: %d\n", blklevel);
  vtpSetBlockLevel(blklevel);


  vtpV7SetResetSoft(1);
  vtpV7SetResetSoft(0);
  //  vtpEbResetFifo();

  vtpStatus(1);
  vtpSDPrintScalers();

  /* Start Data Streams - this function does nothing */
  //  vtpStreamingTcpGo();

  /* Enable to recieve Triggers */
  CDOENABLE(VTP, 1, 0);
  VTPflag=0; /* disable polling for triggers in streaming mode */
}

/**
                        END
**/
void
rocEnd()
{
  int inst;

  VTPflag = 0;
  //CDODISABLE(VTP, 1, 0);

  vtpStatus(1);

  /* Disconnect Streaming sockets */
  for(inst=0;inst<NUM_VTP_CONNECTIONS;inst++) {
    vtpStreamingTcpConnect(inst,0);
  }

  // vtpSDPrintScalers();
  //  vtpTiLinkStatus();
}

/**
                        READOUT
**/
void
rocTrigger(int EVTYPE)
{
  int ii;
  unsigned int evtnum = *(rol->nevents);

  /* print vtpStatistics every 100 events) */
  if((evtnum%100) == 0) {
    vtpStats();
  }


  /* Open an event, containing Banks */
  CEOPEN(ROCID, BT_BANK, blklevel);

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

  /* Dummy Bank of data */
  CBOPEN(0x11, BT_UI4, blklevel);
  for(ii = 0; ii < 10; ii++)
    {
      *rol->dabufp++ = ii;
    }
  CBCLOSE;

  /* If this is the first event then get the config file info and put it in a Bank */
	if(firstEvent)
	{
		char str[10000];
		int len = vtpUploadAll(str, sizeof(str)-4);
	    str[len] = 0;	
	    str[len+1] = 0;	
	    str[len+2] = 0;	
	    str[len+3] = 0;	
		firstEvent = 0;
		printf("VTP string(len = %d bytes): \n%s\n",len,str);
  		CBOPEN(0x12, BT_UC1, blklevel);
//  		CBOPEN(0x12, BT_UI4, blklevel);
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
  /* If No TI to acknowlege in Streaming mode set code = 1 */
  CDOACK(VTP, 1, 0);
}

/**
                        RESET
**/
void
rocReset()
{
  /* close VTP device */
  vtpClose(VTP_FPGA_OPEN|VTP_I2C_OPEN|VTP_SPI_OPEN);
}


/*
  Local Variables:
  compile-command: "make -k vtp_list.so"
  End:
 */

