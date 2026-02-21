/*
 * File:
 *    vtpLibTest.c
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
  
  if(vtpOpen(VTP_I2C_OPEN) != OK)
    goto CLOSE;
  
 CLOSE:
  vtpClose(VTP_I2C_OPEN);

  exit(0);
}

