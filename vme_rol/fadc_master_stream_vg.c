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
 * USER CONFIG PARSING AND FILE GENERATION
 ****************************************/

/**
 * Structure to hold all parameters parsed from user configuration file.
 * This includes both VME/FADC parameters and VTP streaming parameters.
 */
typedef struct {
  /* VME/FADC parameters */
  int fadc_mode;
  int fadc_w_offset;
  int fadc_w_width;
  int fadc_nsb;
  int fadc_nsa;
  int fadc_npeak;
  int fadc_tet;
  int fadc_dac;
  int fadc_adc_mask[16];
  int fadc_trg_mask[16];
  int fadc_tet_ignore_mask[16];

  /* VTP streaming parameters */
  int vtp_streaming_rocid;
  int vtp_streaming_nframe_buf;
  int vtp_streaming_framelen;
  int vtp_streaming;
  unsigned char vtp_streaming_mac[6];
  int vtp_streaming_nstreams;
  unsigned char vtp_streaming_ipaddr[4];
  unsigned char vtp_streaming_subnet[4];
  unsigned char vtp_streaming_gateway[4];
  char vtp_streaming_destip[64];
  int vtp_streaming_destipport;
  int vtp_streaming_localport;
  int vtp_streaming_connect;

  /* Validation flags */
  int have_vtp_rocid;
  int have_vtp_nframe_buf;
  int have_vtp_framelen;
  int have_vtp_streaming;
  int have_vtp_mac;
  int have_vtp_nstreams;
  int have_vtp_ipaddr;
  int have_vtp_subnet;
  int have_vtp_gateway;
  int have_vtp_destip;
  int have_vtp_destipport;
  int have_vtp_localport;
  int have_vtp_connect;
} user_config_params_t;

/**
 * Initialize config parameters to defaults
 */
static void init_config_params(user_config_params_t *params)
{
  int i;
  if (!params) return;

  memset(params, 0, sizeof(user_config_params_t));

  /* Set default VME/FADC parameters */
  params->fadc_mode = 1;
  params->fadc_w_offset = 1200;
  params->fadc_w_width = 800;
  params->fadc_nsb = 8;
  params->fadc_nsa = 60;
  params->fadc_npeak = 1;
  params->fadc_tet = 20;
  params->fadc_dac = 3270;

  for (i = 0; i < 16; i++) {
    params->fadc_adc_mask[i] = 1;
    params->fadc_trg_mask[i] = 0;
    params->fadc_tet_ignore_mask[i] = 0;
  }

  /* VTP parameters - no defaults, must be specified in config */
}

/**
 * Parse user configuration file to extract both VME and VTP parameters.
 * Returns: 0 on success, -1 on failure
 */
static int parse_user_config(const char *config_file, user_config_params_t *params)
{
  FILE *fp;
  char line[1024];
  char keyword[256];
  int line_num = 0;

  if (!config_file || !params) {
    printf("ERROR: parse_user_config - invalid arguments\n");
    return -1;
  }

  printf("INFO: Parsing user config file: %s\n", config_file);

  fp = fopen(config_file, "r");
  if (!fp) {
    printf("ERROR: Cannot open user config file '%s': %s\n", config_file, strerror(errno));
    return -1;
  }

  while (fgets(line, sizeof(line), fp) != NULL) {
    line_num++;

    /* Skip comments and blank lines */
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

    /* Parse keyword */
    if (sscanf(line, "%255s", keyword) != 1) continue;

    /* VME/FADC parameters */
    if (strcmp(keyword, "FADC250_MODE") == 0) {
      sscanf(line, "%*s %d", &params->fadc_mode);
      printf("INFO:   FADC250_MODE = %d\n", params->fadc_mode);
    }
    else if (strcmp(keyword, "FADC250_W_OFFSET") == 0) {
      sscanf(line, "%*s %d", &params->fadc_w_offset);
      printf("INFO:   FADC250_W_OFFSET = %d\n", params->fadc_w_offset);
    }
    else if (strcmp(keyword, "FADC250_W_WIDTH") == 0) {
      sscanf(line, "%*s %d", &params->fadc_w_width);
      printf("INFO:   FADC250_W_WIDTH = %d\n", params->fadc_w_width);
    }
    else if (strcmp(keyword, "FADC250_NSB") == 0) {
      sscanf(line, "%*s %d", &params->fadc_nsb);
      printf("INFO:   FADC250_NSB = %d\n", params->fadc_nsb);
    }
    else if (strcmp(keyword, "FADC250_NSA") == 0) {
      sscanf(line, "%*s %d", &params->fadc_nsa);
      printf("INFO:   FADC250_NSA = %d\n", params->fadc_nsa);
    }
    else if (strcmp(keyword, "FADC250_NPEAK") == 0) {
      sscanf(line, "%*s %d", &params->fadc_npeak);
      printf("INFO:   FADC250_NPEAK = %d\n", params->fadc_npeak);
    }
    else if (strcmp(keyword, "FADC250_TET") == 0) {
      sscanf(line, "%*s %d", &params->fadc_tet);
      printf("INFO:   FADC250_TET = %d\n", params->fadc_tet);
    }
    else if (strcmp(keyword, "FADC250_DAC") == 0) {
      sscanf(line, "%*s %d", &params->fadc_dac);
      printf("INFO:   FADC250_DAC = %d\n", params->fadc_dac);
    }
    else if (strcmp(keyword, "FADC250_ADC_MASK") == 0) {
      sscanf(line, "%*s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
             &params->fadc_adc_mask[0], &params->fadc_adc_mask[1],
             &params->fadc_adc_mask[2], &params->fadc_adc_mask[3],
             &params->fadc_adc_mask[4], &params->fadc_adc_mask[5],
             &params->fadc_adc_mask[6], &params->fadc_adc_mask[7],
             &params->fadc_adc_mask[8], &params->fadc_adc_mask[9],
             &params->fadc_adc_mask[10], &params->fadc_adc_mask[11],
             &params->fadc_adc_mask[12], &params->fadc_adc_mask[13],
             &params->fadc_adc_mask[14], &params->fadc_adc_mask[15]);
    }
    else if (strcmp(keyword, "FADC250_TRG_MASK") == 0) {
      sscanf(line, "%*s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
             &params->fadc_trg_mask[0], &params->fadc_trg_mask[1],
             &params->fadc_trg_mask[2], &params->fadc_trg_mask[3],
             &params->fadc_trg_mask[4], &params->fadc_trg_mask[5],
             &params->fadc_trg_mask[6], &params->fadc_trg_mask[7],
             &params->fadc_trg_mask[8], &params->fadc_trg_mask[9],
             &params->fadc_trg_mask[10], &params->fadc_trg_mask[11],
             &params->fadc_trg_mask[12], &params->fadc_trg_mask[13],
             &params->fadc_trg_mask[14], &params->fadc_trg_mask[15]);
    }
    else if (strcmp(keyword, "FADC250_TET_IGNORE_MASK") == 0) {
      sscanf(line, "%*s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
             &params->fadc_tet_ignore_mask[0], &params->fadc_tet_ignore_mask[1],
             &params->fadc_tet_ignore_mask[2], &params->fadc_tet_ignore_mask[3],
             &params->fadc_tet_ignore_mask[4], &params->fadc_tet_ignore_mask[5],
             &params->fadc_tet_ignore_mask[6], &params->fadc_tet_ignore_mask[7],
             &params->fadc_tet_ignore_mask[8], &params->fadc_tet_ignore_mask[9],
             &params->fadc_tet_ignore_mask[10], &params->fadc_tet_ignore_mask[11],
             &params->fadc_tet_ignore_mask[12], &params->fadc_tet_ignore_mask[13],
             &params->fadc_tet_ignore_mask[14], &params->fadc_tet_ignore_mask[15]);
    }
    /* VTP streaming parameters */
    else if (strcmp(keyword, "VTP_STREAMING_ROCID") == 0) {
      sscanf(line, "%*s %d", &params->vtp_streaming_rocid);
      params->have_vtp_rocid = 1;
      printf("INFO:   VTP_STREAMING_ROCID = %d\n", params->vtp_streaming_rocid);
    }
    else if (strcmp(keyword, "VTP_STREAMING_NFRAME_BUF") == 0) {
      sscanf(line, "%*s %d", &params->vtp_streaming_nframe_buf);
      params->have_vtp_nframe_buf = 1;
      printf("INFO:   VTP_STREAMING_NFRAME_BUF = %d\n", params->vtp_streaming_nframe_buf);
    }
    else if (strcmp(keyword, "VTP_STREAMING_FRAMELEN") == 0) {
      sscanf(line, "%*s %d", &params->vtp_streaming_framelen);
      params->have_vtp_framelen = 1;
      printf("INFO:   VTP_STREAMING_FRAMELEN = %d\n", params->vtp_streaming_framelen);
    }
    else if (strcmp(keyword, "VTP_STREAMING") == 0) {
      sscanf(line, "%*s %d", &params->vtp_streaming);
      params->have_vtp_streaming = 1;
      printf("INFO:   VTP_STREAMING = %d\n", params->vtp_streaming);
    }
    else if (strcmp(keyword, "VTP_STREAMING_MAC") == 0) {
      unsigned int mac[6];
      sscanf(line, "%*s %x %x %x %x %x %x",
             &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
      params->vtp_streaming_mac[0] = mac[0];
      params->vtp_streaming_mac[1] = mac[1];
      params->vtp_streaming_mac[2] = mac[2];
      params->vtp_streaming_mac[3] = mac[3];
      params->vtp_streaming_mac[4] = mac[4];
      params->vtp_streaming_mac[5] = mac[5];
      params->have_vtp_mac = 1;
      printf("INFO:   VTP_STREAMING_MAC = %02X:%02X:%02X:%02X:%02X:%02X\n",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    else if (strcmp(keyword, "VTP_STREAMING_NSTREAMS") == 0) {
      sscanf(line, "%*s %d", &params->vtp_streaming_nstreams);
      params->have_vtp_nstreams = 1;
      printf("INFO:   VTP_STREAMING_NSTREAMS = %d\n", params->vtp_streaming_nstreams);
    }
    else if (strcmp(keyword, "VTP_STREAMING_IPADDR") == 0) {
      unsigned int ip[4];
      sscanf(line, "%*s %u %u %u %u", &ip[0], &ip[1], &ip[2], &ip[3]);
      params->vtp_streaming_ipaddr[0] = ip[0];
      params->vtp_streaming_ipaddr[1] = ip[1];
      params->vtp_streaming_ipaddr[2] = ip[2];
      params->vtp_streaming_ipaddr[3] = ip[3];
      params->have_vtp_ipaddr = 1;
      printf("INFO:   VTP_STREAMING_IPADDR = %d.%d.%d.%d\n",
             ip[0], ip[1], ip[2], ip[3]);
    }
    else if (strcmp(keyword, "VTP_STREAMING_SUBNET") == 0) {
      unsigned int subnet[4];
      sscanf(line, "%*s %u %u %u %u", &subnet[0], &subnet[1], &subnet[2], &subnet[3]);
      params->vtp_streaming_subnet[0] = subnet[0];
      params->vtp_streaming_subnet[1] = subnet[1];
      params->vtp_streaming_subnet[2] = subnet[2];
      params->vtp_streaming_subnet[3] = subnet[3];
      params->have_vtp_subnet = 1;
      printf("INFO:   VTP_STREAMING_SUBNET = %d.%d.%d.%d\n",
             subnet[0], subnet[1], subnet[2], subnet[3]);
    }
    else if (strcmp(keyword, "VTP_STREAMING_GATEWAY") == 0) {
      unsigned int gateway[4];
      sscanf(line, "%*s %u %u %u %u", &gateway[0], &gateway[1], &gateway[2], &gateway[3]);
      params->vtp_streaming_gateway[0] = gateway[0];
      params->vtp_streaming_gateway[1] = gateway[1];
      params->vtp_streaming_gateway[2] = gateway[2];
      params->vtp_streaming_gateway[3] = gateway[3];
      params->have_vtp_gateway = 1;
      printf("INFO:   VTP_STREAMING_GATEWAY = %d.%d.%d.%d\n",
             gateway[0], gateway[1], gateway[2], gateway[3]);
    }
    else if (strcmp(keyword, "VTP_STREAMING_DESTIP") == 0) {
      sscanf(line, "%*s %63s", params->vtp_streaming_destip);
      params->have_vtp_destip = 1;
      printf("INFO:   VTP_STREAMING_DESTIP = %s\n", params->vtp_streaming_destip);
    }
    else if (strcmp(keyword, "VTP_STREAMING_DESTIPPORT") == 0) {
      sscanf(line, "%*s %d", &params->vtp_streaming_destipport);
      params->have_vtp_destipport = 1;
      printf("INFO:   VTP_STREAMING_DESTIPPORT = %d\n", params->vtp_streaming_destipport);
    }
    else if (strcmp(keyword, "VTP_STREAMING_LOCALPORT") == 0) {
      sscanf(line, "%*s %d", &params->vtp_streaming_localport);
      params->have_vtp_localport = 1;
      printf("INFO:   VTP_STREAMING_LOCALPORT = %d\n", params->vtp_streaming_localport);
    }
    else if (strcmp(keyword, "VTP_STREAMING_CONNECT") == 0) {
      sscanf(line, "%*s %d", &params->vtp_streaming_connect);
      params->have_vtp_connect = 1;
      printf("INFO:   VTP_STREAMING_CONNECT = %d\n", params->vtp_streaming_connect);
    }
  }

  fclose(fp);

  /* Validate required VTP parameters */
  if (!params->have_vtp_rocid) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_ROCID\n");
    return -1;
  }
  if (!params->have_vtp_nframe_buf) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_NFRAME_BUF\n");
    return -1;
  }
  if (!params->have_vtp_framelen) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_FRAMELEN\n");
    return -1;
  }
  if (!params->have_vtp_streaming) {
    printf("ERROR: Missing required parameter: VTP_STREAMING\n");
    return -1;
  }
  if (!params->have_vtp_mac) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_MAC\n");
    return -1;
  }
  if (!params->have_vtp_nstreams) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_NSTREAMS\n");
    return -1;
  }
  if (!params->have_vtp_ipaddr) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_IPADDR\n");
    return -1;
  }
  if (!params->have_vtp_subnet) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_SUBNET\n");
    return -1;
  }
  if (!params->have_vtp_gateway) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_GATEWAY\n");
    return -1;
  }
  if (!params->have_vtp_destip) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_DESTIP\n");
    return -1;
  }
  if (!params->have_vtp_destipport) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_DESTIPPORT\n");
    return -1;
  }
  if (!params->have_vtp_localport) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_LOCALPORT\n");
    return -1;
  }
  if (!params->have_vtp_connect) {
    printf("ERROR: Missing required parameter: VTP_STREAMING_CONNECT\n");
    return -1;
  }

  printf("INFO: User config parsing completed successfully\n");
  return 0;
}

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
 * Uses parsed user config parameters and appends peds.txt content
 * Returns: 0 on success, -1 on failure
 */
static int generate_vme_config(const char *config_dir, const char *rocname,
                                 const char *peds_file, const user_config_params_t *params)
{
  char vme_config_path[512];
  FILE *out_fp, *peds_fp;
  char line[1024];
  int line_count = 0;
  int i;

  if (!config_dir || !rocname || !peds_file || !params) return -1;

  /* Build output path */
  snprintf(vme_config_path, sizeof(vme_config_path), "%s/vme_%s.cnf", config_dir, rocname);

  printf("INFO: Generating VME config file: %s\n", vme_config_path);
  printf("INFO: Using parameters from user config\n");

  /* Open output file */
  out_fp = fopen(vme_config_path, "w");
  if (!out_fp)
  {
    printf("ERROR: Cannot create VME config file '%s': %s\n", vme_config_path, strerror(errno));
    return -1;
  }

  /* Write header using parsed parameters */
  fprintf(out_fp, "FADC250_CRATE all\n");
  fprintf(out_fp, "FADC250_SLOT all\n");
  fprintf(out_fp, "\n");
  fprintf(out_fp, "#       channel:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15\n");

  /* Write ADC mask from parsed config */
  fprintf(out_fp, "FADC250_ADC_MASK");
  for (i = 0; i < 16; i++) {
    fprintf(out_fp, "  %d", params->fadc_adc_mask[i]);
  }
  fprintf(out_fp, "\n");

  /* Write TRG mask from parsed config */
  fprintf(out_fp, "FADC250_TRG_MASK");
  for (i = 0; i < 16; i++) {
    fprintf(out_fp, "  %d", params->fadc_trg_mask[i]);
  }
  fprintf(out_fp, "\n");
  fprintf(out_fp, "\n");

  fprintf(out_fp, "# force readout of channel mask (i.e. ignore threshold for readout)\n");
  fprintf(out_fp, "FADC250_TET_IGNORE_MASK");
  for (i = 0; i < 16; i++) {
    fprintf(out_fp, " %d", params->fadc_tet_ignore_mask[i]);
  }
  fprintf(out_fp, "\n");
  fprintf(out_fp, "\n");

  fprintf(out_fp, "#modes=1 (raw sample), 9 ( pulse integral + Hi-res time), 10 (pulse integer + Hi-res time + raw)\n");
  fprintf(out_fp, "FADC250_MODE %d\n", params->fadc_mode);
  fprintf(out_fp, "\n");

  fprintf(out_fp, "# number of ns back from trigger point.\n");
  fprintf(out_fp, "FADC250_W_OFFSET  %d\n", params->fadc_w_offset);
  fprintf(out_fp, "\n");

  fprintf(out_fp, "# number of ns to include in trigger window.\n");
  fprintf(out_fp, "FADC250_W_WIDTH  %d\n", params->fadc_w_width);
  fprintf(out_fp, "\n");

  fprintf(out_fp, "# time (units: ns) before threshold crossing to include in integral\n");
  fprintf(out_fp, "FADC250_NSB %d \n", params->fadc_nsb);
  fprintf(out_fp, "\n");

  fprintf(out_fp, "# time (units: ns) after threshold crossing to include in integral\n");
  fprintf(out_fp, "FADC250_NSA %d\n", params->fadc_nsa);
  fprintf(out_fp, "\n");

  fprintf(out_fp, "FADC250_NPEAK %d\n", params->fadc_npeak);
  fprintf(out_fp, "\n");

  fprintf(out_fp, "# fadc channel hit threshold (adc channel)\n");
  fprintf(out_fp, "FADC250_TET %d\n", params->fadc_tet);
  fprintf(out_fp, "\n");

  fprintf(out_fp, "# board DAC, one and the same for all 16 channels (DAC/mV)\n");
  fprintf(out_fp, "FADC250_DAC %d \n", params->fadc_dac);
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
 * Parse active FADC slots from peds file
 * Reads all FADC250_SLOT entries and collects unique slot numbers
 * Returns: number of active slots found, or -1 on error
 */
static int parse_active_fadc_slots(const char *peds_file, int *slots, int max_slots)
{
  FILE *fp;
  char line[512];
  char keyword[128];
  int slot_num;
  int num_slots = 0;
  int i, already_exists;

  if (!peds_file || !slots || max_slots <= 0) return -1;

  fp = fopen(peds_file, "r");
  if (!fp)
  {
    printf("ERROR: Cannot open peds file '%s' for parsing slots: %s\n", peds_file, strerror(errno));
    return -1;
  }

  /* Read file line by line looking for FADC250_SLOT entries */
  while (fgets(line, sizeof(line), fp) != NULL)
  {
    if (sscanf(line, "%127s %d", keyword, &slot_num) == 2)
    {
      if (strcmp(keyword, "FADC250_SLOT") == 0)
      {
        /* Check if this slot is already in our list */
        already_exists = 0;
        for (i = 0; i < num_slots; i++)
        {
          if (slots[i] == slot_num)
          {
            already_exists = 1;
            break;
          }
        }

        /* Add new slot if not already present and we have room */
        if (!already_exists)
        {
          if (num_slots < max_slots)
          {
            slots[num_slots] = slot_num;
            num_slots++;
            printf("INFO: Found active FADC slot %d in peds file\n", slot_num);
          }
          else
          {
            printf("WARNING: Too many FADC slots found (max %d), ignoring slot %d\n", max_slots, slot_num);
          }
        }
      }
    }
  }

  fclose(fp);
  return num_slots;
}

/**
 * Map FADC slot number to VTP payload number using translation table
 * Translation: slot -> payload
 *   10->1, 13->2, 9->3, 14->4, 8->5, 15->6, 7->7, 16->8,
 *   6->9, 17->10, 5->11, 18->12, 4->13, 19->14, 3->15, 20->16
 * Returns: payload number (1-16) or -1 if slot not in table
 */
static int slot_to_payload(int slot)
{
  /* Translation table: index is payload-1, value is slot */
  static const int payload_to_slot[16] = {
    10, 13, 9, 14, 8, 15, 7, 16, 6, 17, 5, 18, 4, 19, 3, 20
  };
  int i;

  /* Search for slot in table */
  for (i = 0; i < 16; i++)
  {
    if (payload_to_slot[i] == slot)
    {
      return i + 1; /* Return payload number (1-based) */
    }
  }

  return -1; /* Slot not found in translation table */
}

/**
 * Generate vtp_<rocname>vtp.cnf configuration file
 * Uses parsed user config parameters for VTP settings
 * Derives VTP_PAYLOAD_EN from active FADC slots in peds.txt
 * Returns: 0 on success, -1 on failure
 */
static int generate_vtp_config(const char *config_dir, const char *rocname,
                                 const char *peds_file, const user_config_params_t *params)
{
  char vtp_config_path[512];
  FILE *out_fp;
  int active_slots[20];  /* Max 20 slots */
  int num_active_slots;
  int payload_enable[16]; /* 16 payloads */
  int i, slot, payload;

  if (!config_dir || !rocname || !peds_file) return -1;

  /* Parse active FADC slots from peds file */
  num_active_slots = parse_active_fadc_slots(peds_file, active_slots, 20);
  if (num_active_slots < 0)
  {
    printf("ERROR: Failed to parse active FADC slots from peds file\n");
    return -1;
  }

  /* Initialize payload enable array to all zeros */
  for (i = 0; i < 16; i++)
  {
    payload_enable[i] = 0;
  }

  /* Map active slots to payloads */
  for (i = 0; i < num_active_slots; i++)
  {
    slot = active_slots[i];
    payload = slot_to_payload(slot);

    if (payload > 0 && payload <= 16)
    {
      payload_enable[payload - 1] = 1;  /* payload is 1-based, array is 0-based */
      printf("INFO: Mapped FADC slot %d to VTP payload %d (enabled)\n", slot, payload);
    }
    else
    {
      printf("WARNING: FADC slot %d not found in slot->payload translation table (ignored)\n", slot);
    }
  }

  /* Warn if no active payloads */
  if (num_active_slots == 0)
  {
    printf("WARNING: No active FADC slots found in peds file - all VTP payloads will be disabled\n");
  }

  /* Build output path */
  snprintf(vtp_config_path, sizeof(vtp_config_path), "%s/vtp_%svtp.cnf", config_dir, rocname);

  printf("INFO: Generating VTP config file: %s\n", vtp_config_path);

  /* Open output file */
  out_fp = fopen(vtp_config_path, "w");
  if (!out_fp)
  {
    printf("ERROR: Cannot create VTP config file '%s': %s\n", vtp_config_path, strerror(errno));
    return -1;
  }

  /* Write template content (based on vtp_rocnamevtp.cnf) */
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
  fprintf(out_fp, "VTP_PAYLOAD_EN");
  for (i = 0; i < 16; i++)
  {
    fprintf(out_fp, "  %d", payload_enable[i]);
  }
  fprintf(out_fp, "\n");
  fprintf(out_fp, "\n");
  /* Use parsed VTP parameters from user config */
  printf("INFO: Using VTP parameters from user config\n");
  fprintf(out_fp, "VTP_STREAMING_ROCID       %d\n", params->vtp_streaming_rocid);
  fprintf(out_fp, "VTP_STREAMING_NFRAME_BUF  %d\n", params->vtp_streaming_nframe_buf);
  fprintf(out_fp, "VTP_STREAMING_FRAMELEN    %d\n", params->vtp_streaming_framelen);
  fprintf(out_fp, "\n");
  fprintf(out_fp, "###################################################\n");
  fprintf(out_fp, "#VTP FADC Streaming event builder #0 (slots 3-10) #\n");
  fprintf(out_fp, "###################################################\n");
  fprintf(out_fp, "VTP_STREAMING             %d\n", params->vtp_streaming);
  fprintf(out_fp, "\n");
  fprintf(out_fp, "VTP_STREAMING_MAC         0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
          params->vtp_streaming_mac[0], params->vtp_streaming_mac[1],
          params->vtp_streaming_mac[2], params->vtp_streaming_mac[3],
          params->vtp_streaming_mac[4], params->vtp_streaming_mac[5]);
  fprintf(out_fp, "\n");
  fprintf(out_fp, "VTP_STREAMING_NSTREAMS    %d\n", params->vtp_streaming_nstreams);
  fprintf(out_fp, "\n");
  fprintf(out_fp, "VTP_STREAMING_IPADDR      %d  %d %d %d\n",
          params->vtp_streaming_ipaddr[0], params->vtp_streaming_ipaddr[1],
          params->vtp_streaming_ipaddr[2], params->vtp_streaming_ipaddr[3]);
  fprintf(out_fp, "VTP_STREAMING_SUBNET      %d %d %d   %d\n",
          params->vtp_streaming_subnet[0], params->vtp_streaming_subnet[1],
          params->vtp_streaming_subnet[2], params->vtp_streaming_subnet[3]);
  fprintf(out_fp, "VTP_STREAMING_GATEWAY     %d  %d  %d   %d   \n",
          params->vtp_streaming_gateway[0], params->vtp_streaming_gateway[1],
          params->vtp_streaming_gateway[2], params->vtp_streaming_gateway[3]);
  fprintf(out_fp, "\n");
  fprintf(out_fp, "VTP_STREAMING_DESTIP      %s\n", params->vtp_streaming_destip);
  fprintf(out_fp, "VTP_STREAMING_DESTIPPORT  %d\n", params->vtp_streaming_destipport);
  fprintf(out_fp, "VTP_STREAMING_LOCALPORT   %d\n", params->vtp_streaming_localport);
  fprintf(out_fp, "\n");
  fprintf(out_fp, "VTP_STREAMING_CONNECT     %d\n", params->vtp_streaming_connect);
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
  char generated_vme_config[512];  /* Path to generated VME config file */

  printf("***** rocDownload() ENTERED - START OF FUNCTION *****\n");
  fflush(stdout);

  /* ===================================================================
   * USER CONFIG PARSING, PEDESTAL GENERATION, AND CONFIG FILE CREATION
   * =================================================================== */
  {
    char hostname[256];
    char peds_file[512];
    char rocname[256];
    char command[1024];
    const char *coda_env, *coda_data_env, *coda_config_env;
    pid_t pid;
    int status;
    user_config_params_t config_params;

    printf("INFO: ============================================\n");
    printf("INFO: PHASE 1: Parse user configuration\n");
    printf("INFO: ============================================\n");

    /* Initialize config parameters */
    init_config_params(&config_params);

    /* Parse user configuration file */
    if (!rol->usrConfig || !*rol->usrConfig) {
      printf("ERROR: rocDownload - User config file (rol->usrConfig) not set\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }

    printf("INFO: Parsing user config file: %s\n", rol->usrConfig);
    if (parse_user_config(rol->usrConfig, &config_params) != 0) {
      printf("ERROR: rocDownload - Failed to parse user config file\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }

    printf("INFO: ============================================\n");
    printf("INFO: PHASE 2: Generate pedestals\n");
    printf("INFO: ============================================\n");

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

    printf("INFO: ============================================\n");
    printf("INFO: PHASE 3: Generate configuration files\n");
    printf("INFO: ============================================\n");

    /* Parse rocname from peds file */
    if (parse_rocname_from_peds(peds_file, rocname, sizeof(rocname)) != 0)
    {
      printf("ERROR: rocDownload - Failed to parse rocname from peds file\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }

    /* Generate vme_<rocname>.cnf using parsed parameters */
    if (generate_vme_config(coda_config_env, rocname, peds_file, &config_params) != 0)
    {
      printf("ERROR: rocDownload - Failed to generate VME config file\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }

    /* Generate vtp_<rocname>vtp.cnf using parsed parameters */
    if (generate_vtp_config(coda_config_env, rocname, peds_file, &config_params) != 0)
    {
      printf("ERROR: rocDownload - Failed to generate VTP config file\n");
      printf("ERROR: rocDownload - DOWNLOAD TRANSITION FAILED\n");
      return;
    }

    printf("INFO: ============================================\n");
    printf("INFO: Config file generation complete\n");
    printf("INFO:   User config (input):  %s\n", rol->usrConfig);
    printf("INFO:   Hostname: %s\n", hostname);
    printf("INFO:   ROC name: %s\n", rocname);
    printf("INFO:   Peds file: %s\n", peds_file);
    printf("INFO:   VME config (generated): %s/vme_%s.cnf\n", coda_config_env, rocname);
    printf("INFO:   VTP config (generated): %s/vtp_%svtp.cnf\n", coda_config_env, rocname);
    printf("INFO: ============================================\n");

    /* Store generated VME config path for use in FADC configuration below */
    snprintf(generated_vme_config, sizeof(generated_vme_config),
             "%s/vme_%s.cnf", coda_config_env, rocname);
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
  /* Read in the GENERATED FADC250 Config file (NOT rol->usrConfig) */
  printf("INFO: ============================================\n");
  printf("INFO: PHASE 4: Configure FADC using generated VME config\n");
  printf("INFO: ============================================\n");
  printf("INFO: Using generated VME config file: %s\n", generated_vme_config);
  stat = fadc250Config(generated_vme_config);
  if(stat<0) {
    printf("ERROR: Reading FADC250 Config file '%s' FAILED\n", generated_vme_config);
  }else{
    printf("INFO: Successfully loaded FADC250 Config from generated file\n");
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
