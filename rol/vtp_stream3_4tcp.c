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

#define NUM_VTP_CONNECTIONS 4   /* Can be up to 4 */
#define VTP_NET_MODE        0   /*  0=TCP 1=UDP   */

/* define an array of Payload port Config Structures */
PP_CONF ppInfo[16];

/* Data necessary to connect via TCP using EMUSockets
#define CMSG_MAGIC_INT1 0x634d7367
#define CMSG_MAGIC_INT2 0x20697320
#define CMSG_MAGIC_INT3 0x636f6f6c
*/
unsigned int emuData[] = {0x634d7367,0x20697320,0x636f6f6c,6,0,4196352,1,1};
//unsigned int emuData[] = {0x67734d63,0x20736920,0x6cf6f663,0x06000000,0,0x00084000,0x01000000,0x01000000};


/**
                        DOWNLOAD
**/
void
rocDownload()
{
  int stat;
  char buf[1000];
  /* Streaming Version 3 firmware files for VTP */
  //  const char *z7file="fe_vtp_z7_streamingv2_jan27.bin";      /* new version of Streaming Firmware */
  //  const char *z7file="fe_vtp_z7_streamingv3_Mar15.2.bin";    /* Version 3 UDP support - fix UDP checksums v2 */
  //   const char *z7file="fe_vtp_z7_streamingv3_Mar18.9K.bin";     /* Version 3 UDP support - Jumbo 9K packets - Server*/
  // const char *z7file="fe_vtp_z7_streamingv3_Mar18.9K.Cl.bin";  /* Version 3 UDP support - Jumbo 9K packets - Client*/
  //    const char *z7file="fe_vtp_z7_streamingv3_jun22.bin";
  //const char *z7file="fe_vtp_z7_streamingv3_july19.bin";        /* fixed polarity issue on net ports 3,4 */
  //const char *z7file="fe_vtp_z7_streamingv3_july20.bin";        /* 1500 MTU frames */
  const char *z7file="fe_vtp_z7_streamingv3_Jul21.bin";        /* programable MTU */

  //  const char *v7file="fe_vtp_v7_fadc_streamingv2_feb10.bin";   /* new version of streaming firmware */
    //  const char *v7file="fe_vtp_v7_fadc_streamingv3_Mar02.bin";   /* version 3 of streaming firmware */
    // const char *v7file="fe_vtp_v7_fadc_streamingv3_Mar23.bin";     /* fix for multiple streams */
    //   const char *v7file="fe_vtp_v7_fadc_streamingv3_Apr13.bin";     /* Add Async data fifo for EB */
  //    const char *v7file="fe_vtp_v7_fadc_streamingv3_jun17.bin";
  const char *v7file="fe_vtp_v7_fadc_streamingv3_Jul21.bin";        /* fixed 0 hit FADC frame bug */

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
  unsigned int emuip, emuport;
  int stat, ppmask=0;
  int netMode=VTP_NET_MODE; // 0=TCP, 1=UDP
  int localport = 10001; // default local TCP port

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


  /* Get EB connection info to program the VTP TCP stack */
    emuip = vtpRoc_inet_addr(rol->rlinkP->net);
    emuport = rol->rlinkP->port;
    printf("EMU Info from ROC:  IP=0x%08x  port=%d\n",emuip,emuport);
  //  emuip   = vtpRoc_inet_addr("129.57.109.232");  /* Debug to indra-s3 */
  emuip   = vtpRoc_inet_addr("129.57.109.230");
  emuport = 46102;
  localport = localport + netMode;
  printf(" EMU IP = 0x%08x  Port= %d  Localport = %d\n",emuip, emuport, localport);


  /* Configure all the payload ports - total of 8 
     pp_id, ppInfo, moduleID, lag, bank_tag, stream# 
     4 FADCs to Stream 1 and 4 to Stream 2*/
  vtpPayloadConfig(2,ppInfo,1,1,0,1);
  vtpPayloadConfig(4,ppInfo,1,1,0,1);
  vtpPayloadConfig(5,ppInfo,1,1,0,2);
  vtpPayloadConfig(7,ppInfo,1,1,0,2);
  vtpPayloadConfig(10,ppInfo,1,1,0,3);
  vtpPayloadConfig(12,ppInfo,1,1,0,3);
  vtpPayloadConfig(13,ppInfo,1,1,0,4);
  ppmask = vtpPayloadConfig(15,ppInfo,1,1,0,4);


  /* Update the Streaming EB configuration for the new firmware to get the correct PP Mask and ROCID
     PP mask, nstreams, frame_len (ns), ROCID, ppInfo  */
  vtpStreamingSetEbCfg(ppmask, NUM_VTP_CONNECTIONS, 0xffff, ROCID, ppInfo);
  emuData[4] = ROCID;  /* define ROCID in the EMU Connection data as well*/

  /* Enable the Streaming EB to allow Async Events. Disable Stream processing for the moment */
  stat = vtpStreamingEbEnable(VTP_STREB_ASYNC_FIFO_EN);
  if(stat != OK)
    printf("Error in vtpStreamingEbEnable()\n");

  // Reset the MIG - DDR memory write tagging - for Streaming Ebio 
  vtpStreamingMigReset();

  // Reset the data links between V7 Streaming EB and the Zync TCP client 
  // Set the Network output mode
  vtpStreamingEbioReset(netMode);


  /* Get Stream connection info from file. Then Setup the VTP connection registers manually and connect */
  {
    int inst;
    unsigned char ipaddr[4];
    unsigned char subnet[4];
    unsigned char gateway[4];
    unsigned char mac[6];
    unsigned char udpaddr[4], tcpaddr[4], destip[4];
    unsigned int tcpport, udpport;

    /* fix the destination IP address so it is correct */
    destip[3] = (emuip&0xFF); 
    destip[2] = (emuip&0xFF00)>>8; 
    destip[1] = (emuip&0xFF0000)>>16;
    destip[0] = (emuip&0xFF000000)>>24;

    /* Get the base network port info from the initial Config file */
    vtpStreamingGetNetCfg(
	  0,
          ipaddr,
          subnet,
          gateway,
          mac,
          udpaddr,
	  tcpaddr,
          &udpport,
	  &tcpport
      );

    /* Loop over all connections */
    for (inst=0;inst<NUM_VTP_CONNECTIONS;inst++) { 

      /* Increment the Local IP and MAC address and destination port for each Network connection */
      printf("Stream # %d\n",(inst+1));
      if(inst>0) {
	ipaddr[3] = ipaddr[3] + 1;
	mac[4]    =    mac[4] + 1;
	emuport   = emuport   + 1;
      }

      printf(" ipaddr=%d.%d.%d.%d\n",ipaddr[0],ipaddr[1],ipaddr[2],ipaddr[3]);
      printf(" subnet=%d.%d.%d.%d\n",subnet[0],subnet[1],subnet[2],subnet[3]);
      printf(" gateway=%d.%d.%d.%d\n",gateway[0],gateway[1],gateway[2],gateway[3]);
      printf(" mac=%02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
      printf(" udpaddr=%d.%d.%d.%d\n",udpaddr[0],udpaddr[1],udpaddr[2],udpaddr[3]);
      printf(" tcpaddr=%d.%d.%d.%d\n",tcpaddr[0],tcpaddr[1],tcpaddr[2],tcpaddr[3]);
      printf(" udpport=0x%08x  tcpport=0x%08x\n",udpport, tcpport);


      /* Set VTP connection registers */
      vtpStreamingSetNetCfg(
          inst,
	  netMode,
          ipaddr,
          subnet,
          gateway,
          mac,
          destip,
          emuport,
	  localport
      );

      /* Read them back one more time */
      vtpStreamingGetNetCfg(
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
      printf(" udpaddr=%d.%d.%d.%d\n",udpaddr[0],udpaddr[1],udpaddr[2],udpaddr[3]);
      printf(" tcpaddr=%d.%d.%d.%d\n",tcpaddr[0],tcpaddr[1],tcpaddr[2],tcpaddr[3]);
      printf(" udpport=0x%08x  tcpport=0x%08x\n",udpport, tcpport);


      /* Make the Connection - only for Client mode - disable the cMSg connection data 8->0*/
      vtpStreamingTcpConnect(inst, (netMode+1), emuData, 0);

      /* Reset the TCP interface and listen for connection requests 
         In UDP mode this will just allow packets to be sent out */
      // vtpStreamingTcpAccept(inst);

    }
  }

  // Reset the MIG - DDR memory write tagging - for Streaming Ebio 
  //  vtpStreamingMigReset();

  // Reset the data links between V7 Streaming EB and the Zync TCP client 
  // Set the Network output mode
  //vtpStreamingEbioReset(netMode);


  // Enable the streaming EB here instead of in GO for a test
  //  vtpStreamingEbGo();
  // Hack to send emuData in the fifo now - 8 words 
  // Must send before Go in the Aggregator or the data flow will hang
  //vtpStreamingAsyncInfoWrite(8);

  // Send a Prestart Event
  vtpStreamingEvioWriteControl(0,EV_PRESTART,rol->runNumber,rol->runType);

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

  int stat;

 // Enable the Streaming EB
 // vtpStreamingEbGo();
  // Hack to send emuData in the fifo now - 8 words 
  //vtpStreamingAsyncInfoWrite(8);

  
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



  vtpStats(0);
  vtpSDPrintScalers();

  /*Send Go Event*/
  vtpStreamingEvioWriteControl(0,EV_GO,0,0);

  /* Enable StreamingEB Stream processing */
  stat = vtpStreamingEbEnable(VTP_STREB_PP_STREAM_EN);
  if(stat != OK)
    printf("Error in vtpStreamingEbEnable()\n");

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
  int ii, status;
  unsigned int nFrames;

  VTPflag = 0;
  CDODISABLE(VTP, 1, 0);

  vtpStats(0);

  /* Disconnect Streaming sockets */
  //  for(inst=0;inst<NUM_VTP_CONNECTIONS;inst++) {
  //  vtpStreamingTcpConnect(inst,0);
  //}

  sleep(2);  /* wit a bit to make sure the VTP sends all its data */

  /* Send an End Event */
  /*Send End Event to instance 0*/
  nFrames = vtpStreamingFramesSent(0);
  vtpStreamingEvioWriteControl(0,EV_END,rol->runNumber,nFrames);

  sleep(2);

  /* Disable Streaming EB - careful. If the User Sync is not high this can drop packets from a frame using UDP */
  vtpStreamingEbReset();


  /* Disconnect the Socket - Client Mode TCP connections */
  for(ii=0;ii<NUM_VTP_CONNECTIONS;ii++) {
    status = vtpStreamingTcpConnect(ii,0,0,0);
    if(status == ERROR) {
      printf("rocEnd: Error closing socket on link %d\n",ii);
    }
  }
  

  /* Reset all Socket Connections on the TCP Server - Server Mode only*/
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

