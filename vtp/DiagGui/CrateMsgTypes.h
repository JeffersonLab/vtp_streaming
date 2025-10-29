#ifndef CRATEMSGTYPES_H
#define CRATEMSGTYPES_H




#define CRATEMSG_LISTEN_PORT			6102
#define MAX_MSG_SIZE					40000

#define CRATEMSG_HDR_ID					0x12345678

#define CRATEMSG_TYPE_READ16			0x01
#define CRATEMSG_TYPE_WRITE16			0x02
#define CRATEMSG_TYPE_READ32			0x03
#define CRATEMSG_TYPE_WRITE32			0x04
#define CRATEMSG_TYPE_DELAY			    0x05


#define SCALER_SERVER_READ_BOARD	      0x100
#define SCALER_SERVER_GET_CRATE_MAP       0x101
#define SCALER_SERVER_GET_BOARD_PARAMS	  0x102
#define SCALER_SERVER_GET_CHANNEL_PARAMS  0x103
#define SCALER_SERVER_SET_CHANNEL_PARAMS  0x104
#define DATA_SERVER_READ_BOARD            0x105


#define SCALER_TYPE_DSC2        0
#define SCALER_TYPE_FADC250     1
#define SCALER_TYPE_VSCM        2
#define SCALER_TYPE_VTP         3
#define SCALER_TYPE_SSP         4
#define SCALER_TYPE_TD          5
#define SCALER_TYPE_TS          6
#define SCALER_TYPE_MAX         7   /* the maximum number of different board types */


#define SCALER_PARTYPE_THRESHOLD    0
#define SCALER_PARTYPE_THRESHOLD2   1
#define SCALER_PARTYPE_NCHANNELS    2



#define CMD_RSP(cmd)						(cmd | 0x80000000)

#define DW_SWAP(v)                      ((((v) & 0x000000ff) << 24) | \
                                        (((v) & 0x0000ff00) <<  8) | \
                                        (((v) & 0x00ff0000) >>  8) | \
                                        (((v) & 0xff000000) >> 24))
/*
#define DW_SWAP(v)						( ((v>>24) & 0x000000FF) |\
										  ((v<<24) & 0xFF000000) |\
										  ((v>>8)  & 0x0000FF00) |\
										  ((v<<8)  & 0x00FF0000) )
*/

#define HW_SWAP(v)						( ((v>>8) & 0x00FF) |\
										  ((v<<8) & 0xFF00) )

#define LSWAP(v)                        ((((v) & 0x000000ff) << 24) | \
                                        (((v) & 0x0000ff00) <<  8) | \
                                        (((v) & 0x00ff0000) >>  8) | \
                                        (((v) & 0xff000000) >> 24))
/*
#define LSWAP(v)						( ((v>>24) & 0x000000FF) |\
                					      ((v<<24) & 0xFF000000) |\
                					      ((v>>8)  & 0x0000FF00) |\
                					      ((v<<8)  & 0x00FF0000) )
*/
#define HSWAP(v)						( ((v>>8) & 0x00FF) |\
                					      ((v<<8) & 0xFF00) )
                					  
/*****************************************************/
/*********** Some Board Generic Commands *************/
/*****************************************************/

#define CRATE_MSG_FLAGS_NOADRINC	0x0
#define CRATE_MSG_FLAGS_ADRINC		0x1
#define CRATE_MSG_FLAGS_USEDMA		0x2

typedef struct
{
	int cnt;
	int addr;
	int flags;
} Cmd_Read16;

typedef struct
{
	int cnt;
	short vals[MAX_MSG_SIZE/2];
} Cmd_Read16_Rsp;

typedef struct
{
	int cnt;
	int addr;
	int flags;
	short vals[MAX_MSG_SIZE/2];
} Cmd_Write16;

typedef struct
{
	int cnt;
	int addr;
	int flags;
} Cmd_Read32;

typedef struct
{
	int cnt;
	int vals[MAX_MSG_SIZE/4];
} Cmd_Read32_Rsp;

typedef struct
{
	int cnt;
	int addr;
	int flags;
	int vals[MAX_MSG_SIZE/4];
} Cmd_Write32;

typedef struct
{
	int ms;
} Cmd_Delay;






/*******************/
/* scaler commands */

typedef struct
{
  int cnt;
  int slot;
} Cmd_ReadScalers;

typedef struct
{
  int cnt;
  unsigned int vals[MAX_MSG_SIZE/4];
} Cmd_ReadScalers_Rsp;


typedef struct
{
  int cnt;
} Cmd_GetCrateMap;

typedef struct
{
  int cnt;
  int nslots;
  unsigned int vals[MAX_MSG_SIZE/4];
} Cmd_GetCrateMap_Rsp;





typedef struct
{
  int slot;
  int partype;
} Cmd_GetBoardParams;

typedef struct
{
  int cnt;
  unsigned int vals[MAX_MSG_SIZE/4];
} Cmd_GetBoardParams_Rsp;



typedef struct
{
  int slot;
  int channel;
  int partype;
} Cmd_GetChannelParams;

typedef struct
{
  int cnt;
  unsigned int vals[MAX_MSG_SIZE/4];
} Cmd_GetChannelParams_Rsp;



typedef struct
{
  int slot;
  int channel;
  int partype;
  int cnt;
  unsigned int vals[MAX_MSG_SIZE/4];
} Cmd_SetChannelParams;





/*****************************************************/
/*************** Main message structure **************/
/*****************************************************/
typedef struct
{
  int len;
  int type;
  
  union
  {
	Cmd_Read16				  m_Cmd_Read16;
	Cmd_Read16_Rsp			  m_Cmd_Read16_Rsp;
	Cmd_Write16				  m_Cmd_Write16;
	Cmd_Read32				  m_Cmd_Read32;
	Cmd_Read32_Rsp			  m_Cmd_Read32_Rsp;
	Cmd_Write32				  m_Cmd_Write32;
	Cmd_Delay				  m_Cmd_Delay;

	Cmd_ReadScalers	          m_Cmd_ReadScalers;
	Cmd_ReadScalers_Rsp	      m_Cmd_ReadScalers_Rsp;
	Cmd_GetCrateMap	          m_Cmd_GetCrateMap;
	Cmd_GetCrateMap_Rsp	      m_Cmd_GetCrateMap_Rsp;
	Cmd_GetBoardParams	      m_Cmd_GetBoardParams;
	Cmd_GetBoardParams_Rsp	  m_Cmd_GetBoardParams_Rsp;
	Cmd_GetChannelParams	  m_Cmd_GetChannelParams;
	Cmd_GetChannelParams_Rsp  m_Cmd_GetChannelParams_Rsp;
	Cmd_SetChannelParams	  m_Cmd_SetChannelParams;

	unsigned char			  m_raw[MAX_MSG_SIZE];
  } msg;
} CrateMsgStruct;

/*****************************************************/
/************* Server message interface **************/
/*****************************************************/
typedef struct
{
  int (*Read16)(Cmd_Read16 *pCmd_Read16, Cmd_Read16_Rsp *pCmd_Read16_Rsp);
  int (*Write16)(Cmd_Write16 *pCmd_Write16);
  int (*Read32)(Cmd_Read32 *pCmd_Read32, Cmd_Read32_Rsp *pCmd_Read32_Rsp);
  int (*Write32)(Cmd_Write32 *pCmd_Write32);
  int (*Delay)(Cmd_Delay *pCmd_Delay);
  int (*ReadScalers)(Cmd_ReadScalers *pCmd, Cmd_ReadScalers_Rsp *pCmd_Rsp);
  int (*ReadData)(Cmd_ReadScalers *pCmd, Cmd_ReadScalers_Rsp *pCmd_Rsp);
  int (*GetCrateMap)(Cmd_GetCrateMap *pCmd, Cmd_GetCrateMap_Rsp *pCmd_Rsp);
  int (*GetBoardParams)(Cmd_GetBoardParams *pCmd, Cmd_GetBoardParams_Rsp *pCmd_Rsp);
  int (*GetChannelParams)(Cmd_GetChannelParams *pCmd, Cmd_GetChannelParams_Rsp *pCmd_Rsp);
  int (*SetChannelParams)(Cmd_SetChannelParams *pCmd);
} ServerCBFunctions;

int CrateMsgServerStart(ServerCBFunctions *pCB, unsigned short listen_port);

/******************************************************
*******************************************************
******************************************************/

#endif
