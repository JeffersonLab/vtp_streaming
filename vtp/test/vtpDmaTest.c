/*
 * File:
 *    vtpLibTest.c
 *
 * Description:
 *    Test program for the VTP Library
 *
 *
 */

#if defined(Linux_armv7l)

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "vtpLib.h"

#define BUILD_TEST_LEN  100

int
main(int argc, char *argv[])
{
  int result, i;
  volatile unsigned int *buf;

  if(vtpCheckAddresses() == ERROR)
    exit(-1);

  int openmask = VTP_FPGA_OPEN;
  if(vtpOpen(openmask) != openmask)
    {
      printf("burp\n");
      goto CLOSE;
    }

  /* vtpZ7CfgLoad("../firmware/fe_vtp_hallb_z7.bin"); */

  /* vtpV7CfgStart(); */
  /* vtpV7CfgLoad("../firmware/fe_vtp_hallb_v7_ec.bin"); */
  /* vtpV7CfgEnd(); */

//  vtpInit(1);
  vtpInit(VTP_INIT_SKIP);
  vtpSetBlockLevel(1);
  vtpDmaMemOpen(1, 0x1000);
  vtpDmaInit(VTP_DMA_VTP);

  vtpEbReset();
  vtpEbBuildTestEvent(BUILD_TEST_LEN);      // BUILD_TEST_LEN 32bit word test event size
  buf = (volatile unsigned int *)vtpDmaMemGetLocalAddress(0);

  for(i=0; i<(BUILD_TEST_LEN); i++)
  {
    buf[i] = 0;
  }
  for(i=0; i<(BUILD_TEST_LEN); i++)
  {
    if(!(i%4))
      printf("\n%08X:", i*4);
    printf(" %08X", buf[i]);
  }
  printf("\n");

  vtpDmaStart(VTP_DMA_VTP, vtpDmaMemGetPhysAddress(0), 1000);
  result = vtpDmaWaitDone(VTP_DMA_VTP);
  printf("result = %d\n", result);

  buf = (volatile unsigned int *)vtpDmaMemGetLocalAddress(0);

  for(i=0; i<(result>>2); i++)
  {
    if(!(i%4))
      printf("\n%08X:", i*4);
    printf(" %08X", buf[i]);
  }

  printf("\n");
 CLOSE:
  vtpDmaMemClose();
  vtpClose(openmask);

  exit(0);
}

#else

main()
{
  return;
}

#endif
