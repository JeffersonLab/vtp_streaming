#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/mman.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CrateMsgTypes.h"





static ServerCBFunctions gServerCBFunctions;
short gListenPort;

typedef struct
{
  int sock;
  CrateMsgStruct msg;
} SocketThreadStruct;

int
CrateMsg_Read16(CrateMsgStruct *msg, int swap)
{
	if(swap)
	{
		msg->msg.m_Cmd_Read16.cnt = DW_SWAP(msg->msg.m_Cmd_Read16.cnt);
		msg->msg.m_Cmd_Read16.addr = DW_SWAP(msg->msg.m_Cmd_Read16.addr);
		msg->msg.m_Cmd_Read16.flags = DW_SWAP(msg->msg.m_Cmd_Read16.flags);
	}
	if(gServerCBFunctions.Read16)
		return (*gServerCBFunctions.Read16)(&msg->msg.m_Cmd_Read16, &msg->msg.m_Cmd_Read16_Rsp);

	return 0;
}

int
CrateMsg_Write16(CrateMsgStruct *msg, int swap)
{
	int i;

	if(swap)
	{
		msg->msg.m_Cmd_Write16.cnt = DW_SWAP(msg->msg.m_Cmd_Write16.cnt);
		msg->msg.m_Cmd_Write16.addr = DW_SWAP(msg->msg.m_Cmd_Write16.addr);
		msg->msg.m_Cmd_Write16.flags = DW_SWAP(msg->msg.m_Cmd_Write16.flags);

		for(i = msg->msg.m_Cmd_Write16.cnt-1; i >= 0; i--)
			msg->msg.m_Cmd_Write16.vals[i] = HW_SWAP(msg->msg.m_Cmd_Write16.vals[i]);
	}
	if(gServerCBFunctions.Write16)
		(*gServerCBFunctions.Write16)(&msg->msg.m_Cmd_Write16);

	return 0;
}

int
CrateMsg_Read32(CrateMsgStruct *msg, int swap)
{
	if(swap)
	{
		msg->msg.m_Cmd_Read32.cnt = DW_SWAP(msg->msg.m_Cmd_Read32.cnt);
		msg->msg.m_Cmd_Read32.addr = DW_SWAP(msg->msg.m_Cmd_Read32.addr);
		msg->msg.m_Cmd_Read32.flags = DW_SWAP(msg->msg.m_Cmd_Read32.flags);
	}
	if(gServerCBFunctions.Read32)
		return (*gServerCBFunctions.Read32)(&msg->msg.m_Cmd_Read32, &msg->msg.m_Cmd_Read32_Rsp);

	return 0;
}

int
CrateMsg_Write32(CrateMsgStruct *msg, int swap)
{
	int i;

	if(swap)
	{
		msg->msg.m_Cmd_Write32.cnt = DW_SWAP(msg->msg.m_Cmd_Write32.cnt);
		msg->msg.m_Cmd_Write32.addr = DW_SWAP(msg->msg.m_Cmd_Write32.addr);
		msg->msg.m_Cmd_Write32.flags = DW_SWAP(msg->msg.m_Cmd_Write32.flags);

		for(i = msg->msg.m_Cmd_Write32.cnt-1; i >= 0; i--)
			msg->msg.m_Cmd_Write32.vals[i] = DW_SWAP(msg->msg.m_Cmd_Write32.vals[i]);
	}
	if(gServerCBFunctions.Write32)
		(*gServerCBFunctions.Write32)(&msg->msg.m_Cmd_Write32);

	return 0;
}

int
CrateMsg_Delay(CrateMsgStruct *msg, int swap)
{
	if(swap)
		msg->msg.m_Cmd_Delay.ms = DW_SWAP(msg->msg.m_Cmd_Delay.ms);
	if(gServerCBFunctions.Delay)
		(*gServerCBFunctions.Delay)(&msg->msg.m_Cmd_Delay);

	return 0;
}





/* ERROR: ADD SWAP IN FUNCTIONS BELOW !!!!!!!!!!!!!!!!!!!!! */


int
CrateMsg_ReadScalers(CrateMsgStruct *msg, int swap)
{
  int ret;

  if(gServerCBFunctions.ReadScalers)
  {
    ret = (*gServerCBFunctions.ReadScalers)(&msg->msg.m_Cmd_ReadScalers, &msg->msg.m_Cmd_ReadScalers_Rsp);
	/*printf("CrateMsg_ReadScalers ret=%d\n",ret);*/
	return (ret);
  }
  return(0);
}


int
CrateMsg_ReadData(CrateMsgStruct *msg, int swap)
{
  if(gServerCBFunctions.ReadData)
  {
	return (*gServerCBFunctions.ReadData)(&msg->msg.m_Cmd_ReadScalers, &msg->msg.m_Cmd_ReadScalers_Rsp);
  }
  return(0);
}



int
CrateMsg_GetCrateMap(CrateMsgStruct *msg, int swap)
{
  if(gServerCBFunctions.GetCrateMap)
  {
	return (*gServerCBFunctions.GetCrateMap)(&msg->msg.m_Cmd_GetCrateMap, &msg->msg.m_Cmd_GetCrateMap_Rsp);
  }
  return(0);
}

int
CrateMsg_GetBoardParams(CrateMsgStruct *msg, int swap)
{
  if(gServerCBFunctions.GetBoardParams)
  {
	return (*gServerCBFunctions.GetBoardParams)(&msg->msg.m_Cmd_GetBoardParams, &msg->msg.m_Cmd_GetBoardParams_Rsp);
  }
  return(0);
}

int
CrateMsg_GetChannelParams(CrateMsgStruct *msg, int swap)
{
  if(gServerCBFunctions.GetChannelParams)
  {
	return (*gServerCBFunctions.GetChannelParams)(&msg->msg.m_Cmd_GetChannelParams, &msg->msg.m_Cmd_GetChannelParams_Rsp);
  }
  return(0);
}


int
CrateMsg_SetChannelParams(CrateMsgStruct *msg, int swap)
{
  if(gServerCBFunctions.SetChannelParams)
  {
	return (*gServerCBFunctions.SetChannelParams)(&msg->msg.m_Cmd_SetChannelParams);
  }
  return(0);
}















void *
ConnectionThread(void *parm)
{
  int swap, result, val;
  SocketThreadStruct *pParm = (SocketThreadStruct *)parm;

  val = CRATEMSG_HDR_ID;
  if(send(pParm->sock, &val, 4, 0) <= 0)
  {
	printf("ConnectionThread: Error in %s: failed to send HDRID\n", __FUNCTION__);
	goto ConnectionThread_exit;
  }
  printf("ConnectionThread: send val=0x%08x\n",val);

  if(recv(pParm->sock, &val, 4, 0) != 4)
  {
	printf("ConnectionThread: Error in %s: failed to recv HDRID\n", __FUNCTION__);
	goto ConnectionThread_exit;
  }
  printf("ConnectionThread: recv val=0x%08x\n",val);

  // determine sender endianess...
  if(val == CRATEMSG_HDR_ID)
  {
	printf("ConnectionThread: swap=0\n");
	swap = 0;
  }
  else if(val == DW_SWAP(CRATEMSG_HDR_ID))
  {
	printf("ConnectionThread: swap=1\n");
   	swap = 1;
  }
  else
  {
   	printf("ConnectionThread: Error in %s: bad recv HDRID\n", __FUNCTION__);
   	goto ConnectionThread_exit;
  }

  while(1)
  {
    /*printf("ConnectionThread befor recv1 - expecting  message length and message type - 8 bytes total\n");*/
   	if( (result = recv(pParm->sock, (char *)&pParm->msg, 8, 0)) != 8)
   	{
      printf("ConnectionThread: break 1: result=%d\n",result);
      if(result==0) printf("ConnectionThread: Probably client closed connection\n");
      printf("ConnectionThread: error %d >%s<\n",errno,strerror(errno));
   	  /*break;*/

      /*goto error_exit; sergey: does not release memory */
      goto ConnectionThread_exit;

   	}
    /*printf("ConnectionThread after recv1, result=%d\n",result);*/

   	if(swap)
   	{
   	  pParm->msg.len = DW_SWAP(pParm->msg.len);
   	  pParm->msg.type = DW_SWAP(pParm->msg.type);
   	}

   	if( (pParm->msg.len > MAX_MSG_SIZE) || (pParm->msg.len < 0) )
	{
      printf("ConnectionThread: break 2: pParm->msg.len=%d > MAX_MSG_SIZE=%d, or pParm->msg.len=%d < 0\n",pParm->msg.len, MAX_MSG_SIZE, pParm->msg.len);
	  break;
	}

    /*printf("ConnectionThread befor recv2, expecting msg.len=%d msg.type=%d\n",pParm->msg.len,pParm->msg.type);*/
   	if(pParm->msg.len && ((result = recv(pParm->sock, (char *)&pParm->msg.msg, pParm->msg.len, 0)) != pParm->msg.len))
	{
      printf("ConnectionThread: break 3: result=%d, pParm->msg.len=%d\n",result,pParm->msg.len);
	  break;
	}
    /*printf("ConnectionThread after recv2, result=%d\n",result);*/

	result = -1;


	/* VME commands */
	if(pParm->msg.type == CRATEMSG_TYPE_READ16)
	  result = CrateMsg_Read16(&pParm->msg, swap);
	else if(pParm->msg.type == CRATEMSG_TYPE_WRITE16)
	  result = CrateMsg_Write16(&pParm->msg, swap);
	else if(pParm->msg.type == CRATEMSG_TYPE_READ32)
	  result = CrateMsg_Read32(&pParm->msg, swap);
	else if(pParm->msg.type == CRATEMSG_TYPE_WRITE32)
	  result = CrateMsg_Write32(&pParm->msg, swap);
	else if(pParm->msg.type == CRATEMSG_TYPE_DELAY)
	  result = CrateMsg_Delay(&pParm->msg, swap);


	/* scaler commands */
	else if(pParm->msg.type == SCALER_SERVER_READ_BOARD)
	{
	  result = CrateMsg_ReadScalers(&pParm->msg, swap);
	}
	else if(pParm->msg.type == SCALER_SERVER_GET_CRATE_MAP)
	{
	  result = CrateMsg_GetCrateMap(&pParm->msg, swap);
	}
	else if(pParm->msg.type == SCALER_SERVER_GET_BOARD_PARAMS)
	{
	  result = CrateMsg_GetBoardParams(&pParm->msg, swap);
	}
	else if(pParm->msg.type == SCALER_SERVER_GET_CHANNEL_PARAMS)
	{
	  result = CrateMsg_GetChannelParams(&pParm->msg, swap);
	}
	else if(pParm->msg.type == SCALER_SERVER_SET_CHANNEL_PARAMS)
	{
	  result = CrateMsg_SetChannelParams(&pParm->msg, swap);
	}

	else if(pParm->msg.type == DATA_SERVER_READ_BOARD)
	{
	  result = CrateMsg_ReadData(&pParm->msg, swap);
	}

	else
	{
   	  printf("ConnectionThread: Error in %s: unhandled message type %u\n", __FUNCTION__, pParm->msg.type);
	  break;
	}

	if(result < 0)
	{
	  printf("ConnectionThread: Error in %s: failed to process msg type %u\n", __FUNCTION__, pParm->msg.type);
	  break;
	}
	else if(result > 0)
	{
	  pParm->msg.len = result;
	  pParm->msg.type = CMD_RSP(pParm->msg.type);
	  if(send(pParm->sock, &pParm->msg, pParm->msg.len+8, 0) <= 0)
	  {
		printf("ConnectionThread: Error in %s: failed to send msg type %u\n", __FUNCTION__, pParm->msg.type);
		break;
	  }
	}
  }


ConnectionThread_exit:

  if(pParm!=NULL)
  {
    printf("ConnectionThread: Closing connection ..\n");fflush(stdout);
    close(pParm->sock);
    printf("ConnectionThread: Releasing memory at 0x%08x ..\n",
	   (unsigned int)pParm);fflush(stdout);
    free(pParm);
    pParm = NULL;
  }
/* error_exit: */

  printf("ConnectionThread: calling 'pthread_exit' ..\n");fflush(stdout);
  pthread_exit(NULL);
}


void *
ListenerThread(void *p)
{
  pthread_t cThread;
  SocketThreadStruct *pcThreadParm = NULL;
  socklen_t sockAddrSize = sizeof(struct sockaddr_in);
  struct sockaddr_in clientAddr;
  struct sockaddr_in serverAddr;
  int lsock, csock;

  printf("ListenerThread: reached, port >%d<\n",gListenPort);fflush(stdout);

  memset((char *)&serverAddr, 0, sockAddrSize);
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(gListenPort);
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

  printf("ListenerThread: befor socket\n");fflush(stdout);
  lsock = socket(AF_INET, SOCK_STREAM, 0);
  printf("ListenerThread: after socket\n");fflush(stdout);
  if(lsock == -1)
  {
	printf("ListenerThread: Error in %s: create socket failed\n", __FUNCTION__);fflush(stdout);
	return 0;
  }


next_port:

  printf("ListenerThread: befor bind\n");fflush(stdout);
  if(bind(lsock, (struct sockaddr *)&serverAddr, sockAddrSize) == -1)
  {
	printf("ListenerThread: Error in %s: bind() failed\n", __FUNCTION__);fflush(stdout);

	/* in case if port is busy, grab next one */
    gListenPort ++;
    serverAddr.sin_port = htons(gListenPort);
    goto next_port;

	close(lsock);
	return 0;
  }
  printf("ListenerThread: after bind\n");fflush(stdout);

  printf("ListenerThread: befor listen\n");fflush(stdout);
  if(listen(lsock, 1) == -1)
  {
	printf("ListenerThread: Error in %s: listen() failed\n", __FUNCTION__);fflush(stdout);
	close(lsock);
	return 0;
  }
  printf("ListenerThread: after listen\n");fflush(stdout);



  while(1)
  {
	printf("ListenerThread: waiting for accept, port >%d<\n",gListenPort);fflush(stdout);
    csock = accept(lsock, (struct sockaddr *)&clientAddr, &sockAddrSize);

	printf("ListenerThread: accepted\n");fflush(stdout);
    if(csock < 0)
	{
      printf("ListenerThread: Error in %s: accept() failed\n", __FUNCTION__);fflush(stdout);
	  break;
	}




/*if(pcThreadParm==NULL)*/
{
	pcThreadParm = (SocketThreadStruct *) malloc(sizeof(SocketThreadStruct));
	pcThreadParm->sock = csock;
	if(!pcThreadParm)
	{
	  printf("ListenerThread: Error in %s: malloc() failed\n", __FUNCTION__);fflush(stdout);
	  break;
	}
    else
	{
      printf("ListenerThread: malloc'ed %d bytes at pcThreadParm=0x%08x\n"
	     ,sizeof(SocketThreadStruct),
	     (unsigned int)pcThreadParm);
      fflush(stdout);
	}
 }



	/* block annoying IP address(es) */
	{
      char *address;
      unsigned short port;

      address = inet_ntoa(clientAddr.sin_addr);
      port = ntohs (clientAddr.sin_port);

	  /*
      if(!strncmp(address,"129.57.71.",10))
	  {
        printf("CrateMsgServer: ignore request from %s\n",address);fflush(stdout);
        close(lsock);
        continue;
	  }
	  */

      if( (strncmp(address,"129.57.37.",10) != 0) &&
	  (strncmp(address,"129.57.192.",11) != 0) &&
	  (strncmp(address,"129.57.193.",11) != 0) )
	{
	  printf("ListenerThread:  ignore request from %s (port %d)\n",
		 address, port);fflush(stdout);
	  /*close(lsock);*/
	  continue;
	}

	}


	pthread_create(&cThread, NULL, ConnectionThread, (void *)pcThreadParm);
	pthread_detach(cThread); /*sergey: without this have memory leak 10M on every pthread_create*/
  }

  close(lsock);

  return(0);
}



int
CrateMsgServerStart(ServerCBFunctions *pCB, unsigned short listen_port)
{
	pthread_t gListenerThread;

	gListenPort = listen_port;

	memcpy(&gServerCBFunctions, pCB, sizeof(ServerCBFunctions));

	pthread_create(&gListenerThread, NULL, ListenerThread, NULL);

	return 0;
}
