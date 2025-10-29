/*****************************************************************
 * 
 * tsprimary_list.c - "Primary" Readout list routines for tsprimary
 *
 * Usage:
 *
 *    #include "tsprimary_list.c"
 *
 *  then define the following routines:
 * 
 *    void rocDownload(); 
 *    void rocPrestart(); 
 *    void rocGo(); 
 *    void rocEnd(); 
 *    void rocTrigger();
 *
 * SVN: $Rev: 11244 $
 *
 */


#define ROL_NAME__ "TSPRIMARY"
#ifndef MAX_EVENT_LENGTH
/* Check if an older definition is used */
#ifdef MAX_SIZE_EVENTS
#define MAX_EVENT_LENGTH MAX_SIZE_EVENTS
#else
#define MAX_EVENT_LENGTH 10240
#endif /* MAX_SIZE_EVENTS */
#endif /* MAX_EVENT_LENGTH */
#ifndef MAX_EVENT_POOL
/* Check if an older definition is used */
#ifdef MAX_NUM_EVENTS
#define MAX_EVENT_POOL MAX_NUM_EVENTS
#else
#define MAX_EVENT_POOL   400
#endif /* MAX_NUM_EVENTS */
#endif /* MAX_EVENT_POOL */
/* POLLING_MODE */
#define POLLING___
#define POLLING_MODE
/* INIT_NAME should be defined by roc_### (maybe at compilation time - check Makefile-rol) */
#ifndef INIT_NAME
#warning "INIT_NAME undefined. Setting to tsprimary_list__init"
#define INIT_NAME tsprimary_list__init
#endif
#ifndef INIT_NAME_POLL
#warning "INIT_NAME_POLL undefined. Setting to tsprimary_list__poll"
#define INIT_NAME_POLL tsprimary_list__poll
#endif
#include <stdio.h>
#include <rol.h>
#include "jvme.h"
#include <TSPRIMARY_source.h>
#include "tsLib.h"
#include "remexLib.h"
extern int bigendian_out;

extern int tsNeedAck;
/*! Buffer node pointer */
extern DMANODE *the_event;
/*! Data pointer */
extern unsigned int *dma_dabufp;

int emptyCount = 0;   /* Count the number of times event buffers are empty */
int errCount = 0;

#define ISR_INTLOCK INTLOCK
#define ISR_INTUNLOCK INTUNLOCK

pthread_mutex_t ack_mutex=PTHREAD_MUTEX_INITIALIZER;
#define ACKLOCK {				\
    if(pthread_mutex_lock(&ack_mutex)<0)	\
      perror("pthread_mutex_lock");		\
  }
#define ACKUNLOCK {				\
    if(pthread_mutex_unlock(&ack_mutex)<0)	\
      perror("pthread_mutex_unlock");		\
  }
pthread_cond_t ack_cv = PTHREAD_COND_INITIALIZER;
#define ACKWAIT {					\
    if(pthread_cond_wait(&ack_cv, &ack_mutex)<0)	\
      perror("pthread_cond_wait");			\
  }
#define ACKSIGNAL {				\
    if(pthread_cond_signal(&ack_cv)<0)		\
      perror("pthread_cond_signal");		\
  }

int ack_runend=0;
pthread_cond_t endrun_cv = PTHREAD_COND_INITIALIZER;
struct timespec endrun_waittime;
int endrun_timedwait_ret=0;
#define ENDRUN_TIMEDWAIT(__x) {						\
    clock_gettime(CLOCK_REALTIME, &endrun_waittime);			\
    endrun_waittime.tv_sec += __x;					\
    endrun_timedwait_ret = pthread_cond_timedwait(&endrun_cv, &ack_mutex, &endrun_waittime); \
    if(endrun_timedwait_ret<0)						\
      perror("pthread_cond_timedwait");					\
  }
#define ENDRUN_SIGNAL {					\
    if(pthread_cond_signal(&endrun_cv)<0)		\
      perror("pthread_cond_signal");			\
  }

/* ROC Function prototypes defined by the user */
void rocDownload();
void rocPrestart();
void rocGo();
void rocEnd();
void rocTrigger(int arg);
void rocCleanup();
int  getOutQueueCount();
int  getInQueueCount();

/* Asynchronous (to tsprimary rol) trigger routine, connects to rocTrigger */
void asyncTrigger();

/* Input and Output Partitions for VME Readout */
DMA_MEM_ID vmeIN, vmeOUT;


static void __download()
{
  int status;
  /* char srocID[10];*/

  daLogMsg("INFO","Readout list compiled %s", DAYTIME);
#ifdef POLLING___
  rol->poll = 1;
#endif
  *(rol->async_roc) = 0; /* Normal ROC */

  bigendian_out=1;

  pthread_mutex_init(&ack_mutex, NULL);
  pthread_cond_init(&ack_cv,NULL);
  pthread_cond_init(&endrun_cv,NULL);
 
  /*  sprintf(srocID,"ROCID%d",ROCID);*/
  /* Initialize Host Remote Execution library */
  /*  remexSetRedirect(1);
      remexInit(srocID,1);*/

  /* Open the default VME windows */
  vmeOpenDefaultWindows();

  /* Initialize memory partition library */
  dmaPartInit();

  /* Setup Buffer memory to store events */
  dmaPFreeAll();
  vmeIN  = dmaPCreate("vmeIN",MAX_EVENT_LENGTH,MAX_EVENT_POOL,0);
  vmeOUT = dmaPCreate("vmeOUT",0,0,0);

  /* Reinitialize the Buffer memory */
  dmaPReInitAll();
  dmaPStatsAll();

  /* Initialize VME Interrupt interface - use defaults */
  /* tsInit(TS_ADDR,TS_READOUT,0); */
  status = tsInit(TS_ADDR,TS_READOUT,0);
  if(status == -1) daLogMsg("ERROR","Unable to initialize TS Board\n");

  /* Execute User defined download */
  rocDownload();

  daLogMsg("INFO","Download Executed");

  /* FIXME: Not sure if these belong at end of download, or at the beginning of prestart */
  tsClockReset();
  printf(" wait a second...\n");
  taskDelay(60);
  tsTrigLinkReset();
  taskDelay(2);

  /* For streaming configs we must enable User Sync Resets */
#ifdef STREAMING_MODE
  tsSetUserSyncResetReceive(1);
  tsUserSyncReset(1);
#endif

} /*end download */     

static void __prestart()
{
  ACKLOCK;
  ack_runend=0;
  ACKUNLOCK;
  CTRIGINIT;
  *(rol->nevents) = 0;
  unsigned long jj, adc_id;
  daLogMsg("INFO","Entering Prestart");

  TSPRIMARY_INIT;
  CTRIGRSS(TSPRIMARY,1,usrtrig,usrtrig_done);
  CRTTYPE(1,TSPRIMARY,1);

  /* Check the health of the vmeBus Mutex.. re-init if necessary */
  vmeCheckMutexHealth(10);

  /* Execute User defined prestart */
  rocPrestart();

  /* Send a Sync Reset */
  printf("%s: Sending sync\n",__FUNCTION__);
  taskDelay(2);
  tsSyncReset(1); /* Set Block level as well */
  taskDelay(2);
  tsSetBlockLevel(blockLevel);

  /* Connect User Trigger Routine */
  tsIntConnect(TS_INT_VEC,asyncTrigger,0);

  daLogMsg("INFO","Prestart Executed");

  if (__the_event__) WRITE_EVENT_;
  *(rol->nevents) = 0;
  rol->recNb = 0;
} /*end prestart */     

static void __end()
{
  int ii, ievt, rem_count;
  int len, type, lock_key;
  DMANODE *outEvent;
  int oldnumber;
  int iwait=0;
  int blocksLeft=0;
  unsigned int blockstatus=0;
  int bready=0;

  tsDisableTriggerSource(1);

  /* Disable the Stream */
#ifdef STREAMING_MODE
  tsUserSyncReset(1);
#endif

  blockstatus = tsBlockStatus(0,0);
  
  ACKLOCK;
  ack_runend=1;
  if(blockstatus)
    {
      printf("%s: Clearing data from TS (blockstatus = 0x%x)\n",__FUNCTION__, blockstatus);
      ENDRUN_TIMEDWAIT(30);
      printf("%s: endrun_timedwait_ret = %d   blockstatus = 0x%x\n",
	     __FUNCTION__,endrun_timedwait_ret,tsBlockStatus(0,0));
    }
  ACKUNLOCK;

  INTLOCK;
  INTUNLOCK;

  /*  tsStatus(0);
      dmaPStatsAll();*/
  tsIntDisable();
  tsIntDisconnect();

  /* Execute User defined end */
  rocEnd();

  CDODISABLE(TSPRIMARY,1,0);
 
  dmaPStatsAll();
      
  daLogMsg("INFO","End Executed");

  if (__the_event__) WRITE_EVENT_;
} /* end end block */

static void __pause()
{
  CDODISABLE(TSPRIMARY,1,0);
  daLogMsg("INFO","Pause Executed");
  
  if (__the_event__) WRITE_EVENT_;
} /*end pause */

static void __go()
{
  daLogMsg("INFO","Entering Go");
  ACKLOCK;
  ack_runend=0;
  ACKUNLOCK;

  emptyCount=0;
  errCount=0;

  CDOENABLE(TSPRIMARY,1,1);
  rocGo();
  
  tsIntEnable(1); 

  /* Enable the Stream */
#ifdef STREAMING_MODE
  tsUserSyncReset(0);
#endif

  
  if (__the_event__) WRITE_EVENT_;
}

void usrtrig(unsigned long EVTYPE,unsigned long EVSOURCE)
{
  long EVENT_LENGTH;
  int ii, len, data, lock_key;
  int syncFlag=0, lateFail=0;
  unsigned int event_number=0;
  unsigned int currentWord=0;
  DMANODE *outEvent;

  outEvent = dmaPGetItem(vmeOUT);

  if(outEvent != NULL) 
    {
      len = outEvent->length;
      syncFlag = outEvent->type;
      event_number = outEvent->nevent;

      CEOPEN(ROCID, BT_BANK, blockLevel);

      /*rol->dabufp -= 2;*/

      if(rol->dabufp != NULL) 
	{
	  for(ii=0;ii<len;ii++) 
	    {
	      currentWord = outEvent->data[ii];
	      *rol->dabufp++ = currentWord;
	    }
	}
      else 
	{
	  printf("tsprimary_list: ERROR rol->dabufp is NULL -- Event lost\n");
	}

      CECLOSE;

      ACKLOCK;

      dmaPFreeItem(outEvent);

      if(tsNeedAck>0)
	{
	  tsNeedAck=0;
	  ACKSIGNAL;
	}

      if(ack_runend)
	{
	  if(tsBlockStatus(0,0)==0)
	    ENDRUN_SIGNAL;
	}

      ACKUNLOCK;
    }
  else
    {
      logMsg("Error: no Event in vmeOUT queue\n",0,0,0,0,0,0);
    }
  
 
} /*end trigger */

void asyncTrigger()
{
  int intCount=0;
  int length,size;
  unsigned int tirval;
  int clocksource=-1;
  /*  int syncFlag=0;*/

  intCount = tsGetIntCount();
  syncFlag = tsGetSyncEventFlag();

  /* grap a buffer from the queue */
  GETEVENT(vmeIN,intCount);
  if(the_event == NULL)
    {
      if(errCount == 0)
	daLogMsg("ERROR","asyncTrigger: No DMA Buffer Available. Events could be out of sync!");
      printf("asyncTrigger:ERROR: No buffer available!\n");
      errCount++;
      return;
    }
  if(the_event->length!=0) 
    {
      printf("asyncTrigger: ERROR: Interrupt Count = %d the_event->length = %d\t",intCount, the_event->length);
    }

  /*  EVENTOPEN(ROCID,BT_BANK,blockLevel);*/
  /* Store Sync Flag status for this event */
  syncFlag = tsGetSyncEventFlag();
  the_event->type = syncFlag;
  
  /* Execute user defined Trigger Routine */
  rocTrigger(intCount);

  /*  EVENTCLOSE; */

  /* Put this event's buffer into the OUT queue. */
  ACKLOCK;
  PUTEVENT(vmeOUT);
  /* Check if the event length is larger than expected */
  length = (((int)(dma_dabufp) - (int)(&the_event->length))) - 4;
  size = the_event->part->size - sizeof(DMANODE);

  if(length>size) 
    {
      printf("rocLib: ERROR: Event length > Buffer size (%d > %d).  Event %d\n",
	     length,size,the_event->nevent);
    }

  if(dmaPEmpty(vmeIN))
    {

      /*       printf("WARN: vmeIN out of event buffers (intCount = %d).\n",intCount); */

      if((ack_runend==0) || (tsBReady()>0))
	{
	  /* Set the NeedAck for Ack after a buffer is freed */
	  tsNeedAck = 1;
	  
	  /* Wait for the signal indicating that a buffer has been freed */
	  ACKWAIT;
	}

    }

  ACKUNLOCK;

}

void usrtrig_done()
{
} /*end done */

void __done()
{
  poolEmpty = 0; /* global Done, Buffers have been freed */
} /*end done */

static void __reset()
{
  int iemp=0;

  tsIntDisable();
  tsIntDisconnect();

  /* Empty the vmeOUT queue */
  while(!dmaPEmpty(vmeOUT))
    {
      iemp++;
      dmaPFreeItem(dmaPGetItem(vmeOUT));
      if(iemp>=MAX_EVENT_POOL) break;
    }

  printf("** Reset called **\n");
} /* end reset */

int
getOutQueueCount()
{
  if(vmeOUT) 
    return(vmeOUT->list.c);
  else
    return(0);
}

int
getInQueueCount()
{
  if(vmeIN) 
    return(vmeIN->list.c);
  else
    return(0);
}

/* This routine is automatically executed just before the shared libary
   is unloaded.

   Clean up memory that was allocated 
*/
__attribute__((destructor)) void end (void)
{
  static int ended=0;

  if(ended==0)
    {
      printf("ROC Cleanup\n");
      
      rocCleanup();
      
      /*  remexClose();      */
      dmaPFreeAll();
      /*      vmeCloseDefaultWindows();*/

      ended=1;
    }

}
