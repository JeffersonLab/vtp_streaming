# User Config Parsing Implementation - Summary

## Overview

This implementation refactors the configuration system to follow the correct order of operations:

**BEFORE (Incorrect)**:
1. Run fadc250peds
2. Generate config files with hardcoded/partial VTP parameters
3. Use `rol->usrConfig` directly for FADC configuration

**AFTER (Correct)**:
1. Parse `rol->usrConfig` to extract ALL VME and VTP parameters
2. Validate all required parameters
3. Run fadc250peds
4. Generate `vme_<rocname>.cnf` using parsed VME parameters + peds data
5. Generate `vtp_<rocname>.cnf` using parsed VTP parameters + peds data
6. Use GENERATED `vme_<rocname>.cnf` for FADC configuration (NOT `rol->usrConfig`)
7. Use GENERATED `vtp_<rocname>.cnf` for VTP configuration (NOT `rol->usrConfig`)

## Key Design Principles

1. **Parse Once, Use Everywhere**: User config is parsed once at the beginning, all parameters extracted and validated
2. **Single Source of Truth**: All configuration parameters come from user config file
3. **Correct Ordering**: Parse → Validate → Generate pedestals → Create files → Use generated files
4. **No Hardcoded Values**: All values in generated files come from parsed parameters
5. **Clear Separation**: User config (input) vs. Generated configs (output for runtime use)
6. **Comprehensive Logging**: Every phase clearly logged with which files are being read/written/used

## Implementation Details

### fadc_master_stream_vg.c

#### 1. Configuration Parameter Structure

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

  /* Validation flags to ensure all required params are present */
  int have_vtp_rocid;
  int have_vtp_nframe_buf;
  /* ... one flag for each required VTP parameter ... */
} user_config_params_t;
```

This struct holds ALL parameters that need to be extracted from the user config.

#### 2. Parser Function

```c
static int parse_user_config(const char *config_file, user_config_params_t *params);
```

**Features**:
- Reads user config file line by line
- Skips comments and blank lines
- Parses both VME/FADC and VTP parameters
- Sets validation flags for required parameters
- Logs each parameter as it's parsed
- Validates that all required VTP parameters are present
- Returns -1 if any required parameter is missing

**VME Parameters Parsed**:
- FADC250_MODE
- FADC250_W_OFFSET
- FADC250_W_WIDTH
- FADC250_NSB
- FADC250_NSA
- FADC250_NPEAK
- FADC250_TET
- FADC250_DAC
- FADC250_ADC_MASK (16 values)
- FADC250_TRG_MASK (16 values)
- FADC250_TET_IGNORE_MASK (16 values)

**VTP Parameters Parsed**:
- VTP_STREAMING_ROCID (required)
- VTP_STREAMING_NFRAME_BUF (required)
- VTP_STREAMING_FRAMELEN (required)
- VTP_STREAMING (required)
- VTP_STREAMING_MAC (required)
- VTP_STREAMING_NSTREAMS (required)
- VTP_STREAMING_IPADDR (required)
- VTP_STREAMING_SUBNET (required)
- VTP_STREAMING_GATEWAY (required)
- VTP_STREAMING_DESTIP (required)
- VTP_STREAMING_DESTIPPORT (required)
- VTP_STREAMING_LOCALPORT (required)
- VTP_STREAMING_CONNECT (required)

#### 3. Modified Config Generation Functions

```c
static int generate_vme_config(const char *config_dir, const char *rocname,
                                const char *peds_file, const user_config_params_t *params);
```

**Changes**:
- Now accepts `params` pointer to parsed config
- Uses parsed VME parameters instead of hardcoded values
- Writes all FADC parameters from parsed config
- Still appends peds.txt content
- Logs that it's using parameters from user config

```c
static int generate_vtp_config(const char *config_dir, const char *rocname,
                                const char *peds_file, const user_config_params_t *params);
```

**Changes**:
- Now accepts `params` pointer to parsed config
- Uses parsed VTP parameters for all VTP_STREAMING_* values
- Still derives VTP_PAYLOAD_EN dynamically from active FADC slots in peds.txt
- Logs that it's using parameters from user config

#### 4. Updated rocDownload() Flow

```c
void rocDownload()
{
  char generated_vme_config[512];  // Store path for later use

  /* PHASE 1: Parse user configuration */
  user_config_params_t config_params;
  init_config_params(&config_params);

  if (!rol->usrConfig || !*rol->usrConfig) {
    // ERROR: no user config
    return;
  }

  if (parse_user_config(rol->usrConfig, &config_params) != 0) {
    // ERROR: parsing failed
    return;
  }

  /* PHASE 2: Generate pedestals */
  // Get hostname
  // Execute fadc250peds command
  // Verify peds file was created

  /* PHASE 3: Generate configuration files */
  // Parse rocname from peds
  // generate_vme_config(..., &config_params)
  // generate_vtp_config(..., &config_params)
  // Store path to generated VME config

  /* PHASE 4: Configure FADC using generated VME config */
  #ifdef FADC_USE_CONFIG_FILE
    fadc250Config(generated_vme_config);  // NOT rol->usrConfig
  #endif
}
```

**Key Changes**:
1. Parsing happens FIRST, before anything else
2. All parameters validated before proceeding
3. Config generation uses parsed parameters
4. FADC configuration uses GENERATED file, not user config
5. Clear phase separation with logging

### vtp_stream3_1udp_vg.c

#### 1. Helper Functions

```c
static int vtp_get_sanitized_hostname(char *hostname_buf, size_t bufsize);
```
- Gets system hostname
- Sanitizes for filename use (only allows `[A-Za-z0-9._-]`)
- Returns 0 on success, -1 on failure

```c
static int vtp_get_generated_config_path(char *path_buf, size_t bufsize);
```
- Constructs path to generated VTP config: `$CODA_CONFIG/vtp_<hostname>.cnf`
- Uses sanitized hostname to match the generated filename
- Validates `$CODA_CONFIG` environment variable exists
- Returns 0 on success, -1 on failure

#### 2. Modified rocPrestart()

```c
void rocPrestart()
{
  char vtp_config_path[512];

  if (vtp_get_generated_config_path(vtp_config_path, sizeof(vtp_config_path)) == 0)
  {
    if (access(vtp_config_path, R_OK) == 0)
    {
      // SUCCESS: Use generated VTP config
      vtpConfig(vtp_config_path);
      vtp_read_dest_from_cfg(vtp_config_path, &emuip, &emuport);
    }
    else
    {
      // FALLBACK: Generated file doesn't exist, use rol->usrConfig
      vtpConfig(rol->usrConfig);
      vtp_read_dest_from_cfg(rol->usrConfig, &emuip, &emuport);
    }
  }
  else
  {
    // FALLBACK: Path construction failed, use rol->usrConfig
    vtpConfig(rol->usrConfig);
    vtp_read_dest_from_cfg(rol->usrConfig, &emuip, &emuport);
  }
}
```

**Key Features**:
- Primary path: Use `$CODA_CONFIG/vtp_<hostname>.cnf` (generated file)
- Fallback path: Use `rol->usrConfig` if generated file doesn't exist
- Clear warning messages when falling back
- Validates file exists before attempting to read

## Example User Config File

Created `config/userConfig.txt` as a template:

```
############################################
######## VME Configuration #################
############################################

FADC250_CRATE all
FADC250_SLOT all

FADC250_ADC_MASK  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1
FADC250_TRG_MASK  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0
FADC250_TET_IGNORE_MASK 0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0

FADC250_MODE 1
FADC250_W_OFFSET  1200
FADC250_W_WIDTH  800
FADC250_NSB 8
FADC250_NSA 60
FADC250_NPEAK 1
FADC250_TET 20
FADC250_DAC 3270

############################################
######## VTP Configuration #################
############################################

VTP_STREAMING_ROCID       0
VTP_STREAMING_NFRAME_BUF  1000
VTP_STREAMING_FRAMELEN    65536
VTP_STREAMING             0

VTP_STREAMING_MAC         0xCE 0xBA 0xF0 0x03 0x00 0x9d
VTP_STREAMING_NSTREAMS    1

VTP_STREAMING_IPADDR      129  57  69 14
VTP_STREAMING_SUBNET      255 255 255   0
VTP_STREAMING_GATEWAY     129  57  69   1

VTP_STREAMING_DESTIP      129.57.177.25
VTP_STREAMING_DESTIPPORT  19522
VTP_STREAMING_LOCALPORT   10001
VTP_STREAMING_CONNECT     1
```

## Logging Output

### PHASE 1: Parse User Config
```
INFO: ============================================
INFO: PHASE 1: Parse user configuration
INFO: ============================================
INFO: Parsing user config file: /path/to/userConfig.txt
INFO:   FADC250_MODE = 1
INFO:   FADC250_W_OFFSET = 1200
...
INFO:   VTP_STREAMING_ROCID = 0
INFO:   VTP_STREAMING_DESTIP = 129.57.177.25
...
INFO: User config parsing completed successfully
```

### PHASE 2: Generate Pedestals
```
INFO: ============================================
INFO: PHASE 2: Generate pedestals
INFO: ============================================
INFO: Hostname: test2vtp
INFO: Executing pedestal generation command:
INFO:   /path/to/coda/linuxvme/fadc-peds/fadc250peds /path/to/data/test2vtp_peds.txt
INFO: fadc250peds command completed successfully
```

### PHASE 3: Generate Config Files
```
INFO: ============================================
INFO: PHASE 3: Generate configuration files
INFO: ============================================
INFO: Parsed rocname='test2' from peds file
INFO: Generating VME config file: /path/to/config/vme_test2.cnf
INFO: Using parameters from user config
INFO: Successfully created VME config file '/path/to/config/vme_test2.cnf'
INFO: Generating VTP config file: /path/to/config/vtp_test2.cnf
INFO: Using VTP parameters from user config
INFO: Successfully created VTP config file '/path/to/config/vtp_test2.cnf'
INFO: ============================================
INFO: Config file generation complete
INFO:   User config (input):  /path/to/userConfig.txt
INFO:   VME config (generated): /path/to/config/vme_test2.cnf
INFO:   VTP config (generated): /path/to/config/vtp_test2.cnf
INFO: ============================================
```

### PHASE 4: Configure FADC
```
INFO: ============================================
INFO: PHASE 4: Configure FADC using generated VME config
INFO: ============================================
INFO: Using generated VME config file: /path/to/config/vme_test2.cnf
INFO: Successfully loaded FADC250 Config from generated file
```

### VTP Configuration (in rocPrestart)
```
INFO: Using GENERATED VTP config file: /path/to/config/vtp_test2.cnf
```

## Error Handling

### Missing Required Parameter
```
ERROR: Missing required parameter: VTP_STREAMING_ROCID
ERROR: rocDownload - Failed to parse user config file
ERROR: rocDownload - DOWNLOAD TRANSITION FAILED
```

### Generated VTP Config Not Found (Fallback)
```
WARNING: Generated VTP config file '/path/to/config/vtp_test2.cnf' not found or not readable
WARNING: Falling back to user config from rol->usrConfig
```

## Benefits

1. **Correctness**: Parameters are parsed and validated before any file generation
2. **Maintainability**: No hardcoded values in generation functions
3. **Traceability**: Clear logging shows exactly which files are used
4. **Robustness**: Early validation catches missing parameters
5. **Flexibility**: User config is single source of truth
6. **Safety**: VTP has fallback mechanism
7. **Clarity**: Phases are clearly separated and logged

## Files Modified

1. `vme_rol/fadc_master_stream_vg.c` - Main implementation
2. `rol/vtp_stream3_1udp_vg.c` - VTP config loading
3. `config/userConfig.txt` - Example user config (new)
4. `USER_CONFIG_PARSING_PLAN.md` - Design doc (new)
5. `USER_CONFIG_PARSING_TESTING.md` - Test guide (new)

## Testing

See `USER_CONFIG_PARSING_TESTING.md` for comprehensive testing procedures.

## Migration Notes

### For Existing Setups

1. Create a user config file based on `config/userConfig.txt` template
2. Include ALL VME/FADC parameters (previously hardcoded)
3. Include ALL VTP_STREAMING parameters (previously hardcoded)
4. Set the user config file as `rol->usrConfig` in CODA
5. Generated files will appear in `$CODA_CONFIG/`
6. FADC will use `vme_<rocname>.cnf` (generated)
7. VTP will use `vtp_<rocname>.cnf` (generated)

### Backward Compatibility

- VTP has fallback to `rol->usrConfig` if generated file doesn't exist
- VME/FADC requires generated file (no fallback, by design)
- All default values preserved in `init_config_params()`

## Summary

This implementation establishes a clean separation between:
- **Input**: User config file (contains all parameters)
- **Processing**: Parse, validate, generate pedestals, create configs
- **Output**: Generated config files (used for runtime configuration)

The correct order of operations is now enforced, and all configuration values come from the user config file rather than being hardcoded in the generation functions.
