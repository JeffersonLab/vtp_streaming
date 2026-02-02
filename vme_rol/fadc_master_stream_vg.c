/*************************************************************************
 *
 *  ti_master_list.c - Library of routines for readout and buffering of
 *                     events using a JLAB Trigger Interface V3 (TI) with
 *                     a Linux VME controller in CODA 3.0.
 *
 *                     This for a TI in Master Mode controlling multiple ROCs
 */

/* Event Buffer definitions */
#define MAX_EVENT_POOL     100
#define MAX_EVENT_LENGTH   1152*32      /* Size in Bytes */

/* Define maximum number of words in the event block
   MUST be less than MAX_EVENT_LENGTH/4   */
#define MAX_WORDS 200

/* Define TI Type (TI_MASTER or TI_SLAVE) */
#define TI_MASTER
/* EXTernal trigger source (e.g. front panel ECL input), POLL for available data */
#define TI_READOUT TI_READOUT_EXT_POLL
/* TI VME address, or 0 for Auto Initialize (search for TI by slot) */
#define TI_ADDR  0

/* Trigger mode only - comment out */
#define STREAMING_MODE

/* Enable FADC250 Config files */
#define FADC_USE_CONFIG_FILE

/* Measured longest fiber length in system */
#define FIBER_LATENCY_OFFSET 0x4A

/* Additional includes for pedestal generation and config file creation */
#include <string.h>         /* strerror, strcmp, strlen */
#include <errno.h>          /* errno */
#include <unistd.h>         /* gethostname, access */
#include <sys/utsname.h>    /* uname */
#include <sys/wait.h>       /* waitpid */
#include <ctype.h>          /* isalnum */

#include "dmaBankTools.h"   /* Macros for handling CODA banks */
#include "tiprimary_list.c" /* Source required for CODA readout lists using the TI */
#include "fadcLib.h"        /* library of FADC250 routines */
#include "sdLib.h"          /* VXS Signal Distribution board header */
#include "fadc250Config.h"  /* Support for reading FADC config files */

/* Define initial blocklevel and buffering level */
#define BLOCKLEVEL 1
#define BUFFERLEVEL 1
#define SYNC_INTERVAL 100000

/* FADC Library Variables */
extern int fadcA32Base, nfadc;
unsigned int fadcmask=0;

/* Program FADCs to get trigger/clock from SDC or VXS */
#define FADC_VXS

// TODO initialize all FADC slots (vg)
#define NFADC     2
/* Address of first fADC250 */
#define FADC_ADDR 0x180000
/* Increment address to find next fADC250 */
#define FADC_INCR 0x080000

/* Set a common FADC Threshhold for all channels */
#define FADC_THRESHOLD  100

#define FADC_WINDOW_LAT    375  /* 4ns tick */
#define FADC_WINDOW_WIDTH   90  /* 4ns tick */
#define FADC_MODE           1   /* Hall B modes 1-8  1= raw  2=integrated*/

/* for the calculation of maximum data words in the block transfer */
unsigned int MAXFADCWORDS=0;

/****************************************
 * HELPER FUNCTIONS FOR PEDESTAL GENERATION
 ****************************************/

/**
 * Get hostname and sanitize for use in filenames
 * Only allows [A-Za-z0-9._-], replaces others with '_'
 * Returns: 0 on success, -1 on failure
 */
static int get_sanitized_hostname(char *hostname_buf, size_t bufsize)
{
  int i, rc;
  struct utsname uts_info;

  if (!hostname_buf || bufsize == 0) return -1;

  /* Try gethostname first */
  rc = gethostname(hostname_buf, bufsize);
  if (rc != 0 || hostname_buf[0] == '\0')
  {
    /* Fallback to uname */
    if (uname(&uts_info) == 0)
    {
      strncpy(hostname_buf, uts_info.nodename, bufsize - 1);
      hostname_buf[bufsize - 1] = '\0';
    }
    else
    {
      printf("ERROR: Failed to determine hostname (gethostname and uname both failed)\n");
      return -1;
    }
  }

  /* Sanitize hostname: only allow [A-Za-z0-9._-] */
  for (i = 0; hostname_buf[i] != '\0'; i++)
  {
    if (!isalnum(hostname_buf[i]) && hostname_buf[i] != '.' &&
        hostname_buf[i] != '_' && hostname_buf[i] != '-')
    {
      hostname_buf[i] = '_';
    }
  }

  return 0;
}

/**
 * Parse rocname from first line of peds file
 * Expected format: "FADC250_CRATE <rocname>"
 * Returns: 0 on success, -1 on failure
 */
static int parse_rocname_from_peds(const char *peds_file, char *rocname_buf, size_t bufsize)
{
  FILE *fp;
  char line[512];
  char keyword[128];
  int rc;

  if (!peds_file || !rocname_buf || bufsize == 0) return -1;

  fp = fopen(peds_file, "r");
  if (!fp)
  {
    printf("ERROR: Cannot open peds file '%s' for reading: %s\n", peds_file, strerror(errno));
    return -1;
  }

  /* Read first line */
  if (fgets(line, sizeof(line), fp) == NULL)
  {
    printf("ERROR: Peds file '%s' is empty or cannot be read\n", peds_file);
    fclose(fp);
    return -1;
  }
  fclose(fp);

  /* Parse: FADC250_CRATE <rocname> */
  rc = sscanf(line, "%127s %255s", keyword, rocname_buf);
  if (rc != 2 || strcmp(keyword, "FADC250_CRATE") != 0)
  {
    printf("ERROR: First line of peds file '%s' does not match expected format 'FADC250_CRATE <rocname>'\n", peds_file);
    printf("ERROR: Got: '%s'\n", line);
    return -1;
  }

  /* Trim any trailing whitespace from rocname */
  int len = strlen(rocname_buf);
  while (len > 0 && isspace((unsigned char)rocname_buf[len-1]))
  {
    rocname_buf[--len] = '\0';
  }

  if (len == 0)
  {
    printf("ERROR: Parsed rocname from peds file is empty\n");
    return -1;
  }

  printf("INFO: Parsed rocname='%s' from peds file\n", rocname_buf);
  return 0;
}

/**
 * Generate vme_<rocname>.cnf configuration file
 * Follows template from vme_rocname.cnf and appends peds.txt content
 * Returns: 0 on success, -1 on failure
 */
static int generate_vme_config(const char *config_dir, const char *rocname, const char *peds_file)
{
  char vme_config_path[512];
  FILE *out_fp, *peds_fp;
  char line[1024];
  int line_count = 0;

  if (!config_dir || !rocname || !peds_file) return -1;

  /* Build output path */
  snprintf(vme_config_path, sizeof(vme_config_path), "%s/vme_%s.cnf", config_dir, rocname);

  printf("INFO: Generating VME config file: %s\n", vme_config_path);

  /* Open output file */
  out_fp = fopen(vme_config_path, "w");
  if (!out_fp)
  {
    printf("ERROR: Cannot create VME config file '%s': %s\n", vme_config_path, strerror(errno));
    return -1;
  }

  /* Write template header (based on vme_rocname.cnf) */
  fprintf(out_fp, "FADC250_CRATE all\n");
  fprintf(out_fp, "FADC250_SLOT all\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "#       channel:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15\n");
  fprintf(out_fp, "FADC250_ADC_MASK  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1\n");
  fprintf(out_fp, "FADC250_TRG_MASK  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# force readout of channel mask (i.e. ignore threshold for readout)\n");
  fprintf(out_fp, "FADC250_TET_IGNORE_MASK 0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "#modes=1 (raw sample), 9 ( pulse integral + Hi-res time), 10 (pulse integer + Hi-res time + raw)\n");
  fprintf(out_fp, "FADC250_MODE 1\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# number of ns back from trigger point.\n");
  fprintf(out_fp, "FADC250_W_OFFSET  1200\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# number of ns to include in trigger window.\n");
  fprintf(out_fp, "FADC250_W_WIDTH  800\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# time (units: ns) before threshold crossing to include in integral\n");
  fprintf(out_fp, "FADC250_NSB 8 \n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# time (units: ns) after threshold crossing to include in integral\n");
  fprintf(out_fp, "FADC250_NSA 60\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "FADC250_NPEAK 1\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# fadc channel hit threshold (adc channel)\n");
  fprintf(out_fp, "FADC250_TET 20\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# board DAC, one and the same for all 16 channels (DAC/mV)\n");
  fprintf(out_fp, "FADC250_DAC 3270 \n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# board Gains, same for all channels (MeV/channel)\n");
  fprintf(out_fp, "#FADC250_GAIN 0.500\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# Sparsification: 0=Bypassed (read all the channels for the detector), 1=Enabled (reads only the channels around the cluster)\n");
  fprintf(out_fp, "#FADC250_SPARSIFICATION 0\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# Accumulator scaler mode: 0=Default, TET based pulse integration, 1=Sum all samples\n");
  fprintf(out_fp, "#FADC250_ACCUMULATOR_SCALER_MODE_MASK 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n");
  fprintf(out_fp, "FADC250_CRATE end\n");
  fprintf(out_fp, "\n");

  /* Now append the entire peds.txt file content */
  peds_fp = fopen(peds_file, "r");
  if (!peds_fp)
  {
    printf("ERROR: Cannot open peds file '%s' for appending to VME config: %s\n", peds_file, strerror(errno));
    fclose(out_fp);
    return -1;
  }

  while (fgets(line, sizeof(line), peds_fp) != NULL)
  {
    fputs(line, out_fp);
    line_count++;
  }
  fclose(peds_fp);

  fclose(out_fp);

  printf("INFO: Successfully created VME config file '%s' (%d lines from peds appended)\n",
         vme_config_path, line_count);
  return 0;
}

/**
 * Generate vtp_<rocname>.cnf configuration file
 * Follows template from vtp_rocname.cnf
 * Returns: 0 on success, -1 on failure
 */
static int generate_vtp_config(const char *config_dir, const char *rocname)
{
  char vtp_config_path[512];
  FILE *out_fp;

  if (!config_dir || !rocname) return -1;

  /* Build output path */
  snprintf(vtp_config_path, sizeof(vtp_config_path), "%s/vtp_%s.cnf", config_dir, rocname);

  printf("INFO: Generating VTP config file: %s\n", vtp_config_path);

  /* Open output file */
  out_fp = fopen(vtp_config_path, "w");
  if (!out_fp)
  {
    printf("ERROR: Cannot create VTP config file '%s': %s\n", vtp_config_path, strerror(errno));
    return -1;
  }

  /* Write template content (based on vtp_rocname.cnf) */
  fprintf(out_fp, "VTP_CRATE %svtp\n", rocname);
  fprintf(out_fp, "\n");
  fprintf(out_fp, "###################################################\n");
  fprintf(out_fp, "# Firmware Configuration\n");
  fprintf(out_fp, "###################################################\n");
  fprintf(out_fp, "# Z7 FPGA firmware - Local aggregation and formatting\n");
  fprintf(out_fp, "VTP_FIRMWARE_Z7           fe_vtp_z7_streamingv3_ejfat_v5.bin\n");
  fprintf(out_fp, "# V7 FPGA firmware - FADC readout\n");
  fprintf(out_fp, "VTP_FIRMWARE_V7           fe_vtp_v7_fadc_streamingv3_ejfat.bin\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "###################################################\n");
  fprintf(out_fp, "# UDP Statistics/Sync Sender Configuration\n");
  fprintf(out_fp, "###################################################\n");
  fprintf(out_fp, "# Destination host for 1Hz sync packets (hostname or IP address)\n");
  fprintf(out_fp, "# Default: 129.57.29.231 (indra-s2 for forwarding to JLAB EJFAT LB)\n");
  fprintf(out_fp, "VTP_STATS_HOST            129.57.29.231\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# Destination UDP port for sync packets (1-65535)\n");
  fprintf(out_fp, "VTP_STATS_PORT            19531\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# Stream instance to sample for statistics (0-3)\n");
  fprintf(out_fp, "VTP_STATS_INST            0\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# Sync packet length in bytes (typically 28)\n");
  fprintf(out_fp, "VTP_SYNC_PKT_LEN          28\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "###################################################\n");
  fprintf(out_fp, "# Network Streaming Configuration\n");
  fprintf(out_fp, "###################################################\n");
  fprintf(out_fp, "# Number of VTP network stream connections (1-4)\n");
  fprintf(out_fp, "VTP_NUM_CONNECTIONS       1\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# Network transport mode: 0=TCP, 1=UDP\n");
  fprintf(out_fp, "VTP_NET_MODE              1\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# Enable EJFAT packet headers: 0=disabled, 1=enabled (UDP only)\n");
  fprintf(out_fp, "VTP_ENABLE_EJFAT          1\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "# Base local port number for connections (1-65535)\n");
  fprintf(out_fp, "VTP_LOCAL_PORT            10001\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "#        slot: 10 13  9 14  8 15  7 16  6 17  5 18  4 19  3 20\n");
  fprintf(out_fp, "#     payload:  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16\n");
  fprintf(out_fp, "VTP_PAYLOAD_EN  0  0  0  0  0  0  0  0  0  0  0  0  1  0  1  0\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "VTP_STREAMING_ROCID       0\n");
  fprintf(out_fp, "VTP_STREAMING_NFRAME_BUF  1000\n");
  fprintf(out_fp, "VTP_STREAMING_FRAMELEN    65536\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "###################################################\n");
  fprintf(out_fp, "#VTP FADC Streaming event builder #0 (slots 3-10) #\n");
  fprintf(out_fp, "###################################################\n");
  fprintf(out_fp, "VTP_STREAMING             0\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "VTP_STREAMING_MAC         0xCE 0xBA 0xF0 0x03 0x00 0x9d\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "VTP_STREAMING_NSTREAMS    1\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "VTP_STREAMING_IPADDR      129  57 69 14\n");
  fprintf(out_fp, "VTP_STREAMING_SUBNET      255 255 255   0\n");
  fprintf(out_fp, "VTP_STREAMING_GATEWAY     129  57 69   1   \n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "VTP_STREAMING_DESTIP      129.57.177.3\n");
  fprintf(out_fp, "VTP_STREAMING_DESTIPPORT  19522\n");
  fprintf(out_fp, "VTP_STREAMING_LOCALPORT   10001\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "VTP_STREAMING_CONNECT     1\n");
  fprintf(out_fp, "\n");

  fclose(out_fp);

  printf("INFO: Successfully created VTP config file '%s'\n", vtp_config_path);
  return 0;
}

/****************************************
 *  DOWNLOAD
 ****************************************/
void
rocDownload()
{
  int stat;
  unsigned int ival = SYNC_INTERVAL;
  unsigned short iflag;
  int ifa;

  printf("***** rocDownload() ENTERED - START OF FUNCTION *****\n");
  fflush(stdout);

  /* ===================================================================
   * PEDESTAL GENERATION AND CONFIG FILE CREATION
   * =================================================================== */
  {
    char hostname[256];
    char peds_file[512];
    char rocname[256];
    char command[1024];
    const char *coda_env, *coda_data_env, *coda_config_env;
    pid_t pid;
    int status;

    /* Get hostname */
    if (get_sanitized_hostname(hostname, sizeof(hostname)) != 0)
    {
      printf("ERROR: rocDownload - Failed to get hostname\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }
    printf("INFO: Hostname: %s\n", hostname);

    /* Get environment variables */
    coda_env = getenv("CODA");
    coda_data_env = getenv("CODA_DATA");
    coda_config_env = getenv("CODA_CONFIG");

    if (!coda_env || !*coda_env)
    {
      printf("ERROR: rocDownload - CODA environment variable not set\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }
    if (!coda_data_env || !*coda_data_env)
    {
      printf("ERROR: rocDownload - CODA_DATA environment variable not set\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }
    if (!coda_config_env || !*coda_config_env)
    {
      printf("ERROR: rocDownload - CODA_CONFIG environment variable not set\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }

    /* Build paths */
    snprintf(peds_file, sizeof(peds_file), "%s/%s_peds.txt", coda_data_env, hostname);
    snprintf(command, sizeof(command), "%s/linuxvme/fadc-peds/fadc250peds %s",
             coda_env, peds_file);

    printf("INFO: Executing pedestal generation command:\n");
    printf("INFO:   %s\n", command);

    /* Execute command using fork/exec for better control */
    pid = fork();
    if (pid < 0)
    {
      printf("ERROR: rocDownload - fork() failed: %s\n", strerror(errno));
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }
    else if (pid == 0)
    {
      /* Child process - execute command */
      execl("/bin/sh", "sh", "-c", command, (char *)NULL);
      /* If execl returns, it failed */
      printf("ERROR: rocDownload - execl() failed: %s\n", strerror(errno));
      _exit(127);
    }
    else
    {
      /* Parent process - wait for child */
      if (waitpid(pid, &status, 0) < 0)
      {
        printf("ERROR: rocDownload - waitpid() failed: %s\n", strerror(errno));
        printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
        return;
      }

      if (WIFEXITED(status))
      {
        int exit_code = WEXITSTATUS(status);
        if (exit_code != 0)
        {
          printf("ERROR: rocDownload - fadc250peds command failed with exit code %d\n", exit_code);
          printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
          return;
        }
        printf("INFO: fadc250peds command completed successfully\n");
      }
      else if (WIFSIGNALED(status))
      {
        printf("ERROR: rocDownload - fadc250peds command terminated by signal %d\n", WTERMSIG(status));
        printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
        return;
      }
      else
      {
        printf("ERROR: rocDownload - fadc250peds command terminated abnormally\n");
        printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
        return;
      }
    }

    /* Verify peds file exists */
    if (access(peds_file, R_OK) != 0)
    {
      printf("ERROR: rocDownload - Generated peds file '%s' not found or not readable\n", peds_file);
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }

    /* Parse rocname from peds file */
    if (parse_rocname_from_peds(peds_file, rocname, sizeof(rocname)) != 0)
    {
      printf("ERROR: rocDownload - Failed to parse rocname from peds file\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }

    /* Generate vme_<rocname>.cnf */
    if (generate_vme_config(coda_config_env, rocname, peds_file) != 0)
    {
      printf("ERROR: rocDownload - Failed to generate VME config file\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }

    /* Generate vtp_<rocname>.cnf */
    if (generate_vtp_config(coda_config_env, rocname) != 0)
    {
      printf("ERROR: rocDownload - Failed to generate VTP config file\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }

    printf("INFO: ============================================\n");
    printf("INFO: Pedestal generation and config creation complete\n");
    printf("INFO:   Hostname: %s\n", hostname);
    printf("INFO:   ROC name: %s\n", rocname);
    printf("INFO:   Peds file: %s\n", peds_file);
    printf("INFO:   VME config: %s/vme_%s.cnf\n", coda_config_env, rocname);
    printf("INFO:   VTP config: %s/vtp_%s.cnf\n", coda_config_env, rocname);
    printf("INFO: ============================================\n");
  }
  /* ===================================================================
   * END OF PEDESTAL GENERATION AND CONFIG FILE CREATION
   * =================================================================== */

  printf("***** PEDESTAL GENERATION BLOCK COMPLETED - CONTINUING WITH FADC INIT *****\n");
  fflush(stdout);


  /* Setup Address and data modes for DMA transfers
   *   
   *  vmeDmaConfig(addrType, dataType, sstMode);
   *
   *  addrType = 0 (A16)    1 (A24)    2 (A32)
   *  dataType = 0 (D16)    1 (D32)    2 (BLK32) 3 (MBLK) 4 (2eVME) 5 (2eSST)
   *  sstMode  = 0 (SST160) 1 (SST267) 2 (SST320)
   */
  vmeDmaConfig(2,5,1); 

  /* Define BLock Level */
  blockLevel = BLOCKLEVEL;
  bufferLevel = BUFFERLEVEL;


  /*****************
   *   TI SETUP
   *****************/

  /* 
   * Set Trigger source 
   *    For the TI-Master, valid sources:
   *      TI_TRIGGER_FPTRG     2  Front Panel "TRG" Input
   *      TI_TRIGGER_TSINPUTS  3  Front Panel "TS" Inputs
   *      TI_TRIGGER_TSREV2    4  Ribbon cable from Legacy TS module
   *      TI_TRIGGER_PULSER    5  TI Internal Pulser (Fixed rate and/or random)
   */
  tiSetTriggerSource(TI_TRIGGER_TSINPUTS); /* TS Inputs enabled */

  /* Enable set specific TS input bits (1-6) */
  tiEnableTSInput( TI_TSINPUT_1 | TI_TSINPUT_2 );

  /* Load the trigger table that associates 
   *  pins 21/22 | 23/24 | 25/26 : trigger1
   *  pins 29/30 | 31/32 | 33/34 : trigger2
   */
  tiLoadTriggerTable(0);

  tiSetTriggerHoldoff(1,10,0);
  tiSetTriggerHoldoff(2,10,0);

  /* Set the sync delay width to 0x40*32 = 2.048us */
  tiSetSyncDelayWidth(0x54, 0x40, 1);
  
  /* Set initial number of events per block */
  tiSetBlockLevel(blockLevel);

  /* Set Trigger Buffer Level */
  tiSetBlockBufferLevel(BUFFERLEVEL);

  /* Init the SD library so we can get status info */
  stat = sdInit(0);
  if(stat==0) 
    {
      tiSetBusySource(TI_BUSY_SWB,1);
      sdSetActiveVmeSlots(0);
      sdStatus(0);
    }
  else
    { /* No SD or the TI is not in the Proper slot */
      tiSetBusySource(TI_BUSY_LOOPBACK,1);
    }


  /* Do FADC Programming in Download to support Streaming when the TI is is master mode */

#ifndef FADC_VXS  
  /* Init FADC Library and modules here id using a SDC board in a non-VXS crate */
  iflag = 0xea00; /* SDC Board address A16 */
  iflag |= FA_INIT_EXT_SYNCRESET;  /* Front panel sync-reset */
  iflag |= FA_INIT_FP_TRIG;  /* Front Panel Input trigger source */
  iflag |= FA_INIT_FP_CLKSRC;  /* Internal 250MHz Clock source */
#else
  /* If this is in a VXS crate Get CLK, TRIG, and Sync Reset from VXS */
  iflag = 0x25;
#endif

  /* Initialize FADC library */
  fadcA32Base = 0x09000000;
  vmeSetQuietFlag(0);
  faInit(FADC_ADDR, FADC_INCR, NFADC, (iflag|FA_INIT_SKIP_FIRMWARE_CHECK));
  vmeSetQuietFlag(1);

  /* create a slot mask */
  for(ifa=0; ifa < nfadc; ifa++) {
    fadcmask |= (1<<faSlot(ifa));
  }

#ifdef FADC_USE_CONFIG_FILE
  /* Read in a FADC250 Config file */
  stat = fadc250Config(rol->usrConfig);
  //stat = fadc250Config("/polaris/home/tpex/desy/fadc250/tpex_0.cnf");
  if(stat<0) {
    printf("ERROR: Reading FADC250 Config file FAILED\n");
  }else{
    printf("INFO: Read in FADC250 Config file\n");  
  }

  /* Enable Bus Error and SyncReset for readout */
  for(ifa=0; ifa < nfadc; ifa++) {
    faSoftReset(faSlot(ifa),0);
    faResetTriggerCount(faSlot(ifa));
    faEnableSyncReset(faSlot(ifa));
    faEnableBusError(faSlot(ifa));
  }

#else
  /* Program/Init VME Modules Here */
  for(ifa=0; ifa < nfadc; ifa++) 
    {

      faSoftReset(faSlot(ifa),0);
      faResetTriggerCount(faSlot(ifa));

      faEnableSyncReset(faSlot(ifa));

      faEnableBusError(faSlot(ifa));

      /* Set input DAC level - 3250 basically corresponds to 0 shift in the baseline. */
      faSetDAC(faSlot(ifa), 3250, 0);

      /*  Set All channel thresholds the same */
      faSetThreshold(faSlot(ifa), FADC_THRESHOLD, 0xffff);
  
      /*********************************************************************************
       * faSetProcMode(int id, int pmode, unsigned int PL, unsigned int PTW, 
       *    int NSB, unsigned int NSA, unsigned int NP, 
       *    unsigned int NPED, unsigned int MAXPED, unsigned int NSAT);
       *
       *  id    : fADC250 Slot number
       *  pmode : Processing Mode
       *          9 - Pulse Parameter (ped, sum, time)
       *         10 - Debug Mode (9 + Raw Samples)
       *    PL : Window Latency
       *   PTW : Window Width

       *   NSB : Number of samples before pulse over threshold
       *   NSA : Number of samples after pulse over threshold
       *    NP : Number of pulses processed per window
       *  BANK : (Hall B option - replaces NPED,MAXPED,NSAT)
       *  NPED : Number of samples to sum for pedestal (4)
       *MAXPED : Maximum value of sample to be included in pedestal sum (250)
       *  NSAT : Number of consecutive samples over threshold for valid pulse (2)
       */
      faSetProcMode(faSlot(ifa),
		    FADC_MODE,   /* Processing Mode */
		    FADC_WINDOW_LAT, /* PL */
		    FADC_WINDOW_WIDTH,  /* PTW */
		    5,   /* NSB */
		    20,  /* NSA */
		    1,   /* NP */
		    0    /* BANK */
		    );

    }
#endif


  /* Print status for FADCS*/
#ifndef FADC_VXS
  faSDC_Status(0);
#endif
  faGStatus(0);


  tiStatus(0);

  printf("***** rocDownload: User Download Executed *****\n");
  fflush(stdout);

}

/****************************************
 *  PRESTART
 ****************************************/
void
rocPrestart()
{

  int stat;
  int islot;
  unsigned int ival = SYNC_INTERVAL;

  /* Unlock the VME Mutex */
  vmeBusUnlock();


#ifndef STREAMING_MODE
  /* Set number of events per block (broadcasted to all connected TI Slaves)*/
  tiSetBlockLevel(blockLevel);
  printf("rocPrestart: Block Level set to %d\n",blockLevel);

  /* Reset Active ROC Masks on all TD modules */
  tiTriggerReadyReset();

  /* Set Sync Event Interval  (0 disables sync events, max 65535) */
  tiSetSyncEventInterval(ival);
  printf("rocPrestart: Set Sync interval to %d Blocks\n",ival);
#endif

  /* Program the SD to look for Busy from the Active FADC slots */
  if(fadcmask) {
    printf("rocPrestart: Set Active Busy mask for SD (0x%06x)\n",fadcmask);
    sdSetActiveVmeSlots(fadcmask);
  }

  sdStatus(0);
  tiStatus(0); 


  printf("rocPrestart: User Prestart Executed\n");

}

/****************************************
 *  GO
 ****************************************/
void
rocGo()
{
  int islot;
  unsigned int tmask;

  /* Get the current Block Level */
  blockLevel = tiGetCurrentBlockLevel();
  printf("rocGo: Block Level set to %d\n",blockLevel);

  /* Enable Slave Ports that have indicated they are active */
  tiResetSlaveConfig();
  tmask = tiGetTrigSrcEnabledFiberMask();
  printf("%s: TI Source Enable Mask = 0x%x\n", __func__, tmask);
  if (tmask != 0)
    tiAddSlaveMask(tmask);

  /* Enable/Set Block Level on modules, if needed, here */
  faGSetBlockLevel(blockLevel);

  if(FADC_MODE == 9)
    MAXFADCWORDS = 2 + 4 + blockLevel * 8;
  else /* FADC_MODE == 10 */
    /* MAXFADCWORDS = 2 + 4 + blockLevel * (8 + FADC_WINDOW_WIDTH/2); */
    MAXFADCWORDS = 2000;
  
  /*  Enable FADC */
  faGEnable(0, 0);


  /* Example: How to start internal pulser trigger */
#ifdef INTRANDOMPULSER
  /* Enable Random at rate 500kHz/(2^7) = ~3.9kHz */
  tiSetRandomTrigger(1,0x7);
#elif defined (INTFIXEDPULSER)
  /* Enable fixed rate with period (ns) 120 +30*700*(1024^0) = 21.1 us (~47.4 kHz)
     - Generated 1000 times */
  tiSoftTrig(1,1000,700,0);
#endif
}

/****************************************
 *  END
 ****************************************/
void
rocEnd()
{
  int islot;

  /* Example: How to stop internal pulser trigger */
#ifdef INTRANDOMPULSER
  /* Disable random trigger */
  tiDisableRandomTrigger();
#elif defined (INTFIXEDPULSER)
  /* Disable Fixed Rate trigger */
  tiSoftTrig(1,0,700,0);
#endif

  /* FADC Disable */
  faGDisable(0);

  /*Make sure Stream is disabled - this should be done in tiprimary_list.c */
  //    tiUserSyncReset(1,0);

  /* FADC Event status - Is all data read out */
  faGStatus(0);

  //tiStatus(0);

  printf("rocEnd: Ended after %d blocks\n",tiGetIntCount());
  
}

/****************************************
 *  TRIGGER
 ****************************************/
void
rocTrigger(int arg)
{
  int ii, islot;
  int stat, ifa, nwords, blockError, dCnt, len=0, idata;
  unsigned int val;
  unsigned int *start;
  unsigned int datascan, scanmask, roCount;

  /* Set TI output 1 high for diagnostics */
  tiSetOutputPort(1,0,0,0);

  /* Readout the trigger block from the TI 
     Trigger Block MUST be reaodut first */
  dCnt = tiReadTriggerBlock(dma_dabufp);

  if(dCnt<=0)
    {
      printf("No TI Trigger data or error.  dCnt = %d\n",dCnt);
    }
  else
    { /* TI Data is already in a bank structure.  Bump the pointer */
      dma_dabufp += dCnt;
    }


  /* fADC250 Readout */
  BANKOPEN(3,BT_UI4,blockLevel);

  /* Mask of initialized modules */
  scanmask = faScanMask();
  /* Check scanmask for block ready up to 100 times */
  datascan = faGBready(scanmask, 100); 
  stat = (datascan == scanmask);

  if(stat) 
    {
      for(ifa = 0; ifa < nfadc; ifa++)
	{
	  nwords = faReadBlock(faSlot(ifa), dma_dabufp, MAXFADCWORDS, 1);
	  
	  /* Check for ERROR in block read */
	  blockError = faGetBlockError(1);
	  
	  if(blockError) 
	    {
	      printf("ERROR: Slot %d: in transfer (event = %d), nwords = 0x%x\n",
		     faSlot(ifa), roCount, nwords);

	      if(nwords > 0)
		dma_dabufp += nwords;
	    } 
	  else 
	    {
	      dma_dabufp += nwords;
	    }
	}
    }
  else 
    {
      printf("ERROR: Event %d: Datascan != Scanmask  (0x%08x != 0x%08x)\n",
	     roCount, datascan, scanmask);
      *dma_dabufp++ = 0xda0bad003;
      *dma_dabufp++ = roCount;
      *dma_dabufp++ = datascan;
      *dma_dabufp++ = scanmask;
    }
  BANKCLOSE;


  /* Set TI output 0 low */
  tiSetOutputPort(0,0,0,0);

}

void
rocCleanup()
{
  int islot=0;

  printf("%s: Reset all Modules\n",__FUNCTION__);
  
  /* Reset the FADCs */
  faGReset(1);

}
