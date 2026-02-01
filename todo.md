# Config-Driven Parameters for vtp_stream3_1udp_vg.c - Implementation Plan

## Analysis Summary

### Hard-Coded Parameters Found in `rol/vtp_stream3_1udp_vg.c`:

**Requested for Config Migration:**
1. **z7file** (line 322): `"fe_vtp_z7_streamingv3_ejfat_v5.bin"` - Z7 FPGA firmware filename
2. **v7file** (line 325): `"fe_vtp_v7_fadc_streamingv3_ejfat.bin"` - V7 FPGA firmware filename
3. **VTP_STATS_HOST** (line 133): `"129.57.29.231"` - UDP stats destination host
4. **VTP_STATS_PORT** (line 137): `19531` - UDP stats destination port
5. **VTP_STATS_INST** (line 141): `0` - Stream instance to sample
6. **VTP_SYNC_PKT_LEN** (line 144): `28` - Sync packet length in bytes

**Additional Runtime Parameters (Proposed for Config):**
7. **NUM_VTP_CONNECTIONS** (line 51): `1` - Number of network streams (can be 1-4)
8. **VTP_NET_MODE** (line 52): `1` - Network mode (0=TCP, 1=UDP)
9. **ENABLE_EJFAT** (line 53): `1` - Enable EJFAT packet headers
10. **VTP_LOCAL_PORT** (line 365): `10001` - Local port base for connections

**Not Recommended for Config (Too Low-Level):**
- MAX_EVENT_LENGTH, MAX_EVENT_POOL, MAXBUFSIZE - These are buffer sizes that should remain compile-time constants

### Existing Config Infrastructure

**Location:** `vtp/vtpConfig.c` + `vtp/vtpConfig.h`
- Already parses `config/streaming_vtp3.cnf`
- Already reads `VTP_FIRMWARE_V7` and `VTP_FIRMWARE_Z7` into `vtpConf.fw_filename_v7` and `vtpConf.fw_filename_z7`
- Uses simple keyword-based parsing: `strcmp(keyword, "KEY")` then `sscanf(str_tmp, ...)`
- Global struct: `static VTP_CONF vtpConf;`

**Strategy:** Extend existing vtpConfig infrastructure rather than create new parser

## Implementation Plan

### 1. Extend VTP_CONF Structure (vtp/vtpConfig.h)

Add new fields to the `VTP_CONF` struct for streaming-related parameters:

```c
typedef struct {
  // ... existing fields ...

  struct {
    char stats_host[FNLEN];     // UDP stats destination host
    int stats_port;              // UDP stats destination port (1-65535)
    int stats_inst;              // Stream instance to sample (0-3)
    int sync_pkt_len;            // Sync packet length in bytes
    int num_connections;         // Number of VTP network streams (1-4)
    int net_mode;                // Network mode: 0=TCP, 1=UDP
    int enable_ejfat;            // Enable EJFAT headers: 0=off, 1=on
    int local_port;              // Local port base (0-65535)
  } streaming;

  // ... existing fields ...
} VTP_CONF;
```

### 2. Initialize Defaults (vtp/vtpConfig.c - vtpInitGlobals function)

Add initialization with sensible defaults matching current hard-coded behavior:

```c
// In vtpInitGlobals():
strcpy(vtpConf.streaming.stats_host, "129.57.29.231");
vtpConf.streaming.stats_port = 19531;
vtpConf.streaming.stats_inst = 0;
vtpConf.streaming.sync_pkt_len = 28;
vtpConf.streaming.num_connections = 1;
vtpConf.streaming.net_mode = 1;       // UDP
vtpConf.streaming.enable_ejfat = 1;
vtpConf.streaming.local_port = 10001;
```

### 3. Add Config Parsing (vtp/vtpConfig.c - vtpReadConfigFile function)

Add parsing for new keywords (around line 663-673 where VTP_FIRMWARE_* is parsed):

```c
else if(!strcmp(keyword,"VTP_STATS_HOST"))
{
  sscanf(str_tmp, "%*s %250s", vtpConf.streaming.stats_host);
  printf("VTP_STATS_HOST = %s\n", vtpConf.streaming.stats_host);
}
else if(!strcmp(keyword,"VTP_STATS_PORT"))
{
  sscanf(str_tmp, "%*s %d", &argi[0]);
  if(argi[0] > 0 && argi[0] < 65536) {
    vtpConf.streaming.stats_port = argi[0];
    printf("VTP_STATS_PORT = %d\n", argi[0]);
  } else {
    printf("WARNING: Invalid VTP_STATS_PORT %d, using default %d\n",
           argi[0], vtpConf.streaming.stats_port);
  }
}
// ... similar for other fields with validation ...
```

### 4. Add Accessor Functions (vtp/vtpLib.h + vtpLib.c or vtpConfig.c)

Create getter functions to safely access config values:

```c
// In vtpConfig.h or vtpLib.h:
const char* vtpGetStatsHost(void);
int vtpGetStatsPort(void);
int vtpGetStatsInst(void);
int vtpGetSyncPktLen(void);
int vtpGetNumConnections(void);
int vtpGetNetMode(void);
int vtpGetEnableEjfat(void);
int vtpGetLocalPort(void);
const char* vtpGetFirmwareZ7(void);
const char* vtpGetFirmwareV7(void);

// Implementations return vtpConf.streaming.* fields
```

### 5. Modify rol/vtp_stream3_1udp_vg.c

**A. Remove hard-coded firmware filenames in rocDownload():**
- Lines 322-325: Replace with calls to vtpConfig accessors
- Build full paths using $CODA env var + accessor results
- Add validation (check file is non-empty, readable if feasible)

**B. Remove #defines for stats parameters (lines 130-144):**
- Remove `#ifndef VTP_STATS_HOST` block
- Remove `#ifndef VTP_STATS_PORT` block
- Remove `#ifndef VTP_STATS_INST` block
- Remove `enum { VTP_SYNC_PKT_LEN = 28 };`

**C. Update rocGo() stats sender launch (lines 573-581):**
- Replace with accessor calls: `vtpGetStatsHost()`, `vtpGetStatsPort()`, `vtpGetStatsInst()`
- Keep env var override for backward compatibility (VTP_STATS_HOST, VTP_STATS_PORT can still override)

**D. Update other config-driven constants:**
- Lines 51-53: Remove #defines for NUM_VTP_CONNECTIONS, VTP_NET_MODE, ENABLE_EJFAT
- Replace usages with accessor calls throughout the file
- Line 365: Use `vtpGetLocalPort()` instead of hard-coded 10001

**E. Add clear error handling:**
- If config file missing, vtpConfig will use defaults (already logs warning)
- Log clear messages when using default values
- Validate firmware file paths are non-empty

### 6. Update config/streaming_vtp3.cnf

Add new configuration keys with documentation:

```
# Firmware files (already exist, just document):
# VTP_FIRMWARE_Z7           fe_vtp_z7_streamingv3_Mar02.bin
# VTP_FIRMWARE_V7           fe_vtp_v7_fadc_streamingv3_Mar15.2.bin

# UDP Statistics/Sync Sender Configuration
# Destination host for 1Hz sync packets (hostname or IP)
VTP_STATS_HOST            129.57.29.231
# Destination UDP port for sync packets (1-65535)
VTP_STATS_PORT            19531
# Stream instance to sample for statistics (0-3)
VTP_STATS_INST            0
# Sync packet length in bytes (typically 28)
VTP_SYNC_PKT_LEN          28

# Network Streaming Configuration
# Number of VTP network stream connections (1-4)
VTP_NUM_CONNECTIONS       1
# Network transport mode: 0=TCP, 1=UDP
VTP_NET_MODE              1
# Enable EJFAT packet headers: 0=disabled, 1=enabled (UDP only)
VTP_ENABLE_EJFAT          1
# Base local port number for connections (1-65535)
VTP_LOCAL_PORT            10001
```

## Validation & Testing

1. **Compilation:**
   - Verify clean compilation with no errors
   - Check all function signatures match

2. **Default Behavior:**
   - Run with existing config → should behave identically to current code
   - Verify defaults match current hard-coded values

3. **Config Override:**
   - Modify config values → verify they are read and applied
   - Test with missing keys → should use defaults + log warning

4. **Env Var Override:**
   - Set VTP_STATS_HOST, VTP_STATS_PORT env vars → should still override (backward compat)

5. **File Validation:**
   - Test with non-existent firmware files → should fail gracefully
   - Test with empty config values → should use defaults

## Backward Compatibility

- **Environment variables:** VTP_STATS_HOST and VTP_STATS_PORT env vars will still work (checked in rocGo)
- **Missing config keys:** Will use hard-coded defaults (same as current behavior)
- **Missing config file:** vtpConfig already handles this, uses all defaults
- **Behavior:** When config matches previous hard-coded values, behavior is identical

## Files Modified

1. `vtp/vtpConfig.h` - Add streaming struct to VTP_CONF
2. `vtp/vtpConfig.c` - Add initialization, parsing, and accessor functions
3. `rol/vtp_stream3_1udp_vg.c` - Remove hard-coded values, use accessors
4. `config/streaming_vtp3.cnf` - Add new configuration keys with defaults

## Additional Notes

- Keep code style consistent (indentation, printf format, error messages)
- Use existing vtpConfig error handling patterns
- All changes localized to config system and one ROL file
- No changes to VTP library core functionality
