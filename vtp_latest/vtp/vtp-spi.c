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
 *     VTP-SPI library
 *
 *----------------------------------------------------------------------------*/

#include <linux/spi/spidev.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include "vtpLib.h"
#include "vtp-spi.h"

static const int nVTPSPIFD = 1;
static int vtpSPIFD[1] = {-1, };
const char vtpSPIDev[1][256] = {"/dev/spidev32766.0", };
static uint32_t vtpSpiMode[1];
uint32_t vtpSpiSpeed[1] = {781250, };
uint16_t vtpSpiDelay[1] = {200, };
uint8_t vtpSpiBits[1] = {8, };

extern uint32_t vtpDebugMask;

/* static struct spi_ioc_transfer xfer[2]; */
#define CHECKSPIID(x)				\
  if(vtpSPIFD[x] <= 0) \
    {							\
      printf("%s: ERROR: VTP SPI-%d not open.\n",	\
	     __func__,x);				\
      return ERROR;					\
    }							\

int
vtpSPIOpen()
{
  int iid, ret;

  for(iid = 0; iid < nVTPSPIFD; iid++)
    {
      if(vtpSPIFD[iid] > 0)
	{
	  VTP_DBGN(VTP_DEBUG_INIT, "VTP SPI already opened.\n");
	  return OK;
	}

      VTP_DBGN(VTP_DEBUG_INIT, "open SPI device = %s\n", vtpSPIDev[iid]);
      vtpSPIFD[iid] = open(vtpSPIDev[iid], O_RDWR | O_SYNC);

      if(vtpSPIFD[iid] < 0)
	{
	  printf("%s: ERROR from open: %s (%d)\n",
		 __func__, strerror(errno), errno);
	  return ERROR;
	}

      /*
       * spi vtpSpiMode
       */
#define SETDEFAULT
#ifdef SETDEFAULT
      VTP_DBGN(VTP_DEBUG_INIT, "set SPI mode = 0x%x\n", vtpSpiMode[iid]);
      ret = ioctl(vtpSPIFD[iid], SPI_IOC_WR_MODE32, &vtpSpiMode[iid]);
      if (ret == -1)
        perror("can't set spi mode");
#endif
      VTP_DBGN(VTP_DEBUG_INIT, "get SPI mode\n");
      ret = ioctl(vtpSPIFD[iid], SPI_IOC_RD_MODE32, &vtpSpiMode[iid]);
      if (ret == -1)
	perror("can't get spi mode");

      /*
       * bits per word
       */
#ifdef SETDEFAULT
      VTP_DBGN(VTP_DEBUG_INIT, "set SPI bits per word = 0x%x\n",
	       (uint32_t) vtpSpiBits);
      ret = ioctl(vtpSPIFD[iid], SPI_IOC_WR_BITS_PER_WORD, &vtpSpiBits);
      if (ret == -1)
        perror("can't set bits per word");
#endif
      VTP_DBGN(VTP_DEBUG_INIT, "get SPI bits per word\n");
      ret = ioctl(vtpSPIFD[iid], SPI_IOC_RD_BITS_PER_WORD, &vtpSpiBits[iid]);
      if (ret == -1)
	perror("can't get bits per word");

      /*
       * max speed hz
       */
#ifdef SETDEFAULT
      VTP_DBGN(VTP_DEBUG_INIT, "set SPI max speed = 0x%x\n",
	       (uint32_t) vtpSpiSpeed);
      ret = ioctl(vtpSPIFD[iid], SPI_IOC_WR_MAX_SPEED_HZ, &vtpSpiSpeed);
      if (ret == -1)
        perror("can't set max speed hz");
#endif
      VTP_DBGN(VTP_DEBUG_INIT, "get SPI max speed\n");
      ret = ioctl(vtpSPIFD[iid], SPI_IOC_RD_MAX_SPEED_HZ, &vtpSpiSpeed[iid]);
      if (ret == -1)
	perror("can't get max speed hz");

      printf("%s: \n",__func__);
      printf("\tspi mode:      0x%x\n", vtpSpiMode[iid]);
      printf("\tbits per word: %d\n", vtpSpiBits[iid]);
      printf("\tmax speed:     %d Hz (%d KHz)\n",
	     vtpSpiSpeed[iid], vtpSpiSpeed[iid]/1000);
    }

  return OK;
}

int
vtpSPIClose()
{
  int iid;

  for(iid = 0; iid < nVTPSPIFD; iid++)
    {
      CHECKSPIID(iid);

      close(vtpSPIFD[iid]);
    }

  return OK;
}


void
vtpSpiTransfer(int id, uint8_t const *tx, uint8_t const *rx, size_t len)
{
  int lerrno;

  struct spi_ioc_transfer tr =
    {
      .tx_buf = (unsigned long)tx,
      .rx_buf = (unsigned long)rx,
      .len = len,
      .delay_usecs = vtpSpiDelay[id],
      .speed_hz = vtpSpiSpeed[id],
      .bits_per_word = vtpSpiBits[id],
    };

  if (vtpSpiMode[id] & SPI_TX_QUAD)
    tr.tx_nbits = 4;
  else if (vtpSpiMode[id] & SPI_TX_DUAL)
    tr.tx_nbits = 2;
  if (vtpSpiMode[id] & SPI_RX_QUAD)
    tr.rx_nbits = 4;
  else if (vtpSpiMode[id] & SPI_RX_DUAL)
    tr.rx_nbits = 2;
  if (!(vtpSpiMode[id] & SPI_LOOP))
    {
      if (vtpSpiMode[id] & (SPI_TX_QUAD | SPI_TX_DUAL))
	tr.rx_buf = 0;
      else if (vtpSpiMode[id] & (SPI_RX_QUAD | SPI_RX_DUAL))
	tr.tx_buf = 0;
    }

  if(ioctl(vtpSPIFD[id], SPI_IOC_MESSAGE(1), &tr) < 1)
    {
      lerrno = errno;
      printf("%s: ioctl ERROR %d: %s\n", __func__,
	     lerrno, strerror(lerrno));
      return;
    }

}

#ifdef DONTUSE

uint8_t
vtpSPIRead(int id, unsigned int addr)
{
  uint8_t rval = 0;
  uint8_t tx_buf[4], rx_buf[4];

  CHECKSPIID(id);

  tx_buf[0] = SI5341_CMD_SETADDR;
  tx_buf[1] = addr;
  tx_buf[2] = SI5341_CMD_RDDATA;
  tx_buf[3] = 0;

  vtpSpiTransfer(id, tx_buf, rx_buf, sizeof(tx_buf);

  return rval;
}

int
vtpSPIWrite(int id, unsigned int addr, unsigned int val)
{
  uint8_t tx_buf[4], rx_buf[4];
  CHECKSPIID(id);

  tx_buf[0] = SI5341_CMD_SETADDR;
  tx_buf[1] = addr;
  tx_buf[2] = SI5341_CMD_WRDATA;
  tx_buf[3] = 0;

  vtpSpiTransfer(id, tx_buf, rx_buf, sizeof(tx_buf);

  return OK;
}
#endif
