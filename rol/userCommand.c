/* userCommand.c
 *
 *  Library for use with ROC readout lists to add user specific commands
 *  that can be executed during the Run Control transitions
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>


#define DEBUG 1  /* if 1, print some debug statements */



/* example function */
int 
rolCommand()
{  

  printf(" *** Executing rolCommand (return Ok) ***\n");
  vtpStats();

  return OK;
}

int 
rolCommand2()
{  

  printf(" *** Executing rolCommand2 (return Error) ***\n");
  

  return -1;
}

