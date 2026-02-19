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

#define STREAMING_MODE


#include <VTP_source.h>

#undef USE_DMA
#undef READOUT_TI
#undef READOUT_VTP

#define MAXBUFSIZE 10000
unsigned int gFixedBuf[MAXBUFSIZE];

int blklevel = 1;
int maxdummywords = 200;
int vtpComptonEnableScalerReadout = 0;

/* trigBankType:
   Type 0xff10 is RAW trigger No timestamps
   Type 0xff11 is RAW trigger with timestamps (64 bits)
*/
int trigBankType = 0xff10;
int firstEvent;

#define NUM_VTP_CONNECTIONS 1   /* can be up to 4 */

/* define an array of Payload port Config Structures */
PP_CONF ppInfo[16]

/**
                        DOWNLOAD
**/
void
rocDownload()
{
  int stat;
  char buf[1000];
  /* Streaming firmware files for VTP */
  //  const char *z7file="fe_vtp_z7_streaming_may22_1411.bin";
  // const char *z7file="fe_vtp_z7_streaming_mar15.bin";
  const char *z7file="fe_vtp_z7_streamingv2_jan27.bin";     /* new version of Streaming Firmware */

  //  const char *v7file="fe_vtp_v7_fadc_streaming_aug20_0853.bin";
  //  const char *v7file="fe_vtp_v7_buff_streaming_feb15.bin";
  //  const char *v7file="fe_vtp_v7_fadc_streaming_nov24.bin";   /* New Readout format */
  const char *v7file="fe_vtp_v7_fadc_streamingv2_feb10.bin";   /* new version of streaming firmware */


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

}

/**
                        PRESTART
**/
void
rocPrestart()
{

  int status;
  unsigned int emuip, emuport;
  int ppmask=0; 


  VTPflag = 0;


  /* Initialize the VTP here since external clock is stable now */
  if(vtpInit(VTP_INIT_CLK_VXS_250))
  {
    printf("vtpInit() **FAILED**. User should not continue.\n");
    return;
  }


  printf("Calling VTP_READ_CONF_FILE ..\n");fflush(stdout);

  printf("%s: rol->usrConfig = %s\n", __func__, rol->usrConfig);

  /* Read Config file and Intialize VTP variables */
  vtpInitGlobals();
  if(rol->usrConfig)
    vtpConfig(rol->usrConfig);


  /* Configure all the payload ports - total of 8 
     pp_id, ppInfo, moduleID, lag, bank_tag, stream#*/
  vtpPayloadConfig(2,ppInfo,1,1,0,1);
  vtpPayloadConfig(4,ppInfo,1,1,0,1);
  vtpPayloadConfig(5,ppInfo,1,1,0,1);
  vtpPayloadConfig(7,ppInfo,1,1,0,1);
  vtpPayloadConfig(10,ppInfo,1,1,0,1);
  vtpPayloadConfig(12,ppInfo,1,1,0,1);
  vtpPayloadConfig(13,ppInfo,1,1,0,1);
  ppmask = vtpPayloadConfig(15,ppInfo,1,1,0,1);

  /* Update the Streaming EB configuration for the new firmware to get the correct PP Mask and ROCID
     PP mask, nstreams, frame_len (ns), ROCID, ppInfo  */
  vtpStreamingSetEbCfg(ppmask, NUM_VTP_CONNECTIONS, 0x1fff, ROCID, ppInfo);


  /* Get Stream connection info from file. Then Setup the VTP connection registers manually and connect */
  {
    int inst;
    unsigned char ipaddr[4];
    unsigned char subnet[4];
    unsigned char gateway[4];
    unsigned char mac[6];
    unsigned char udpaddr[4];
    unsigned char tcpaddr[4];
    unsigned int tcpport, udpport;

    for (inst=0;inst<NUM_VTP_CONNECTIONS;inst++) { 
      printf("Stream # %d\n",(inst+1));
      vtpStreamingGetTcpCfg(
	  inst,
          ipaddr,
          subnet,
          gateway,
          mac,
          udpaddr,
	  tcpaddr,
          &udpport,
	  &tcpport
      );
      printf(" ipaddr=%d.%d.%d.%d\n",ipaddr[0],ipaddr[1],ipaddr[2],ipaddr[3]);
      printf(" subnet=%d.%d.%d.%d\n",subnet[0],subnet[1],subnet[2],subnet[3]);
      printf(" gateway=%d.%d.%d.%d\n",gateway[0],gateway[1],gateway[2],gateway[3]);
      printf(" mac=%02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
      printf(" destip=%d.%d.%d.%d\n",udpaddr[0],udpaddr[1],udpaddr[2],udpaddr[3]);
      printf(" destip=%d.%d.%d.%d\n",tcpaddr[0],tcpaddr[1],tcpaddr[2],tcpaddr[3])
      printf(" udpport=0x%08x  tcpport=0x%08x\n",udpport, tcpport);

      /* Set VTP connection registers */
      vtpStreamingSetTcpCfg(
          inst,
          ipaddr,
          subnet,
          gateway,
          mac,
          destip,
          destipport,
	  localport
      );

      /* Make the Connection */
      //      vtpStreamingTcpConnect(inst,1);

      /* Listen for connections - TCP Mode only*/
      vtpStreamingTcpAccept(inst);
    }
  }

  // Reset the MIG - DDR memory write tagging - for Streaming Ebio 
  vtpStreamingMigReset();

  // Reset the data links between V7 Streaming EB and the Zync TCP client 
  vtpStreamingEbioReset();

  // vtpStatus(1);

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


  // Enable the Streaming EB
  vtpStreamingEbGo();


  vtpStats(0);
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
  //  int inst;

  VTPflag = 0;
  //CDODISABLE(VTP, 1, 0);

  vtpStats(0);

  /* Disconnect Streaming sockets */
  //  for(inst=0;inst<NUM_VTP_CONNECTIONS;inst++) {
  //  vtpStreamingTcpConnect(inst,0);
  //}


  /* Reset all Socket Connections on the TCP Server*/
  vtpStreamingTcpReset(0);


  vtpSDPrintScalers();
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

