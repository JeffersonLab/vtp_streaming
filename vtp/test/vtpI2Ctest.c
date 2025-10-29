/*
 * File:
 *    vtpI2CTest.c
 *
 * Description:
 *    Test program for the VTP Library
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "vtpLib.h"

extern int nfadc;

int 
main(int argc, char *argv[]) 
{

  if(vtpCheckAddresses() == ERROR)
    exit(-1);
  
  if(vtpOpen(VTP_I2C_OPEN) == ERROR)
    {
      printf("vtpOpen not OK\n");
      goto CLOSE;
    }

  /* Select Channel 0 */
  /* vtpI2CWrite8(0, 0); */

  printf("Read: 0x%04x\n", vtpI2CRead16(0x40, 0x8E));

  
 CLOSE:
  vtpClose(VTP_I2C_OPEN);

  exit(0);
}

