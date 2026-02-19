/*
 * File:
 *    vtpFirmwareLoad.c
 *
 * Description:
 *    JLab VTP V7 Firmware Load
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "vtpLib.h"

char *progName;

void Usage();

int
main(int argc, char *argv[])
{

  int stat=0;
  char fwConfigFilename[1000];
  char hostname[100], hostfile[100], z7file[100], v7file[100];
  FILE *f;

  printf("\nJLAB VTP firmware load\n");
  printf("----------------------------\n");

  progName = argv[0];

  if(argc==2)
    {
      strcpy(fwConfigFilename, argv[1]);
    }
  else if (argc > 2)
    {
      printf(" ERROR: too many arguments (%d))\n", argc-1);
      Usage();
      exit(-1);
    }
  else
    {
      sprintf(fwConfigFilename, "%s/firmwares/vtp_firmware.txt", getenv(VTP_CONFIG_GET_ENV));
    }

  f = fopen(fwConfigFilename, "r");
  if(!f)
  {
    perror("fopen");
    printf("ERROR - failed to open: %s\n",
	   fwConfigFilename);
    return -1;
  }

  if(gethostname(hostname,sizeof(hostname)) < 0)
    {
      perror("gethostname");
      exit(-1);
    }

  /* Strip out the domain if it is included */
  int i;
  for(i=0; i<strlen(hostname); i++)
    {
      if(hostname[i] == '.')
	{
	  hostname[i] = '\0';
	  break;
	}
    }

  char buf[1000];
  while(!feof(f))
    {
      fgets(buf, sizeof(buf), f);

      if(sscanf(buf, "%100s %100s %100s", hostfile, z7file, v7file) >= 3)
	{
	  if(!strcmp(hostname, hostfile))
	    {
	      printf("Found host %s, z7file: %s, v7file: %s\n",
		     hostfile, z7file, v7file);
	      break;
	    }
	}
    }
  fclose(f);


  /* Initialize library */
  stat = vtpOpen(VTP_FPGA_OPEN | VTP_I2C_OPEN | VTP_SPI_OPEN);
  if(stat < 0)
    {
      printf(" Unable to initialize VTP library.\n");
      goto CLOSE;
    }

  /* Load firmware here */
  sprintf(buf, "%s/firmwares/%s", getenv(VTP_CONFIG_GET_ENV), z7file);
  if(vtpZ7CfgLoad(buf) != OK)
    {
      printf("Z7 programming failed...\n");
      return -1;
    }

  sprintf(buf, "%s/firmwares/%s", getenv(VTP_CONFIG_GET_ENV), v7file);
  if(vtpV7CfgLoad(buf) != OK)
    {
      printf("V7 programming failed...\n");
      return -1;
    }


  ltm4676_print_status();

  if(vtpInit(VTP_INIT_CLK_INT))
  {
    printf("VTP Init failed - exiting...\n");
    goto CLOSE;
  }

 CLOSE:
  vtpClose(VTP_FPGA_OPEN | VTP_I2C_OPEN | VTP_SPI_OPEN);

  exit(0);
}

void
Usage()
{
  /* Two arguments:
      0: program name
      1: Name of Text file with format:

      hostname    [z7 firmware filename]   [v7 firmware filename]

      e.g.

      hallavtp1	  fe_vtp_hallb_z7.bin      fe_vtp_halla_v7_compton.bin

  */

  printf("\n");
  printf("%s <firmware .bin file>\n",progName);
  printf("\n");

}

/*
  Local Variables:
  compile-command: "make -k vtpFirmwareLoad"
  End:
 */
