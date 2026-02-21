/*
 * File:
 *    vtpConfigTest.c
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
  char *rol_usrConfig = "none";

  vtpInitGlobals();
  vtpConfig("");
  if(strncasecmp(rol_usrConfig,"none",4))
    vtpConfig(rol_usrConfig);

  exit(0);
}
