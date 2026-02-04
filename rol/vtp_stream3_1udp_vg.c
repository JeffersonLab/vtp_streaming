/*************************************************************************
 *
 *  vtp_list.c -      Library of routines for readout of events using a
 *                    JLAB Trigger Interface V3 (TI) with a VTP in
 *                    CODA 3.0.
 *
 *                    This is for a VTP with serial connection to a TI
 *
 */

/* Event Buffer definitions */
#define MAX_EVENT_LENGTH 40960
#define MAX_EVENT_POOL   100

#define STREAMING_MODE

#include <VTP_source.h>

/* --- Added system headers for timestamp/frame reads and UDP sender thread --- */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>

#undef USE_DMA
#undef READOUT_TI
#undef READOUT_VTP

#define MAXBUFSIZE 10000
unsigned int gFixedBuf[MAXBUFSIZE];

int blklevel = 1;
int maxdummywords = 200;
int vtpComptonEnableScalerReadout = 0;

/* trigBankType:
   Type 0xff10 is RAW trigger No timestamps
   Type 0xff11 is RAW trigger with timestamps (64 bits)
*/
int trigBankType = 0xff10;
int firstEvent;

/*
 * Network streaming configuration now read from config file.
 * Default values in vtpConfig.c match previous hard-coded values:
 * - VTP_NUM_CONNECTIONS: 1 (can be 1-4)
 * - VTP_NET_MODE: 1 (0=TCP, 1=UDP)
 * - VTP_ENABLE_EJFAT: 1 (Enable EJFAT headers - UDP only)
 * - VTP_LOCAL_PORT: 10001 (base local port)
 */

/* define an array of Payload port Config Structures */
PP_CONF ppInfo[16];

/* Data necessary to connect via TCP using EMUSockets
#define CMSG_MAGIC_INT1 0x634d7367
#define CMSG_MAGIC_INT2 0x20697320
#define CMSG_MAGIC_INT3 0x636f6f6c
*/
/* Note: emuData[6] will be set to vtpGetNumConnections() in rocPrestart() */
unsigned int emuData[] = {0x634d7367,0x20697320,0x636f6f6c,6,0,4196352,1,0};
/*unsigned int emuData[] = {0x67734d63,0x20736920,0x6cf6f663,0x06000000,0,0x00084000,0x01000000,0x01000000};*/

/* =========================[ ADDED: VTP helpers ]========================= */

/**
 * Get short, sanitized hostname for use in filenames.
 * Strips the domain portion (everything from the first dot onward) so that
 * "test2.jlab.org" becomes "test2", matching the ROC name used when the
 * config files are generated.
 * Only allows [A-Za-z0-9_-] in the result; other characters become '_'.
 * Returns: 0 on success, -1 on failure
 */
static int vtp_get_sanitized_hostname(char *hostname_buf, size_t bufsize)
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

  /* Strip domain: keep only the short hostname (everything before the first dot).
   * gethostname() may return an FQDN (e.g. "test2.jlab.org") but the generated
   * config files use only the short ROC name ("test2").  When no dot is present
   * the string is already a short name and nothing is changed. */
  {
    char *dot = strchr(hostname_buf, '.');
    if (dot)
      *dot = '\0';
  }

  /* Sanitize hostname: only allow [A-Za-z0-9_-] */
  for (i = 0; hostname_buf[i] != '\0'; i++)
  {
    if (!isalnum(hostname_buf[i]) && hostname_buf[i] != '_' &&
        hostname_buf[i] != '-')
    {
      hostname_buf[i] = '_';
    }
  }

  return 0;
}

/**
 * Construct path to generated VTP config file
 * Uses $CODA_CONFIG/vtp_<hostname>.cnf (assuming rocname = hostname)
 * Returns: 0 on success, -1 on failure
 */
static int vtp_get_generated_config_path(char *path_buf, size_t bufsize)
{
  char hostname[256];
  const char *coda_config;

  if (!path_buf || bufsize == 0) return -1;

  /* Get CODA_CONFIG directory */
  coda_config = getenv("CODA_CONFIG");
  if (!coda_config || !*coda_config)
  {
    printf("ERROR: CODA_CONFIG environment variable not set\n");
    return -1;
  }

  /* Get sanitized hostname */
  if (vtp_get_sanitized_hostname(hostname, sizeof(hostname)) != 0)
  {
    printf("ERROR: Failed to get hostname for VTP config path\n");
    return -1;
  }

  /* Construct path: $CODA_CONFIG/vtp_<hostname>.cnf */
  snprintf(path_buf, bufsize, "%s/vtp_%s.cnf", coda_config, hostname);

  return 0;
}

/* Helper to get source ID (use ROCID as fallback) */
static int vtp_get_src_id(uint32_t *out)
{
  if (!out) return ERROR;
  *out = (uint32_t)ROCID;
  return OK;
}

/* Helper: read VTP_STREAMING_DESTIP and VTP_STREAMING_DESTIPPORT from cfg file */
static void
vtp_read_dest_from_cfg(const char *cfg_path,
                       unsigned int *emuip,
                       unsigned int *emuport)
{
  FILE *f;
  char line[1024];

  if (!cfg_path)
    return;

  f = fopen(cfg_path, "r");
  if (!f)
  {
    printf("vtp_read_dest_from_cfg: cannot open config file '%s'\n", cfg_path);
    return;
  }

  while (fgets(line, sizeof(line), f))
  {
    char key[128];
    char val[512];

    /* Skip comments and blank lines */
    if (line[0] == '#' || line[0] == '\n')
      continue;

    if (sscanf(line, "%127s %511s", key, val) != 2)
      continue;

    if (strcmp(key, "VTP_STREAMING_DESTIP") == 0)
    {
      /* Expect dotted-quad string, e.g. 192.188.29.11 */
      printf("DDD = '%s'\n", val);
      unsigned int ip = vtpRoc_inet_addr(val);
      if (ip != 0)
        *emuip = ip;
      printf("DDD = '%08x'\n", *emuip);
    }
    else if (strcmp(key, "VTP_STREAMING_DESTIPPORT") == 0)
    {
      unsigned int port_tmp;
      printf("DDD = '%s'\n", val);
      if (sscanf(val, "%u", &port_tmp) == 1)
        *emuport = port_tmp;
      printf("DDD = '%08x'\n", *emuport);
    }
  }

  fclose(f);
}

/* =========================[ ADDED: UDP stats sender ]========================= */
/*
 * Stats sender configuration now read from config file.
 * Default values in vtpConfig.c match previous hard-coded values:
 * - VTP_STATS_HOST: "129.57.29.231" (indra-s2 IP for forwarding sync packets)
 * - VTP_STATS_PORT: 19531
 * - VTP_STATS_INST: 0
 * - VTP_SYNC_PKT_LEN: 28
 */

static pthread_t g_vtp_stats_thr;
static volatile int g_vtp_stats_run = 0;
static int g_vtp_stats_sock = -1;
static struct sockaddr_storage g_vtp_stats_dst;
static socklen_t g_vtp_stats_dst_len = 0;
static int g_vtp_stats_inst = 0;  /* Set by vtp_stats_sender_launch() from config */

/* Helper for 64-bit network byte order (big endian) */
static inline uint64_t htonll(uint64_t val) {
  uint32_t hi = htonl((uint32_t)(val >> 32));
  uint32_t lo = htonl((uint32_t)(val & 0xFFFFFFFFU));
  return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/**
 * Set sync packet data in the specified format for load balancer.
 *
 * @param buffer   Buffer in which to write the data.
 * @param version  Software version.
 * @param srcId    Identifier for this data source.
 * @param evtNum   64-bit event (frame) number used by the load balancer
 *                 to determine which backend host should receive the packet.
 *                 This message indicates that this application has already
 *                 sent this most recent event.
 * @param evtRate  Event (frame) rate in Hz — the rate at which this application
 *                 is sending events to the load balancer (0 if unknown).
 * @param nanos    Unix timestamp in nanoseconds representing when this message
 *                 was sent (0 if unknown).
 */
static void setSyncData(char* buffer, int version, uint32_t srcId,
                        uint64_t evtNum, uint32_t evtRate, uint64_t nanos) {
    buffer[0] = 'L';
    buffer[1] = 'C';
    buffer[2] = (char)version;
    buffer[3] = 0; /* reserved byte */

    /* Store fields in network byte order (big endian) */
    *((uint32_t *)(buffer + 4))  = htonl(srcId);
    *((uint64_t *)(buffer + 8))  = htonll(evtNum);
    *((uint32_t *)(buffer + 16)) = htonl(evtRate);
    *((uint64_t *)(buffer + 20)) = htonll(nanos);
}

static int _resolve_udp(const char *host, uint16_t port, struct sockaddr_storage *out, socklen_t *out_len)
{
  char port_str[16];
  struct addrinfo hints, *res = NULL;
  snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM; hints.ai_protocol = IPPROTO_UDP;
  int rc = getaddrinfo(host, port_str, &hints, &res);
  if (rc != 0 || !res) return ERROR;
  if (res->ai_addrlen > sizeof(*out)) { freeaddrinfo(res); return ERROR; }
  memcpy(out, res->ai_addr, res->ai_addrlen);
  *out_len = (socklen_t)res->ai_addrlen;
  freeaddrinfo(res);
  return OK;
}

static void *_vtp_stats_sender_main(void *arg)
{
  (void)arg;
  uint32_t src_id = 0;
  (void)vtp_get_src_id(&src_id);

  /* Initialize frame counter tracking - extend 32-bit to 64-bit */
  uint32_t prev_fc_low32 = vtpStreamingGetEbFrameCnt(g_vtp_stats_inst);
  uint64_t prev_fc_ext = (uint64_t)prev_fc_low32;

  struct timespec t_prev;
  clock_gettime(CLOCK_MONOTONIC, &t_prev);

  while (g_vtp_stats_run)
  {
    /* sleep ~1s, resilient to EINTR */
    struct timespec req = { .tv_sec = 1, .tv_nsec = 0 }, rem;
    while (nanosleep(&req, &rem) != 0 && errno == EINTR) req = rem;

    struct timespec t_now;
    clock_gettime(CLOCK_MONOTONIC, &t_now);
    double dt = (t_now.tv_sec - t_prev.tv_sec) + (t_now.tv_nsec - t_prev.tv_nsec)/1e9;
    if (dt <= 0) dt = 1.0;
    t_prev = t_now;

    /* Get current 32-bit frame counter from VTP library */
    uint32_t cur_fc_low32 = vtpStreamingGetEbFrameCnt(g_vtp_stats_inst);

    /* Extend to 64-bit by detecting wraps */
    uint64_t fc_ext;
    if (cur_fc_low32 < prev_fc_low32) {
      /* Wrap detected - increment upper 32 bits */
      prev_fc_ext += (1ULL << 32);
    }
    fc_ext = (prev_fc_ext & 0xFFFFFFFF00000000ULL) | (uint64_t)cur_fc_low32;
    prev_fc_low32 = cur_fc_low32;

    /* Calculate event rate (frames per second) */
    double dframes = (fc_ext >= prev_fc_ext) ? (double)(fc_ext - prev_fc_ext) : 0.0;
    uint32_t evt_rate = (uint32_t)((dframes / dt) + 0.5);

    /* Compute timestamp: frame_number × 65.535 microseconds (in nanoseconds)
     * 65.535 µs = 65535 ns */
    uint64_t timestamp_ns = fc_ext * 65535ULL;

    /* Prepare sync packet buffer */
    int pkt_len = vtpGetSyncPktLen();
    char buf[256];  /* Large enough for any reasonable sync packet */
    if (pkt_len > (int)sizeof(buf)) pkt_len = sizeof(buf);
    setSyncData(buf, 1, src_id, fc_ext, evt_rate, timestamp_ns);

    /* Send sync packet */
    (void)sendto(g_vtp_stats_sock, buf, pkt_len, 0,
                 (struct sockaddr *)&g_vtp_stats_dst, g_vtp_stats_dst_len);

    /* Update previous frame count for next iteration */
    prev_fc_ext = fc_ext;
  }
  return NULL;
}

static int vtp_stats_sender_launch(const char *host, uint16_t port, int stream_inst)
{
  if (!host || !*host || port == 0 || stream_inst < 0) return ERROR;
  if (g_vtp_stats_run) return OK; /* idempotent */

  if (_resolve_udp(host, port, &g_vtp_stats_dst, &g_vtp_stats_dst_len) != OK) {
    printf("vtp_stats_sender: resolve failed for %s:%u\n", host, (unsigned)port);
    return ERROR;
  }
  int s = socket(g_vtp_stats_dst.ss_family, SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0) { printf("vtp_stats_sender: socket failed: %s\n", strerror(errno)); return ERROR; }

  g_vtp_stats_sock = s;
  g_vtp_stats_inst = stream_inst;
  g_vtp_stats_run = 1;

  int rc = pthread_create(&g_vtp_stats_thr, NULL, _vtp_stats_sender_main, NULL);
  if (rc != 0) {
    g_vtp_stats_run = 0;
    close(g_vtp_stats_sock); g_vtp_stats_sock = -1;
    printf("vtp_stats_sender: pthread_create failed: %s\n", strerror(rc));
    return ERROR;
  }
  return OK;
}

static int vtp_stats_sender_stop(void)
{
  if (!g_vtp_stats_run) return OK;
  g_vtp_stats_run = 0;
  (void)pthread_join(g_vtp_stats_thr, NULL);
  if (g_vtp_stats_sock >= 0) { close(g_vtp_stats_sock); g_vtp_stats_sock = -1; }
  memset(&g_vtp_stats_dst, 0, sizeof(g_vtp_stats_dst));
  g_vtp_stats_dst_len = 0;
  g_vtp_stats_inst = 0;
  return OK;
}

/* =========================[ Original code + mods ]========================= */

/**
                        DOWNLOAD
**/
void
rocDownload()
{
// Check and get CODA env variable.
const char *coda = getenv("CODA");
if (!coda || !*coda) {
    fprintf(stderr, "ERROR: CODA env var is not set\n");
    return;
}
  
  int stat;
  char buf[1000];
  /* Streaming Version 3 firmware files for VTP */

  /* Get firmware filenames from config (already set by vtpConfig) */
  const char *z7file = vtpGetFirmwareZ7();
  const char *v7file = vtpGetFirmwareV7();

  /* Validate firmware filenames */
  if (!z7file || !*z7file) {
    printf("WARNING: Z7 firmware filename not set in config, using default\n");
    z7file = "fe_vtp_z7_streamingv3_ejfat_v5.bin";
  }
  if (!v7file || !*v7file) {
    printf("WARNING: V7 firmware filename not set in config, using default\n");
    v7file = "fe_vtp_v7_fadc_streamingv3_ejfat.bin";
  }

  firstEvent = 1;

  /* Open VTP library */
  stat = vtpOpen(VTP_FPGA_OPEN | VTP_I2C_OPEN | VTP_SPI_OPEN);
  if(stat < 0)
  {
    printf(" Unable to Open VTP driver library.\n");
  }

  /* Load firmware here */
snprintf(buf, sizeof(buf), "%s/src/vtp/vtp_streaming/vtp/firmware/%s", coda, z7file);
//sprintf(buf, "/home/ejfat/coda-vg/3.10_devel2/src/vtp/vtp_streaming/vtp/firmware/%s", z7file);

 if(vtpZ7CfgLoad(buf) != OK)
  {
    printf("Z7 programming failed... (%s)\n", buf);
  }

snprintf(buf, sizeof(buf), "%s/src/vtp/vtp_streaming/vtp/firmware/%s", coda, v7file);
//sprintf(buf, "/home/ejfat/coda-vg/3.10_devel2/src/vtp/vtp_streaming/vtp/firmware/%s", v7file);

 if(vtpV7CfgLoad(buf) != OK)
  {
    printf("V7 programming failed... (%s)\n", buf);
  }

  ltm4676_print_status();
}

/**
                        PRESTART
**/
void
rocPrestart()
{
  unsigned int emuip = 0, emuport = 0;
  int ii, stat, ppmask=0;
  int netMode;      // 0=TCP, 1=UDP (from config)
  int localport;    // base local TCP port (from config)
  int numConnections;  // Number of VTP connections (from config)
  int enableEjfat;  // Enable EJFAT headers (from config)

  /* Get network streaming config values */
  netMode = vtpGetNetMode();
  localport = vtpGetLocalPort();
  numConnections = vtpGetNumConnections();
  enableEjfat = vtpGetEnableEjfat();

  VTPflag = 0;

  /* Initialize the VTP here since external clock is stable now */
  if(vtpInit(VTP_INIT_CLK_VXS_250))
  {
    printf("vtpInit() **FAILED**. User should not continue.\n");
    return;
  }

  /* Read back frame counter for verification */
  {
    uint32_t fc = vtpStreamingGetEbFrameCnt(0);
    printf("VTP frame counter (inst 0, PRESTART): 0x%08x (%u)\n", fc, fc);
  }

  printf("Calling VTP_READ_CONF_FILE ..\n");fflush(stdout);

  /* Read Config file and Initialize VTP variables */
  /* Use GENERATED VTP config file from $CODA_CONFIG, not rol->usrConfig */
  {
    char vtp_config_path[512];

    if (vtp_get_generated_config_path(vtp_config_path, sizeof(vtp_config_path)) == 0)
    {
      printf("INFO: Using GENERATED VTP config file: %s\n", vtp_config_path);

      /* Verify file exists before trying to read it */
      if (access(vtp_config_path, R_OK) == 0)
      {
        vtpInitGlobals();
        vtpConfig(vtp_config_path);

        /* Try to get DESTIP/DESTIPPORT from VTP config file */
        vtp_read_dest_from_cfg(vtp_config_path, &emuip, &emuport);
      }
      else
      {
        printf("WARNING: Generated VTP config file '%s' not found or not readable\n", vtp_config_path);
        printf("WARNING: Falling back to user config from rol->usrConfig\n");

        /* Fallback to user config if generated file doesn't exist */
        printf("%s: Fallback - rol->usrConfig = %s\n", __func__, rol->usrConfig);
        vtpInitGlobals();
        if(rol->usrConfig)
        {
          vtpConfig(rol->usrConfig);
          vtp_read_dest_from_cfg(rol->usrConfig, &emuip, &emuport);
        }
      }
    }
    else
    {
      printf("ERROR: Failed to construct generated VTP config path\n");
      printf("ERROR: Falling back to user config from rol->usrConfig\n");

      /* Fallback to user config if path construction fails */
      printf("%s: Fallback - rol->usrConfig = %s\n", __func__, rol->usrConfig);
      vtpInitGlobals();
      if(rol->usrConfig)
      {
        vtpConfig(rol->usrConfig);
        vtp_read_dest_from_cfg(rol->usrConfig, &emuip, &emuport);
      }
    }
  }

  /* Fallback to CODA ROC link info if config did not set them */
  if (emuip == 0 || emuport == 0)
  {
    unsigned int fallback_ip   = vtpRoc_inet_addr(rol->rlinkP->net);
    unsigned int fallback_port = rol->rlinkP->port;
    printf("vtp_read_dest_from_cfg did not fully set EMU dest, "
           "falling back to ROC link: ip=0x%08x port=%d\n",
           fallback_ip, fallback_port);

    if (emuip == 0)   emuip   = fallback_ip;
    if (emuport == 0) emuport = fallback_port;
  }

  localport = localport + netMode;

  printf("EMU DEST from cfg/ROC: IP=0x%08x port=%d localport=%d\n",
         emuip, emuport, localport);

  /* ========================================================================
   * DYNAMIC PAYLOAD CONFIGURATION
   * ========================================================================
   * Configure payload ports dynamically based on VTP_PAYLOAD_EN from config.
   *
   * CRITICAL: ppmask must represent ALL active payloads, not just the last one.
   * We initialize ppmask=0 and then accumulate (|=) the mask for each active
   * payload. This ensures the streaming EB knows about all configured payloads.
   *
   * If VTP_PAYLOAD_EN is missing or cannot be parsed, we fall back to no
   * payloads enabled (safe default that prevents undefined behavior).
   * ======================================================================== */
  {
    const int *payload_en = vtpGetPayloadEnableArray();
    int payload_num;
    int active_count = 0;
    int mask_result;

    /* Initialize ppmask to 0 - CRITICAL: do not skip this! */
    ppmask = 0;

    if (!payload_en) {
      printf("ERROR: Unable to get payload enable array from config\n");
      printf("ERROR: Falling back to no payloads enabled (ppmask=0)\n");
    } else {
      printf("INFO: Configuring VTP payload ports from VTP_PAYLOAD_EN...\n");

      /* Iterate through all 16 possible payloads */
      for (payload_num = 1; payload_num <= 16; payload_num++) {
        /* Check if this payload is enabled (array index is payload_num-1) */
        if (payload_en[payload_num - 1] == 1) {
          printf("INFO:   Enabling payload %d (VTP_PAYLOAD_EN[%d]=1)\n",
                 payload_num, payload_num - 1);

          /* Configure this payload port */
          mask_result = vtpPayloadConfig(payload_num, ppInfo, 1, 1, 0, 1);

          /* CRITICAL: Accumulate mask using OR, not assignment */
          ppmask |= mask_result;

          active_count++;

          /* Safety check: limit to 8 active payloads (hardware constraint) */
          if (active_count > 8) {
            printf("WARNING: More than 8 payloads are active (limit reached)\n");
            printf("WARNING: Payload %d and higher will be ignored\n", payload_num);
            break;
          }
        }
      }

      printf("INFO: Configured %d active payload port(s), ppmask=0x%04X\n",
             active_count, ppmask);

      if (active_count == 0) {
        printf("WARNING: No payloads enabled in VTP_PAYLOAD_EN - check config file\n");
      }
    }
  }
  /* ======================================================================== */

  /* Update the Streaming EB configuration for the new firmware to get the correct PP Mask and ROCID
     PP mask, nstreams, frame_len (ns), ROCID, ppInfo  */
  vtpStreamingSetEbCfg(ppmask, numConnections, 0xffff, ROCID, ppInfo);
  emuData[4] = ROCID;  /* define ROCID in the EMU Connection data as well*/
  emuData[6] = numConnections;  /* Update number of connections from config */

  /* If UDP transport then disable cMsg Headers, if EJFAT then enable those headers*/
  if(netMode) {
    vtpStreamingEbDisable(VTP_STREB_CMSG_HDR_EN);
    if(enableEjfat) vtpStreamingEbEnable(VTP_STREB_EJFAT_EN);
  }

  /* Enable the Streaming EB to allow Async Events. Disable Stream processing for the moment */
  stat = vtpStreamingEbEnable(VTP_STREB_ASYNC_FIFO_EN);
  if(stat != OK)
    printf("Error in vtpStreamingEbEnable()\n");

  /* Reset the MIG - DDR memory write tagging - for Streaming Ebio */
  vtpStreamingMigReset();

  /* Reset the data links between V7 Streaming EB and the Zync TCP client
     Set the Network output mode */
  vtpStreamingEbioReset(netMode);

  /* Get Stream connection info from file. Then Setup the VTP connection registers manually and connect */
  {
    int inst;
    unsigned char ipaddr[4];
    unsigned char subnet[4];
    unsigned char gateway[4];
    unsigned char mac[6];
    unsigned char udpaddr[4], tcpaddr[4], destip[4];
    unsigned int tcpport, udpport;

    /* fix the destination IP address so it is correct (from emuip) */
    destip[3] = (emuip & 0xFF);
    destip[2] = (emuip & 0xFF00) >> 8;
    destip[1] = (emuip & 0xFF0000) >> 16;
    destip[0] = (emuip & 0xFF000000) >> 24;

    /* Get the base network port info from the initial Config file */
    vtpStreamingGetNetCfg(
          0,
          ipaddr,
          subnet,
          gateway,
          mac,
          udpaddr,
          tcpaddr,
          &udpport,
          &tcpport
      );

    /* Loop over all connections */
    for (inst=0;inst<numConnections;inst++) {

      /* Increment the Local IP and MAC address for each Network connection */
      printf("Stream # %d\n",(inst+1));
      if(inst>0) {
        ipaddr[3] = ipaddr[3] + 1;
        mac[4]    =    mac[4] + 1;
        /* emuport   = emuport   + 1; */
      }

      printf(" ipaddr=%d.%d.%d.%d\n",ipaddr[0],ipaddr[1],ipaddr[2],ipaddr[3]);
      printf(" subnet=%d.%d.%d.%d\n",subnet[0],subnet[1],subnet[2],subnet[3]);
      printf(" gateway=%d.%d.%d.%d\n",gateway[0],gateway[1],gateway[2],gateway[3]);
      printf(" mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
      printf(" udpaddr=%d.%d.%d.%d\n",udpaddr[0],udpaddr[1],udpaddr[2],udpaddr[3]);
      printf(" tcpaddr=%d.%d.%d.%d\n",tcpaddr[0],tcpaddr[1],tcpaddr[2],tcpaddr[3]);
      printf(" udpport=0x%08x  tcpport=0x%08x\n",udpport, tcpport);

      /* Set VTP connection registers */
      vtpStreamingSetNetCfg(
          inst,
          netMode,
          ipaddr,
          subnet,
          gateway,
          mac,
          destip,
          emuport,   /* DESTIPPORT from cfg */
          localport  /* local port (still from code) */
      );

      /* Read them back one more time */
      vtpStreamingGetNetCfg(
          inst,
          ipaddr,
          subnet,
          gateway,
          mac,
          udpaddr,
          tcpaddr,
          &udpport,
          &tcpport
      );
      printf(" ipaddr=%d.%d.%d.%d\n",ipaddr[0],ipaddr[1],ipaddr[2],ipaddr[3]);
      printf(" subnet=%d.%d.%d.%d\n",subnet[0],subnet[1],subnet[2],subnet[3]);
      printf(" gateway=%d.%d.%d.%d\n",gateway[0],gateway[1],gateway[2],gateway[3]);
      printf(" mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
      printf(" udpaddr=%d.%d.%d.%d\n",udpaddr[0],udpaddr[1],udpaddr[2],udpaddr[3]);
      printf(" tcpaddr=%d.%d.%d.%d\n",tcpaddr[0],tcpaddr[1],tcpaddr[2],tcpaddr[3]);
      printf(" udpport=0x%08x  tcpport=0x%08x\n",udpport, tcpport);

      /* Make the Connection - only for Client mode - to disable the cMSg connection data set last arg 8->0 */
      vtpStreamingTcpConnect(inst, (netMode+1), emuData, 0);
      /* vtpStreamingTcpAccept(inst); */
    }
  }

  /* Send a Prestart Event to each stream */
  for(ii=0;ii<numConnections;ii++) {
    vtpStreamingEvioWriteControl(ii,EV_PRESTART,rol->runNumber,rol->runType);
  }

  printf(" Done with User Prestart\n");
}


/**
                        PAUSE
**/
void
rocPause()
{
  VTPflag = 0;
  CDODISABLE(VTP, 1, 0);
  /* ADDED: stop stats thread */
  (void)vtp_stats_sender_stop();
}

/**
                        GO
**/
void
rocGo()
{
  int ii, stat;
  int numConnections = vtpGetNumConnections();

  /* ADDED: launch 1 Hz UDP status sender at GO begin */
  /* Get config values, but allow env var override for backward compatibility */
  const char *host = getenv("VTP_STATS_HOST");
  char *port_env = getenv("VTP_STATS_PORT");
  uint16_t port;
  int inst;

  if (!host || !*host) host = vtpGetStatsHost();

  if (port_env && *port_env) {
    long pv = strtol(port_env, NULL, 10);
    if (pv > 0 && pv < 65536) port = (uint16_t)pv;
    else port = (uint16_t)vtpGetStatsPort();
  } else {
    port = (uint16_t)vtpGetStatsPort();
  }

  inst = vtpGetStatsInst();

  (void)vtp_stats_sender_launch(host, port, inst);

  /* Enable the Streaming EB */
  /* vtpStreamingEbGo(); */
  /* vtpStreamingAsyncInfoWrite(8); */

  if(vtpSerdesCheckLinks() == ERROR)
  {
    daLogMsg("ERROR","VTP Serdes links not up");
  }

  printf("Calling vtpSerdesStatusAll()\n");
  vtpSerdesStatusAll();

  /*
  blklevel = vtpTiLinkGetBlockLevel(0);
  printf("Block level read from TI: %d\n", blklevel);
  */
  printf("Setting VTP block level to: %d\n", blklevel);
  vtpSetBlockLevel(blklevel);

  vtpV7SetResetSoft(1);
  vtpV7SetResetSoft(0);
  /* vtpEbResetFifo(); */

  vtpStats(0);
  vtpSDPrintScalers();

  /*Send Go Event*/
  for(ii=0;ii<numConnections;ii++) {
    vtpStreamingEvioWriteControl(ii,EV_GO,0,0);
  }

  /* Enable StreamingEB Stream processing */
  stat = vtpStreamingEbEnable(VTP_STREB_PP_STREAM_EN);
  if(stat != OK)
    printf("Error in vtpStreamingEbEnable()\n");

  /* Enable to recieve Triggers */
  CDOENABLE(VTP, 1, 0);
  VTPflag=0; /* disable polling for triggers in streaming mode */
}

/**
                        END
**/
void
rocEnd()
{
  int ii, stat, status;
  unsigned int nFrames;
  int numConnections = vtpGetNumConnections();

  VTPflag = 0;
  CDODISABLE(VTP, 1, 0);

  /* ADDED: stop stats thread */
  (void)vtp_stats_sender_stop();

  vtpStats(0);

  /* Disconnect Streaming sockets */
  /* for(inst=0;inst<NUM_VTP_CONNECTIONS;inst++) { vtpStreamingTcpConnect(inst,0); } */

  sleep(2);  /* wait a bit to make sure the VTP sends all its data */

  /* Send an End Event */
  /* Enable StreamingEB AsyncFiFo processing */
  stat = vtpStreamingEbEnable(VTP_STREB_ASYNC_FIFO_EN);
  if(stat != OK)
    printf("Error in vtpStreamingEbEnable()\n");

  /*Send End Event to instance 0*/
  for(ii=0;ii<numConnections;ii++) {
    nFrames = vtpStreamingFramesSent(ii);
    vtpStreamingEvioWriteControl(ii,EV_END,rol->runNumber,nFrames);
    printf("rocEnd: Stream %d - Wrote End Event (total %d frames)\n",ii, nFrames);
  }
  sleep(2);

  /* Disable Streaming EB - careful. If the User Sync is not high this can drop packets from a frame using UDP */
  /* vtpStreamingEbReset(); */

  /* Disconnect the Socket - Client Mode TCP connections */
  for(ii=0;ii<numConnections;ii++) {
    status = vtpStreamingTcpConnect(ii,0,0,0);
    if(status == ERROR) {
      printf("rocEnd: Error closing socket on link %d\n",ii);
    }
  }

  /* Reset all Socket Connections on the TCP Server - Server Mode only*/
  vtpStreamingTcpReset(0);

  vtpSDPrintScalers();
  /* vtpTiLinkStatus(); */
}

/**
                        READOUT
**/
void
rocTrigger(int EVTYPE)
{
  int ii;
  unsigned int evtnum = *(rol->nevents);

  /* print vtpStatistics every 100 events) */
  if((evtnum%100) == 0) {
    vtpStats();
  }

  /* Open an event, containing Banks */
  CEOPEN(ROCID, BT_BANK, blklevel);

  /* Open a trigger bank */
  CBOPEN(trigBankType, BT_SEG, blklevel);
  for(ii = 0; ii < blklevel; ii++)
  {
    if(trigBankType == 0xff11)
    {
      *rol->dabufp++ = (EVTYPE << 24) | (0x01 << 16) | (3);
    }
    else
    {
      *rol->dabufp++ = (EVTYPE << 24) | (0x01 << 16) | (1);
    }

    *rol->dabufp++ = (blklevel * (evtnum - 1) + (ii + 1));

    if(trigBankType == 0xff11)
    {
      /* Compute timestamp from frame counter: frame × 65.535 µs (in nanoseconds) */
      uint32_t fc = vtpStreamingGetEbFrameCnt(g_vtp_stats_inst);
      uint64_t ts = (uint64_t)fc * 65535ULL;
      *rol->dabufp++ = (uint32_t)(ts & 0xffffffffu);
      *rol->dabufp++ = (uint32_t)(ts >> 32);
    }
  }

  /* Close trigger bank */
  CBCLOSE;

  /* Dummy Bank of data */
  CBOPEN(0x11, BT_UI4, blklevel);
  for(ii = 0; ii < 10; ii++)
  {
    *rol->dabufp++ = ii;
  }
  CBCLOSE;

  /* If this is the first event then get the config file info and put it in a Bank */
  if(firstEvent)
  {
    char str[10000];
    int len = vtpUploadAll(str, sizeof(str)-4);
    str[len] = 0;
    str[len+1] = 0;
    str[len+2] = 0;
    str[len+3] = 0;
    firstEvent = 0;
    printf("VTP string(len = %d bytes): \n%s\n",len,str);
    CBOPEN(0x12, BT_UC1, blklevel);
    /* CBOPEN(0x12, BT_UI4, blklevel); */
    for(ii = 0; ii < (len+3)/4; ii++)
    {
      unsigned int val;
      val = ((unsigned char)str[ii*4+0] << 0)  |
            ((unsigned char)str[ii*4+1] << 8)  |
            ((unsigned char)str[ii*4+2] << 16) |
            ((unsigned char)str[ii*4+3] << 24);
      *rol->dabufp++ = val;
    }
    CBCLOSE;
  }

  /* Close event */
  CECLOSE;
}

/**
                        READOUT ACKNOWLEDGE
**/
void
rocTrigger_done()
{
  /* If No TI to acknowlege in Streaming mode set code = 1 */
  CDOACK(VTP, 1, 0);
}

/**
                        RESET
**/
void
rocReset()
{
  /* close VTP device */
  vtpClose(VTP_FPGA_OPEN|VTP_I2C_OPEN|VTP_SPI_OPEN);
}

/*
  Local Variables:
  compile-command: "make -k vtp_list.so"
  End:
 */

