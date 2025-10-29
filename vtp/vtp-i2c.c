/*----------------------------------------------------------------------------*
 *  Copyright (c) 2016        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Authors: Bryan Moffit                                                   *
 *             moffit@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5660             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *     VTP-I2C library
 *
 *----------------------------------------------------------------------------*/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include "vtpLib.h"
#include "vtp-i2c.h"

static const int nVTPI2CFD = 2;
static int vtpI2CFD[2] = {-1, -1};
const char vtpI2CDev[2][256] = {
  "/dev/i2c-0",
  "/dev/i2c-1"
};

extern uint32_t vtpDebugMask;

#define CHECKI2CID(x)				\
  if(vtpI2CFD[x] <= 0) \
    {							\
      printf("%s: ERROR: VTP I2C-%d not open.\n",	\
	     __func__,x);				\
      return ERROR;					\
    }							\

int
vtpI2COpen()
{
  int iid;

  for(iid = 0; iid < nVTPI2CFD; iid++)
    {
      if(vtpI2CFD[iid] > 0)
	{
	  VTP_DBGN(VTP_DEBUG_INIT, "VTP I2C-%d already open.\n",
		   iid);
	  return OK;
	}

      VTP_DBGN(VTP_DEBUG_INIT, "open I2C device = %s\n", vtpI2CDev[iid]);
      vtpI2CFD[iid] = open(vtpI2CDev[iid], O_RDWR);

      if(vtpI2CFD[iid] < 0)
	{
	  printf("%s: ERROR for %s from open: %s (%d)",
		 __func__, vtpI2CDev[iid], strerror(errno), errno);
	  return ERROR;
	}
    }

  return OK;
}

int
vtpI2CClose()
{
  int iid;

  for(iid = 0; iid < nVTPI2CFD; iid++)
    {
      CHECKI2CID(iid);

      close(vtpI2CFD[iid]);
    }

  return OK;
}

int
vtpI2CSelectSlave(int id, uint8_t slaveAddr)
{
  int lerrno;

  CHECKI2CID(id);

  if(ioctl(vtpI2CFD[id], I2C_SLAVE, slaveAddr) < 0)
    {
      lerrno = errno;
      printf("%s(%d): ioctl ERROR %d: %s\n", __func__,
	     id, lerrno, strerror(lerrno));
      return ERROR;
    }

  return OK;
}

int
vtpI2CWriteCmd(int id, uint8_t cmd)
{
  int lerrno = 0;

  CHECKI2CID(id);

  if(i2c_smbus_write_byte(vtpI2CFD[id], cmd) < 0)
    {
      lerrno = errno;
      printf("%s(%d, 0x%x): i2c write byte ERROR %d: %s\n", __func__,
       id, cmd,
       lerrno, strerror(lerrno));
      return ERROR;
    }

  return OK;
}

uint8_t
vtpI2CRead8(int id, uint8_t cmd)
{
  uint32_t rval = 0;
  int lerrno = 0;

  CHECKI2CID(id);

  if((rval = i2c_smbus_read_byte_data(vtpI2CFD[id], cmd)) < 0)
	{
	  lerrno = errno;
	  printf("%s(%d): i2c read word ERROR %d: %s\n", __func__,
		 id, lerrno, strerror(lerrno));
	  return ERROR;
	}

  return (uint8_t)(rval & 0xFF);
}

uint16_t
vtpI2CRead16(int id, uint8_t cmd)
{
  uint32_t rval = 0;
  int lerrno = 0;

  CHECKI2CID(id);

  if((rval = i2c_smbus_read_word_data(vtpI2CFD[id], cmd)) < 0)
    {
      lerrno = errno;
      printf("%s(%d): i2c read word ERROR %d: %s\n", __func__,
	     id, lerrno, strerror(lerrno));
      return ERROR;
    }

  return (uint16_t)(rval & 0xFFFF);
}

uint32_t
vtpI2CReadBlock(int id, uint8_t cmd, uint8_t *buf)
{
  uint32_t rval = 0;
  int lerrno = 0;

  CHECKI2CID(id);

  if((rval = i2c_smbus_read_block_data(vtpI2CFD[id], cmd, buf)) < 0)
    {
      lerrno = errno;
      printf("%s(%d): i2c read block ERROR %d: %s\n", __func__,
	     id, lerrno, strerror(lerrno));
      return ERROR;
    }

  return rval;
}

int
vtpI2CWrite8(int id, uint8_t cmd, uint8_t val)
{
  int lerrno = 0;

  CHECKI2CID(id);

  if(i2c_smbus_write_byte_data(vtpI2CFD[id], cmd, val) < 0)
    {
      lerrno = errno;
      printf("%s(%d, 0x%x,0x%x): i2c write byte ERROR %d: %s\n", __func__,
	     id, cmd, val,
	     lerrno, strerror(lerrno));
      return ERROR;
    }

  return OK;
}

int
vtpI2CWrite16(int id, uint8_t cmd, uint16_t val)
{
  int lerrno = 0;

  CHECKI2CID(id);

  if(i2c_smbus_write_word_data(vtpI2CFD[id], cmd, val) < 0)
    {
      lerrno = errno;
      printf("%s(%d, 0x%x,0x%x): i2c write word ERROR %d: %s\n", __func__,
	     id, cmd, val,
	     lerrno, strerror(lerrno));
      return ERROR;
    }

  return OK;
}
