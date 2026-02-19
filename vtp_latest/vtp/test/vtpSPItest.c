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
  
  if(vtpOpen(VTP_SPI_OPEN) == ERROR)
    {
      printf("vtpOpen not OK\n");
      goto CLOSE;
    }

  si5341_Test();
  
 CLOSE:
  vtpClose(VTP_SPI_OPEN);

  exit(0);
}

