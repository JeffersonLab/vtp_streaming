# User Config Parsing Implementation Plan

## Goal
Separate user config parsing from file generation. Parse ALL parameters (VME + VTP) from user config FIRST, then generate both vme_<rocname>.cnf and vtp_<rocname>.cnf, and use the generated files for configuration.

## Current Flow (WRONG)
1. Run fadc250peds
2. Generate config files with hardcoded/partial VTP parameters
3. Use rol->usrConfig directly for FADC config

## New Flow (CORRECT)
1. Parse rol->usrConfig to extract ALL VME and VTP parameters
2. Validate all required parameters
3. Run fadc250peds
4. Generate vme_<rocname>.cnf using parsed VME parameters + peds data
5. Generate vtp_<rocname>.cnf using parsed VTP parameters + peds data
6. Use generated vme_<rocname>.cnf for FADC config (NOT rol->usrConfig)
7. Use generated vtp_<rocname>.cnf for VTP config (NOT rol->usrConfig)

## Implementation Steps

### 1. Create config parameter struct (fadc_master_stream_vg.c)
```c
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
} user_config_params_t;
```

### 2. Add parser function
```c
static int parse_user_config(const char *config_file, user_config_params_t *params);
```

### 3. Modify generate_vme_config() to accept params
```c
static int generate_vme_config(
  const char *config_dir,
  const char *rocname,
  const char *peds_file,
  const user_config_params_t *params);
```

### 4. Modify generate_vtp_config() to accept params
```c
static int generate_vtp_config(
  const char *config_dir,
  const char *rocname,
  const char *peds_file,
  const user_config_params_t *params);
```

### 5. Update rocDownload() flow
- Parse user config FIRST
- Validate all parameters
- Run fadc250peds
- Generate both config files
- Use generated vme_<rocname>.cnf for FADC config

### 6. Update vtp_stream3_1udp_vg.c
- Use $CODA_CONFIG/vtp_<rocname>.cnf instead of rol->usrConfig
- Need to determine rocname (can use hostname-based logic)

## Files to Modify
1. vme_rol/fadc_master_stream_vg.c - Main implementation
2. rol/vtp_stream3_1udp_vg.c - Use generated VTP config
