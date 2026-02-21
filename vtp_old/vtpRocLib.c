/* Routines associated with the CODA ROC firmware relsease for the VTP */


/* VTP ROC Data FIFOs - both Synchronous and Asynchrous - have a max depth
    One can write more than this into the FIFOs as long we keep track of how full
    The fifos are before writing data. */
#define VTP_ROC_DATA_FIFO_DEPTH    512   // total 32 bit words
#define VTP_ROC_DATA_FIFO_FULL     0x80000000


/* Simple Acknowledge of Trigger when CPU Synchonous Events are enabled */
#define VTP_ROC_ACK   vtp->roc.CpuSyncEventLen = 0



int
vtpRocStatus(int flag)
{

  int  ii, jj, status, fw_version, fw_type, timestamp, tcpFull=0;
  unsigned int ctrl, tcp_ctrl, tcp_state, tcp_status, ti[4], rocid, roc[12], totalBytes[2],
    tiTrigCnt, tiTrigAck, eb_ctrl, eb_status,  ebiotx[2], ebiorx[2], evioBank[3], 
    port[16], slot[16], ppState[16];

  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  VLOCK;
  ctrl       = vtp->clk.Ctrl;
  status     = vtp->clk.Status;
  fw_version = vtp->clk.FW_Version;
  fw_type    = vtp->clk.FW_Type;
  timestamp  = vtp->clk.FW_Timestamp;

  tcp_ctrl   = vtp->tcpClient[0].Ctrl;
  tcp_state  = vtp->tcpClient[0].IP4_TCPStateStatus;
  tcp_status = vtp->tcpClient[0].IP4_TCPStatus;

  ti[0]      = vtp->tiLink.Ctrl;
  ti[1]      = vtp->tiLink.LinkStatus;
  ti[2]      = vtp->tiLink.Status;
  ti[3]      = vtp->tiLink.EBStatus;

  ebiotx[0] =  vtp->v7.ebioTx[0].Ctrl; 
  ebiotx[1] =  vtp->v7.ebioTx[0].Status;
  ebiorx[0] =  vtp->ebiorx[0].Ctrl;
  ebiorx[1] =  vtp->ebiorx[0].Status;

  eb_ctrl      = vtp->v7.rocEB.Ctrl;
  eb_status    = vtp->v7.rocEB.Status;
  evioBank[0]  = vtp->v7.rocEB.evio_cfg[0];
  evioBank[1]  = vtp->v7.rocEB.evio_cfg[1];
  evioBank[2]  = vtp->v7.rocEB.evio_cfg[2];
  for(ii=0;ii<16;ii++) {
    port[ii]    = (vtp->v7.rocEB.pp_cfg[ii]);
    ppState[ii] = vtp->v7.rocEB.pp_state[ii];
  }

  rocid         = vtp->roc.rocID;
  tiTrigCnt     = vtp->roc.TiTriggerCnt;
  tiTrigAck     = vtp->roc.TiTriggerAck;
  totalBytes[0] = vtp->roc.BytesSent[0];
  totalBytes[1] = vtp->roc.BytesSent[1];
  roc[0]        = vtp->roc.Ctrl;
  roc[1]        = vtp->roc.State;
  tcpFull = (roc[1])&VTP_ROC_STATE_TCPFULL;
  roc[2]        = vtp->roc.CpuSyncEventStatus;
  roc[3]        = vtp->roc.CpuSyncEventLenStatus;
  roc[4]        = vtp->roc.CpuAsyncEventStatus;
  roc[5]        = vtp->roc.CpuAsyncEventLenStatus;
  roc[6]        = (vtp->roc.MaxRecordSize)<<2;     /* Convert to bytes */
  roc[7]        = vtp->roc.MaxBlocks;
  roc[8]        = vtp->roc.RecordTimeout;

  VUNLOCK;

  /* Calculate total number of modules/port */
  for(ii=0;ii<16;ii++) {
    slot[ii]=0;
    for(jj=0;jj<4;jj++) {
      if((port[ii])&(3<<(jj*8))) {
	slot[ii] += 1;
	//	printf("debug: port[%d]=0x%08x, slot[%d] = %d\n",(ii+1),port[ii],(ii+1),slot[jj]);
      }
    }
  }



  printf("---------------------------------------\n");
  printf("--VTP (Z7) Statistics                --\n");
  printf("---------------------------------------\n");
  printf("Clock:\n");
  printf("  Global PLL locked: %d\n", (status>>0) & 0x1);
  printf("\n");
  printf("Control Reg: %08x\n", ctrl);
  printf("\n");
  printf("Firmware:\n");
  printf("  Type: %2d\n", fw_type);
  printf("  Version: %d.%d\n", (fw_version>>16) & 0xFFFF, (fw_version>>0) & 0xFFFF);
  printf("  Timestamp: 0x%08X : %d/%d/%d %d:%d:%d \n", timestamp,
	 ((timestamp>>17)&0x3f)+2000, ((timestamp>>23)&0xf), ((timestamp>>27)&0x1f),
	 ((timestamp>>12)&0x1f), ((timestamp>>6)&0x3f), ((timestamp>>0)&0x3f) );
  printf("\n\n");
  
  printf("TCP LINK Status:\n");
  printf("    Ctrl            = %08x\n",tcp_ctrl);
  printf("    State           = %08x\n",tcp_state);
  if(tcp_status)
    printf("    Status          = %08x  (Connected)\n",tcp_status);
  else
    printf("    Status          = %08x  (No connection)\n",tcp_status);

  printf("\nEBIO (EB->ROC) Link:\n");
  printf("  TX Control : Status: 0x%08X | 0x%08X\n", ebiotx[0],ebiotx[1]);
  printf("  RX Control : Status: 0x%08X | 0x%08X\n", ebiorx[0],ebiorx[1]);
  
  printf("\n");
  printf("VTP ROC Status (ID = %d):\n",rocid);
  if((ti[0]&VTP_TI_CTRL_MODE)) 
    printf("    TI Link  Ctrl   = %08x  (Hardware Mode)\n",ti[0]);
  else
    printf("    TI Link  Ctrl   = %08x  (Software Sync Mode)\n",ti[0]);
  printf("    TI Link  Status = %08x\n",ti[1]);
  printf("    TI Status       = %08x\n",ti[2]);
  printf("    TI (EB Status)  = %08x\n",ti[3]);
  printf("    Trigger Cnt  = %d\n",tiTrigCnt);
  printf("    Trigger Acks = %d\n",tiTrigAck);
  printf("    Bytes Sent   = 0x%08x%08x\n",totalBytes[1],totalBytes[0]);
  printf("    EVIO Record: Size = %d Bytes, Max Event Blocks = %d, Timeout = %d (12.5 ns ticks)\n",roc[6],roc[7],roc[8]); 
  printf("\n");

  printf("    ROC_EB   Ctrl   = %08x\n",eb_ctrl);
  printf("    ROC_EB   Status = %08x\n",eb_status);
  printf("    ROC_EB   Bank0  = %08x\n",evioBank[0]);
  printf("    ROC_EB   Bank1  = %08x\n",evioBank[1]);
  printf("    ROC_EB   Bank2  = %08x\n",evioBank[2]);
  printf("    ROC_EB   P_PORT = ");
  for(ii=0;ii<16;ii++) {
    printf(" %d ",slot[ii]);
  }
  printf("\n    ROC_EB   STATE  = ");
  for(ii=0;ii<8;ii++) {
    printf(" 0x%08x ",ppState[ii]);
  }
  printf("\n                      ");
  for(ii=8;ii<16;ii++) {
    printf(" 0x%08x ",ppState[ii]);
  }
  printf("\n\n");

  printf("    ROC             Ctrl   = %08x\n",roc[0]);
  if(tcpFull) {
    printf("    ROC             State  = %d  %02x %02x %02x %02x  (TCPFULL)\n",
	 (roc[1]&0x10000)>>16,
	 (roc[1]&0xF000)>>12,
	 (roc[1]&0xF00)>>8,
	 (roc[1]&0xF0)>>4,
	 (roc[1]&0xF));
  }else{
    printf("    ROC             State  = %d  %02x %02x %02x %02x\n",
	 (roc[1]&0x10000)>>16,
	 (roc[1]&0xF000)>>12,
	 (roc[1]&0xF00)>>8,
	 (roc[1]&0xF0)>>4,
	 (roc[1]&0xF));
  }
  printf("    ROC SyncEvt     Status = %08x\n",roc[2]);
  printf("    ROC SynEvtLen   Status = %08x\n",roc[3]);
  printf("    ROC AsyncEvt    Status = %08x\n",roc[4]);
  printf("    ROC AsyncEvtLen Status = %08x\n",roc[5]);
  
  printf("\n");

  return OK;
}


/* Configure the ROC ID and buffering parameters 
       
     roc_id        : valid IDs are numbers from 0-255
     max_rec_size  : Record buffer size in bytes. 
                     0 will use default  1048351 (32bit words or ~4MB)
     max_blocks    : max number of blocks allowed before sending a Record (1-128)
                     0 will use default  64
     rec_timeout   : clock timeout before sending a record in seconds (1-25)
                     0 will use default 1   
*/

#define VTP_ROC_TICKS_PER_SEC   78125000    // 12.5ns per tick

int
vtpRocConfig(int roc_id, int max_rec_size, int max_blocks, int rec_timeout)
{
  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  VLOCK;

  vtp->roc.rocID = roc_id;

  if(max_rec_size <= 0) {
    vtp->roc.MaxRecordSize = 1048351;       /* Size in 32 bit words */
  }else{
    vtp->roc.MaxRecordSize = max_rec_size;
  }

  if(max_blocks <= 0) {
    vtp->roc.MaxBlocks = 64;
  }else{
    if(max_blocks > 128)
      vtp->roc.MaxBlocks = 128;
    else
      vtp->roc.MaxBlocks = max_blocks;
  }

  if(rec_timeout <= 0) {
    vtp->roc.RecordTimeout = VTP_ROC_TICKS_PER_SEC;
  }else{
    if(rec_timeout > 25)
      vtp->roc.RecordTimeout = (VTP_ROC_TICKS_PER_SEC*25);
    else
      vtp->roc.RecordTimeout = (VTP_ROC_TICKS_PER_SEC*rec_timeout);
  }

  VUNLOCK;

  return OK;
}


int
vtpRocReset(int en_mask)
{
  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  VLOCK;
  vtp->roc.Ctrl = 1; /* Enable Reset */

  if(en_mask)
    vtp->roc.Ctrl = (en_mask&0x700);
  else
    vtp->roc.Ctrl = 0;

  VUNLOCK;

  return OK;
}

int
vtpRocTCPInit(int mask, int ip0, int ip1, int ip2, int ip3, int dst_port)
{

  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  VLOCK;
  vtp->clk.Ctrl = 0x7;
  vtp->clk.Ctrl = 0x6;
  vtp->clk.Ctrl = 0x0;
  usleep(10000);


  vtp->tcpClient[0].Ctrl = 0x03C5;
  vtp->tcpClient[0].IP4_StateRequest = 0;

  /* Program network parameters */
  vtp->tcpClient[0].IP4_Addr = (129<<24) | (57<<16) | (109<<8) | (28<<0);
  vtp->tcpClient[0].IP4_SubnetMask = 0xFFFFFF00;
  vtp->tcpClient[0].IP4_GatewayAddr = (129<<24) | (57<<16) | (109<<8) | (1<<0);
  vtp->tcpClient[0].MAC_ADDR[1] = 0x0000CEBA;
  vtp->tcpClient[0].MAC_ADDR[0] = 0xF00300DA;
  vtp->tcpClient[0].TCP_DEST_ADDR[1] = (ip0<<24) | (ip1<<16) | (ip2<<8) | (ip3<<0);
  vtp->tcpClient[0].TCP_PORT[0] = (10001<<16) | (dst_port<<0);

  vtp->tcpClient[0].Ctrl = 0x03C5;
  vtp->tcpClient[0].Ctrl = 0x03C0;
  usleep(10000);
  vtp->tcpClient[0].Ctrl = 0x13C0;


  sleep(1);

  /* Connect to Server */
  vtp->tcpClient[0].IP4_StateRequest = 2;

  VUNLOCK;

  return OK;
}

int
vtpRocEnd()
{
  CHECKINIT;
  //CHECKTYPE(VTP_FW_TYPE_VCODAROC,0);

  VLOCK;
  vtp->tcpClient[0].IP4_StateRequest = 0;
  VUNLOCK;

  return OK;
}

int
vtpRocSetID(int roc_id)
{
  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  VLOCK;

  vtp->roc.rocID = roc_id;

  VUNLOCK;

  return roc_id;
}


/* Routine to Poll the trigger status for the VTP ROC.  This should be used when
   the User has enabled Synchronous CPU events in the ROC Ctrl Register. It returns
   the total number of readout triggers in the queue. This should be no more than
   the buffer level set by the TS/TI Master */

int
vtpRocPoll()
{
  int ntrig = 0;

  CHECKINIT;

  /* return the number of outstanding triggers */
  ntrig = ((vtp->roc.CpuSyncEventLenStatus)&0x2ff0000)>>16;

  return ntrig;
}


/* Rotuine to Write a Data Bank to the ROC Synchonous Event Fifo. This Bank MUST
   be properly formated EVIO Bank with the correct length or it will hang the ROC. */
void
vtpRocWriteBank(unsigned int *bank)
{
  int ii, blen=0;

  VLOCK;
  if(bank == NULL) {  /* Just write 0 to the Len fifo to Acknowledge the trigger */
    vtp->roc.CpuSyncEventLen = 0;
  }else{
    blen = bank[0] + 1;   /* get the total bank length in words */
    for(ii=0;ii<blen;ii++) {                 // Write data into FIFO
	vtp->roc.CpuSyncEventData = bank[ii];
      }
      vtp->roc.CpuSyncEventLen = blen;       // Write the Length to acknowledge
  }

  VUNLOCK;

  return;
}



/* Return the TI trigger count */
unsigned int
vtpRocGetTriggerCnt()
{

  if(vtp==NULL)
    return(0);

  /* for some reason vtp!=NULL after a library load and unload so this CHECKTYPE
     generates an error message we do not want to care about */
  //  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  return(vtp->roc.TiTriggerCnt);

}

/* Return the number of 32bit ints sent. This is a 64 bit counter */
unsigned long long
vtpRocGetNlongs()
{
  unsigned int bytes[2];
  unsigned long long total;

  if(vtp==NULL)
    return(0);

  //CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  VLOCK;
  bytes[0] = vtp->roc.BytesSent[0];
  bytes[1] = vtp->roc.BytesSent[1];
  VUNLOCK;

  total =  (bytes[1])&0x00000000FFFFFFFF;
  total = (total<<(32ll))&0xFFFFFFFF00000000;
  total += bytes[0];

  /* convert to longs */
  total = total>>2;

  return(total);
}

/* Return the number of Bytes sent. Pass a pointer to a 64 bit counter */
void
vtpRocGetNBytes(unsigned long long *nbytes)
{
  unsigned int bytes[2];
  unsigned long long total;

  if(vtp==NULL)
    return;

  //CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  VLOCK;
  bytes[0] = vtp->roc.BytesSent[0];
  bytes[1] = vtp->roc.BytesSent[1];
  VUNLOCK;

  total =  (bytes[1])&0x00000000FFFFFFFF;
  total = (total<<(32ll))&0xFFFFFFFF00000000;
  total += bytes[0];

  /* write value to the memory location */
  *nbytes = total;

  return;
}




/* vtpRocEnable defines available data sources for the ROC
   to collect data from to send in an event. These include
   Vertex 7 payload slot EB                  (bit 8)
   Synchronous Data from the software ROC    (bit 9)
   Asynchonous Data from the software ROC    (bit 10)
*/
int
vtpRocEnable(int en_mask)
{
  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  VLOCK;

  vtp->roc.Ctrl = ((en_mask&7)<<8);

  VUNLOCK;


  return OK;
}



int
vtpRocGetCfg(int *roc_id, int *en_mask)
{
  uint32_t val;
  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  VLOCK;

  val = vtp->roc.rocID;
  *roc_id    = (val & 0xFF);
  val = vtp->roc.Ctrl;
  *en_mask = (val>>8)&7;

  VUNLOCK;

  return OK;
}

int
vtpRocSetTcpCfg(
    unsigned char ipaddr[4],
    unsigned char subnet[4],
    unsigned char gateway[4],
    unsigned char mac[6],
    unsigned int destipaddr,
    unsigned int destipport
  )
{
  int inst=0, link=0;
  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  /* Check for valid port range (16 bits) */
  if(destipport > 0xffff) {
    printf("%s: ERROR: Destination Port out of range (%d)\n",__func__,destipport);
    return ERROR;
  }

  VLOCK;
  vtp->tcpClient[inst].IP4_StateRequest = 0;
  vtp->tcpClient[inst].IP4_Addr         = (    ipaddr[0]<<24) | (    ipaddr[1]<<16) | (    ipaddr[2]<<8) | (    ipaddr[3]<<0);
  vtp->tcpClient[inst].IP4_SubnetMask   = (    subnet[0]<<24) | (    subnet[1]<<16) | (    subnet[2]<<8) | (    subnet[3]<<0);
  vtp->tcpClient[inst].IP4_GatewayAddr  = (   gateway[0]<<24) | (   gateway[1]<<16) | (   gateway[2]<<8) | (   gateway[3]<<0);
  vtp->tcpClient[inst].MAC_ADDR[1]      =                                             (       mac[0]<<8) | (       mac[1]<<0);
  vtp->tcpClient[inst].MAC_ADDR[0]      = (       mac[2]<<24) | (       mac[3]<<16) | (       mac[4]<<8) | (       mac[5]<<0);

  vtp->tcpClient[inst].TCP_DEST_ADDR[link] = destipaddr;
  printf("%s: TCP_DEST_ADDR = 0x%08X (link=%d)\n", __func__, vtp->tcpClient[inst].TCP_DEST_ADDR[link], link);
  vtp->tcpClient[inst].TCP_PORT[link]      = (10001<<16) | destipport;
  VUNLOCK;

  return OK;
}

int
vtpRocSetTcpCfg2(int inst, unsigned int destipaddr, unsigned int destipport)
{
  int link=0; /* Only one link per network interface */
  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  /* Check for valid port range (16 bits) */
  if(destipport > 0xffff) {
    printf("%s: ERROR: Destination Port out of range (%d)\n",__func__,destipport);
    return ERROR;
  }

  VLOCK;
  vtp->tcpClient[inst].IP4_StateRequest = 0;
  vtp->tcpClient[inst].IP4_Addr         = VTP_NET_OUT.ip[inst];
  vtp->tcpClient[inst].IP4_SubnetMask   = VTP_NET_OUT.sm[inst];
  vtp->tcpClient[inst].IP4_GatewayAddr  = VTP_NET_OUT.gw[inst];
  vtp->tcpClient[inst].MAC_ADDR[1]      = VTP_NET_OUT.mac[0][inst];
  vtp->tcpClient[inst].MAC_ADDR[0]      = VTP_NET_OUT.mac[1][inst];
  vtp->tcpClient[inst].TCP_DEST_ADDR[link] = destipaddr;
  printf("%s: TCP_DEST_ADDR = 0x%08X (link=%d)\n", __func__, vtp->tcpClient[inst].TCP_DEST_ADDR[link], link);
  vtp->tcpClient[inst].TCP_PORT[link]      = (10001<<16) | destipport;
  VUNLOCK;

  return OK;
}


int
vtpRocGetTcpCfg(
    unsigned char ipaddr[4],
    unsigned char subnet[4],
    unsigned char gateway[4],
    unsigned char mac[6],
    unsigned char destipaddr[4],
    unsigned short *destipport
  )
{
  unsigned int val, inst=0, link=0;
  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);


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

  val = vtp->tcpClient[inst].TCP_DEST_ADDR[link];
  destipaddr[0] = ((val>>24)&0xFF); destipaddr[1] = ((val>>16)&0xFF); destipaddr[2] = ((val>>8)&0xFF); destipaddr[3] = ((val>>0)&0xFF);

  printf("%s: TCP_DEST_ADDR = 0x%08X (link=%d)\n", __func__, vtp->tcpClient[inst].TCP_DEST_ADDR[link],link);

  val = vtp->tcpClient[inst].TCP_PORT[link];
  *destipport = ((val>>0)&0xFFFF);
  VUNLOCK;

  return OK;
}



/* Convert a standard IP4 address string (eg "129.57.29.1") into an unsigned integer
   that can be used for the VTP TCP stack registers */
unsigned int
vtpRoc_inet_addr(const char *ip4)
{
  unsigned int addr=0;

  addr = inet_addr(ip4);
  if (addr == 0xffffffff) {
    printf("%s: ERROR: No valid IP4 address provided (%s) \n",__func__,ip4);
    return 0;
  }

  /* must swap for proper VTP endianess */
  addr = LSWAP(addr);

  return addr;
}

/* Convert a standard MAC address string (eg ce:ba:f0:03:12:34) into 2 unsigned integers
   that can be used for the VTP TCP stack registers */
int
vtpRoc_mac_addr(char *mac_str, unsigned int mm[])
{ 
  int ii;
  unsigned int mac[6] = {0,0,0,0,0,0};
  char *hx;

  /* This functions assumes the form  XX:XX:XX:XX:XX:XX */
  
  if(mac_str != NULL) {
    /* get first byte */
    hx = strtok(mac_str,":");
    mac[0] = strtol(hx, (char **)NULL, 16);
    /* and then the rest... */
    for (ii=1;ii<6;ii++) {
      hx = strtok(NULL,":");
      mac[ii] = strtol(hx, (char **)NULL, 16);
    }
    printf("mac[] = %02x %02x %02x %02x %02x %02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  }else{
    printf("%s: ERROR: NULL string passed to function\n",__func__);
    return ERROR;
  }

  /* create the 2 VTP register entries */
  mm[0] = (mac[0])<<8 | mac[1];
  mm[1] = (mac[2])<<24 | (mac[3]<<16) | (mac[4])<<8 | mac[5];

  return OK;
}



int
vtpRocEvioWriteControl(unsigned int type, unsigned int val0, unsigned int val1)
{

  int full;
  unsigned int rocid=0;

  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  /* Check that the fifo does not already have data in it */
  full =  (vtp->roc.CpuAsyncEventStatus&0x2ff); /* how many words currently in fifo? */

  if(full>0) {
    printf("%s: ERROR: Data in fifo (%d words). Cannot send Control event\n",__func__,full);
    return ERROR;
  }

  VLOCK;

  rocid = vtp->roc.rocID;

  /* cMsg Header */
  if(type == 0xffd4) // End Event
    vtp->roc.CpuAsyncEventData = 3;
  else
    vtp->roc.CpuAsyncEventData = 1;
  vtp->roc.CpuAsyncEventData = (13<<2);

  /* EVIO Block header */
  vtp->roc.CpuAsyncEventData = 13;
  vtp->roc.CpuAsyncEventData = 0xffffffff;
  vtp->roc.CpuAsyncEventData = 8;
  vtp->roc.CpuAsyncEventData = 1;
  vtp->roc.CpuAsyncEventData = rocid;
  vtp->roc.CpuAsyncEventData = (0x1400|0x200|4); /* Control Event, Last block, evio version */  
  vtp->roc.CpuAsyncEventData = 0;
  vtp->roc.CpuAsyncEventData = 0xc0da0100;

  /* CODA Control Event */
  vtp->roc.CpuAsyncEventData = 4;
  vtp->roc.CpuAsyncEventData = ((type<<16)|(1<<8)|(0));
  vtp->roc.CpuAsyncEventData = 1200;
  vtp->roc.CpuAsyncEventData = val0;
  vtp->roc.CpuAsyncEventData = val1;

  vtp->roc.CpuAsyncEventLen = 15;
  VUNLOCK;

  return OK;
}


int
vtpRocEvioWriteUserEvent(unsigned int *buf)
{

  int ii, jj, full, flow_cntl = 0;
  unsigned int blen, avail, rem, tag, dt, num, totalLen;
  unsigned int rocid=0;
  static unsigned int maxwds = 1024*1024;

  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  /* First check that the User's buffer contains EVIO Bank data */
  blen = buf[0] + 1;  // total length of buffer 
  tag = (buf[1]&0xffff0000)>>16;
  dt  = (buf[1]&0xff00)>>8;
  num =  buf[1]&0xff;

  if(blen>maxwds) {  /* 4MB is the Max EVIO ROC record size */
    printf("%s: ERROR: buffer length (%d words) is too long for a User Event\n",__func__,blen); 
    return ERROR;
  }
  if(tag>=0xff00) {
    printf("%s: ERROR: Illegal User Bank Tag (0x%04x)\n",__func__,tag);
    return ERROR;
  }
  if(((dt&0x3f)>0x10)&&((dt&0x3f)!=0x20)) {
    printf("%s: ERROR: Illegal Bank Data Type (%d) for User Event\n",__func__,dt);
    return ERROR;
  }
  if(num>0) {
    printf("%s: ERROR: Num field must be zero for User Events (num=%d)\n",__func__,num);
    return ERROR;
  }

  VLOCK;

  totalLen = blen + 8; 
  if(((totalLen+2) > VTP_ROC_DATA_FIFO_DEPTH)) {
    flow_cntl = 1;  /* We need to be careful overfilling the FIFO */
    full =  (vtp->roc.CpuAsyncEventStatus&0x2ff); /* how many words currently in fifo? */
    if (full > (VTP_ROC_DATA_FIFO_DEPTH-12)) {
      printf("%s: ERROR: DATA FIFO too Full (%d words) - try again later\n",__func__,full);
      VUNLOCK;
      return ERROR;
    }else{
      printf("%s: WARN: Flow Control Enabled (current fifo level = %d)\n",__func__,full);
    }
  }
  rocid = vtp->roc.rocID;  // get rocid for EVIO header


  /* Write the total length to the Length FiFo so that the VTP
     knows how much data is coming and starts reading into its send buffer*/
  vtp->roc.CpuAsyncEventLen = totalLen + 2;
  rem = totalLen+2;


  /* cMsg Header */
  vtp->roc.CpuAsyncEventData = 1;
  vtp->roc.CpuAsyncEventData = (totalLen<<2);  // Length in Bytes

  /* EVIO Block header */
  vtp->roc.CpuAsyncEventData = totalLen;
  vtp->roc.CpuAsyncEventData = 0xffffffff;
  vtp->roc.CpuAsyncEventData = 8;
  vtp->roc.CpuAsyncEventData = 1;
  vtp->roc.CpuAsyncEventData = rocid;
  vtp->roc.CpuAsyncEventData = (0x1000|0x200|4); /* User Event, Last block, evio version */  
  vtp->roc.CpuAsyncEventData = 0;
  vtp->roc.CpuAsyncEventData = 0xc0da0100;

  rem = rem - 10;

  /* Write User Event buffer */
  if(flow_cntl) {
    vtp->roc.CpuAsyncEventData = (blen - 1);
    jj=1; rem = rem-1;
    do {
      if(vtp->roc.CpuAsyncEventStatus&VTP_ROC_DATA_FIFO_FULL) {
	printf("%s: ERROR: ASYNC DATA FIFO is FULL!!\n",__func__);
	VUNLOCK;
	return ERROR;
      }else{
	avail = VTP_ROC_DATA_FIFO_DEPTH - (vtp->roc.CpuAsyncEventStatus&0x2ff);
	if(avail>rem) avail=rem;
      }
      for(ii=0;ii<avail;ii++) {
	vtp->roc.CpuAsyncEventData = buf[jj+ii];
      }
      rem = rem - avail;
      jj += avail;
      
    }while (rem > 0);

  }else{ /* just blast it in */
    vtp->roc.CpuAsyncEventData = (blen - 1);
    for(ii=1;ii<blen;ii++) {
      vtp->roc.CpuAsyncEventData = buf[ii];
    }
  }
  
  VUNLOCK;

  return OK;
}



/* Routine to read in a file (as text) and create a User Event in a buffer that
   can be sent by the VTP into the Data Stream using the function vtpRocEvioWriteUserEvent() */

int
vtpRocFile2Event(const char *fname, unsigned char *buf, int utag, int rocid, int maxbytes)
{

  int ii, c, ilen, rem;
  FILE *fid;
  int maxb = 1024*1024*4; /* max VTP output buffer size */
  unsigned int ev_header[4];
  

  if (fname == NULL) {
    printf("%s: ERROR: No filename was specified\n",__func__);
    return ERROR;
  }  


  if((utag == 0)||(utag>=0xff00)) {
    printf("%s: ERROR: Invalid User Event tag specified (0x%04x)\n",__func__,utag);
    return ERROR;
  }

  if((maxbytes==0) || (maxbytes>maxb))
    maxbytes = maxb;  /* Default to 4 MB */


  fid = fopen(fname,"r");
  if(fid==NULL) {
    printf("%s: ERROR: The file %s does not exist \n",__func__,fname);
    return ERROR;
  }else{
    printf("%s: INFO: Opened file %s for reading into User Event \n",__func__,fname);
  }
  /* Read in the file to the buffer */
  ilen = 16; /* leave the header words - 4 words * 4 bytes each*/
  while((ilen<maxbytes) && ((c = getc(fid))!=EOF)) {
    buf[ilen++] = c;
  }
  fclose(fid);
  printf("%s: INFO: Read %d bytes into buffer\n",__func__,ilen);

  /* Make sure we null terminate the buffer and pad it out to an integal number of words */
  rem = 4 - (ilen&0x3);
  if(rem<4) {
    for(ii=0;ii<rem;ii++)
      buf[(ilen+ii)] = 0;
    ilen+=ii;
  }else{
    rem=0;  /* no padding necessary just NULL terminate*/
    buf[ilen] = 0;
  }

  /* Write the header info in the buffer */
  ev_header[0] = (ilen>>2) - 1; /* divide by 4  and subtract 1 */
  ev_header[1] = (rocid<<16)|(0x10<<8)|0; 
  ev_header[2] = (ilen>>2) - 3;
  ev_header[3] = (utag<<16)|(rem<<14)|(3<<8)|0;
  memcpy(&buf[0],&ev_header[0],16);
  //  printf("%s: INFO: Copied Event Header = 0x%08x 0x%08x \n",__func__,ev_header[0],ev_header[1], ev_header[2], ev_header[3]);

  /*Swap the bytes going to the Async Event 32 bit Fifo */
  {
    unsigned char aa, bb;
    for (ii=16; ii<=(ev_header[0]<<2); ii+=4) {
      aa=buf[ii]; bb=buf[ii+1];
      buf[ii] = buf[ii+3];
      buf[ii+1] = buf[ii+2];
      buf[ii+2] = bb;
      buf[ii+3] = aa;
    }
  }

  
  return (ev_header[0]);

}




int
vtpRocEbReset()
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_VCODAROC,0);

  VLOCK;
  vtp->v7.rocEB.Ctrl = 1;
  //  vtp->v7.rocEB.Ctrl = 0;
  VUNLOCK;

  return OK;
}

int
vtpRocEbStart()
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_VCODAROC,0);

  VLOCK;
  vtp->v7.rocEB.Ctrl = 2;
  VUNLOCK;

  return OK;
}

int
vtpRocEbStop()
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_VCODAROC,0);

  VLOCK;
  vtp->v7.rocEB.Ctrl = 1;   /* Set Reset bit but do not clear it */
  VUNLOCK;

  return OK;
}


/* Initialize the ROC Eventbuilder Bank Tags and Block Level and clear all the 
   payload port config registers */

int
vtpRocEbInit(unsigned int bank0, unsigned int bank1, unsigned int bank2)
{
  int ii;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_VCODAROC,0);

  /* Check that EB is stopped (reset bit enabled) */
  if(((vtp->v7.rocEB.Ctrl)&1) == 0) {
    printf("%s: ERROR: EB is enabled (call vtpRocEbStop() first)\n", __func__);
    return ERROR;
  }


  VLOCK;
  /* If Bank tag is 0 then define a default Bank0 = 1, Bank1 = 2  and Bank2 = 3*/

  if(bank0 != 0) 
    vtp->v7.rocEB.evio_cfg[0] = 0x010000 | (bank0&0xffff);
  else
    vtp->v7.rocEB.evio_cfg[0] = 0x010001;

  if(bank1 != 0) 
    vtp->v7.rocEB.evio_cfg[1] = 0x010000 | (bank1&0xffff);
  else
    vtp->v7.rocEB.evio_cfg[1] = 0x010002;

  if(bank2 != 0) 
    vtp->v7.rocEB.evio_cfg[2] = 0x010000 | (bank2&0xffff);
  else
    vtp->v7.rocEB.evio_cfg[2] = 0x010003;


  /* Clear Slot configuration registers */
  for(ii=0;ii<16;ii++) {
      vtp->v7.rocEB.pp_cfg[ii] = 0;
  }

  VUNLOCK;

  return OK;
}


 /* Readout List build config routine. Pass the Payload Port Config array to fill in the
    payload port config registers */
int
vtpRocEbConfig(PP_CONF *ppInfo, int blocklevel)
{
  int ii, ppmask = 0;
  unsigned int reg;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_VCODAROC,0);

  /* Check that EB is stopped (reset bit enabled) */
  if(((vtp->v7.rocEB.Ctrl)&1) == 0) {
    printf("%s: ERROR: EB is enabled (call vtpRocEbStop() first)\n", __func__);
    return ERROR;
  }


  VLOCK;
  /* loop through all the ppInfo elements */
  for(ii=0;ii<16;ii++) {
    if(ppInfo[ii].module_id) {
      vtp->v7.rocEB.pp_cfg[ii] = ppInfo[ii].bankInfo;
      ppmask |= (1<<ii);
    }
  }
  VUNLOCK;


  /* Make sure the VTP serdes link mask is setup correctly as well */
  vtpEnableTriggerPayloadMask(ppmask);


  /* If Block level is > 0 then set that info as well for each Bank Config register
     but make sure we save the Bank tag info */
  VLOCK;
  if((blocklevel>0)&&(blocklevel<255)) {
    reg =  (vtp->v7.rocEB.evio_cfg[0])&0x0000ffff;
    vtp->v7.rocEB.evio_cfg[0] = (blocklevel<<16)|reg;
    reg =  (vtp->v7.rocEB.evio_cfg[1])&0x0000ffff;
    vtp->v7.rocEB.evio_cfg[1] = (blocklevel<<16)|reg;
    reg =  (vtp->v7.rocEB.evio_cfg[2])&0x0000ffff;
    vtp->v7.rocEB.evio_cfg[2] = (blocklevel<<16)|reg;
  }

  VUNLOCK;

  return ppmask;
}

/* Set the Block level for the ROC EventBuilder */
int 
vtpRocEbSetBlockLevel(int blocklevel)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_VCODAROC,0);

  unsigned int reg=0;

  /* If Block level is > 0 then set that info in each Config register*/
  if((blocklevel>0)&&(blocklevel<255)) {
    reg =  (vtp->v7.rocEB.evio_cfg[0])&0x0000ffff;
    vtp->v7.rocEB.evio_cfg[0] = (blocklevel<<16)|reg;
    reg =  (vtp->v7.rocEB.evio_cfg[1])&0x0000ffff;
    vtp->v7.rocEB.evio_cfg[1] = (blocklevel<<16)|reg;
    reg =  (vtp->v7.rocEB.evio_cfg[2])&0x0000ffff;
    vtp->v7.rocEB.evio_cfg[2] = (blocklevel<<16)|reg;   
  }else{
    printf("%s: ERROR: Invalid block level (%d)\n",__func__,blocklevel);
    return ERROR;
  }


  return OK;
}




int
vtpRocMigReset()
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_VCODAROC,0);

  printf("%s()\n", __func__);

  // V7 Mig
  VLOCK;
  vtp->v7.mig[0].Ctrl = 0x1;         // Assert SYS_RST
  usleep(10000);
  vtp->v7.mig[0].Ctrl = 0x2;         // Assert FIFO_RST
  usleep(10000);
  vtp->v7.mig[0].Ctrl = 0x0;         // Release all Resets   
  usleep(10000);
  VUNLOCK;

  return OK;
}


int
vtpRocQsfpReset(int inst, int reset)
{
  int val;
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_VCODAROC,0);

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


#define TCP_SKIP_EN   0x0

int
vtpRocSkipTcp(int inst, int skip)
{
  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_VCODAROC,0);

  if(inst<0 || inst>1)
  {
    printf("%s: ERROR inst=%d invalid.\n", __func__, inst);
    return ERROR;
  }

  VLOCK;
  if(skip) vtp->ebiorx[inst].Ctrl |= 0x00000200;
  else     vtp->ebiorx[inst].Ctrl &= 0xFFFFFDFF;
  VUNLOCK;

  return OK;
}


int
vtpRocEbioReset()
{
  CHECKINIT;

  int j;

  vtp->v7.ebioTx[0].Ctrl = 0x7; // Assert: RESET, TRAINING, FIFO_RESET
  vtp->v7.ebioTx[0].Ctrl = 0x6; // Release RESET, ebioTx: TRAINING,FIFO_RESET

  vtp->ebiorx[0].Ctrl = 0xBA | TCP_SKIP_EN;
  usleep(10000);
  vtp->ebiorx[0].Ctrl = 0xB8 | TCP_SKIP_EN;
  usleep(10000);
    
  for(j=0;j<10;j++)
    {
      vtp->ebiorx[0].Ctrl = 0xBA | TCP_SKIP_EN;
      vtp->ebiorx[0].Ctrl = 0xB8 | TCP_SKIP_EN;
      usleep(1000);
      if(!(vtp->ebiorx[0].Status & 0xFFFF0000))
        break;
      vtp->ebiorx[0].Ctrl = 0xBE | TCP_SKIP_EN;
    }
  vtp->ebiorx[0].Ctrl = 0xB8 | TCP_SKIP_EN;
  usleep(1000);
  vtp->ebiorx[0].Ctrl = 0x38 | TCP_SKIP_EN;


  if(j != 10) printf("EBIORX(0)     sync'd\n");
  else        printf("EBIORX(0) NOT sync'd\n");

    // V7 evioTx reset release
  vtp->v7.ebioTx[0].Ctrl = 0x4;

    // Release resets downstream->upstream
#if 1
    vtp->ebiorx[0].Ctrl = 0x78 | TCP_SKIP_EN;
#endif
    vtp->v7.ebioTx[0].Ctrl = 0x0;
    vtp->v7.ebioTx[0].Ctrl = 0x8;

  return OK;
}



int
vtpRocTcpConnect(int connect, unsigned int *cdata, int dlen)
{
  int jj, inst=0;


  CHECKINIT;
  CHECKTYPE(VTP_FW_TYPE_VCODAROC,0);

  //  printf("%s(%d,cdata,%d)\n", __func__, connect, dlen);


  VLOCK;
  if(connect>0)
  {
    vtp->tcpClient[inst].IP4_StateRequest = 0;    // tcp: disconnect socket
    vtp->tcpClient[inst].Ctrl = 0x03C5;           // tcp: reset: phy, qsfp, tcp
    usleep(10000);


    // Z7 socket connect
    vtp->tcpClient[inst].Ctrl = 0x03C0;           // tcp: reset: qsfp
    usleep(10000);
    vtp->tcpClient[inst].Ctrl = 0x13C0;           // tcp: reset: none
    usleep(250000);
    vtp->tcpClient[inst].IP4_StateRequest = 1;    // tcp: connect socket
    usleep(250000);

    /* Make sure we are connected before we send any data */
    {
      volatile unsigned int done=0;
      int wait=100;
      printf("%s: Connecting to Server...\n",__func__);
      while(wait>0) {
	done = (vtp->tcpClient[inst].IP4_TCPStatus)&0xff;
	if(done>0) {
	  printf("%s: TCP Connection - Complete! (%d)\n",__func__,(100-wait));
	  break;
        }
	wait--;
	sleep(1);
      }

      if (wait<=0) printf("%s: **WARNING**: TCP Connection - May have failed!\n",__func__);
    }


    /* Send optional Data required to complete connection to the CODA EMU (EB) */
    if((cdata!=0)&&(dlen!=0)) {
      printf("%s: Sending connection info (%d words)\n",__func__,dlen);
      for(jj=0; jj<dlen; jj++) {
	//printf(" 0x%08x ",cdata[jj]);
	vtp->roc.CpuAsyncEventData = cdata[jj];
      }
      printf("\n");
      vtp->roc.CpuAsyncEventLen = dlen;
    }


  }
  else if(connect == -1) {   /* force disconnect */
    printf("%s: Reset any TCP Connections\n",__func__);
    vtp->tcpClient[inst].IP4_StateRequest = 0;
    usleep(500000);

  } else {
    /*Before disconnecting the socket we should make sure all data has been sent by checking the 
      TCP buffer full bit in the ROC State register */
    int max=1000, tcpfull=VTP_ROC_STATE_TCPFULL;
    while(tcpfull) {
      max--;
      tcpfull = (vtp->roc.State)&VTP_ROC_STATE_TCPFULL;
      if(max==0) {
	printf("%s: ERROR: Data still present in TCP Buffer - NOT closing socket yet!\n",__func__);
	VUNLOCK;
	return ERROR;
      };
    }

    printf("%s: Closing TCP Socket... (max = %d)\n",__func__,max);
    vtp->tcpClient[inst].IP4_StateRequest = 0;
    usleep(500000);        // We can't reset hardware too soon, or the socket on the EB side might not close cleanly
    //    vtp->tcpClient[inst].Ctrl = 0x03C5;           // tcp: reset: phy, qsfp, tcp   maybe we dont need to do this
  }

  VUNLOCK;

  return OK;
}

int
vtpRocTcpGo()
{
  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);


  /* Nothing to do here yet */


  return OK;
}

/* If status comes back non-zero then the TCP link is up */
int
vtpRocTcpConnected()
{
  int status=0;

  CHECKINIT;
  CHECKTYPE(ZYNC_FW_TYPE_ZCODAROC,1);

  status = (vtp->tcpClient[0].IP4_TCPStatus)&0xff;

  return status;

}

/* Read the Network information for the VTP ROC output. There can be up to 
   4 seperate network interfaces available
 
   The default input file for the VTP should be found on the VTP SD card mounted
   under the the directory  /mnt/boot
   The filename should default to  <hostname>_net.txt
    
*/

int
vtpRocReadNetFile(char *filename_in)
{
  FILE   *fd;
  char   filename[128];
  int    jj, ch;
  char   str_tmp[256], keyword[64];
  char   host[32], value[64];
  unsigned int mm[2] = {0,0};


  gethostname(host,32);  /* obtain our hostname - and drop any domain extension */
  for(jj=0; jj<strlen(host); jj++)
    {
      if(host[jj] == '.')
	{
	  host[jj] = '\0';
	  break;
	}
    }

  
  if(filename_in == NULL) {  /* Use default file */
    sprintf(filename,"/mnt/boot/%s_net.txt",host);
  }else{
    strcpy(filename,filename_in); /* copy filename from parameter list to local string */
  }
  printf("%s: Opening Network file %s \n",__func__,filename);

  if((fd=fopen(filename,"r")) == NULL)
    {
      printf("%s: Can't open file >%s<\n",__func__,filename);
      return(-1);
    }

  /* Parse the file */
  
  while ((ch = getc(fd)) != EOF)
    {
      /* Skip comments and whitespace */
      if ( ch == '#' || ch == ' ' || ch == '\t' )
	{
	  while ( getc(fd)!='\n' /*&& getc(fd)!=!= EOF*/ ) {} /*ERROR !!!*/
	}
      else if( ch == '\n' ) {}
      else
	{ /* Backup and read the whole line into memory */
	  ungetc(ch,fd);
	  fgets(str_tmp, 256, fd);
	  sscanf (str_tmp, "%s %s", keyword, value);

	  printf("\nfgets returns %s so keyword=%s  val=%s\n",str_tmp,keyword,value);

	  if(strcmp(keyword,"ipaddr1")==0)
	    VTP_NET_OUT.ip[0] = vtpRoc_inet_addr(value);

	  if(strcmp(keyword,"gateway1")==0)
	    VTP_NET_OUT.gw[0] = vtpRoc_inet_addr(value);

	  if(strcmp(keyword,"subnet1")==0)
	    VTP_NET_OUT.sm[0] = vtpRoc_inet_addr(value);

	  if(strcmp(keyword,"mac1")==0) {
	    vtpRoc_mac_addr(value,mm);
	    VTP_NET_OUT.mac[0][0] = mm[0];
	    VTP_NET_OUT.mac[1][0] = mm[1];
	  }
	  
	}
    }

  fclose(fd);

  return(0);
}
