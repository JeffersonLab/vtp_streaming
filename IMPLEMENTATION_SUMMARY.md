# Config-Driven Parameters Implementation Summary

## Overview
Successfully migrated hard-coded runtime parameters in `rol/vtp_stream3_1udp_vg.c` to config-driven values read from `config/streaming_vtp3.cnf`.

## Files Modified

### 1. vtp/vtpConfig.h
**Changes:**
- Added new `streaming` struct to `VTP_CONF` with 8 fields:
  - `stats_host[FNLEN]` - UDP stats destination host
  - `stats_port` - UDP stats destination port (1-65535)
  - `stats_inst` - Stream instance to sample (0-3)
  - `sync_pkt_len` - Sync packet length in bytes
  - `num_connections` - Number of VTP network streams (1-4)
  - `net_mode` - Network mode (0=TCP, 1=UDP)
  - `enable_ejfat` - Enable EJFAT headers (0/1)
  - `local_port` - Base local port (1-65535)

- Added 10 accessor function declarations:
  - `vtpGetStatsHost()`, `vtpGetStatsPort()`, `vtpGetStatsInst()`
  - `vtpGetSyncPktLen()`, `vtpGetNumConnections()`, `vtpGetNetMode()`
  - `vtpGetEnableEjfat()`, `vtpGetLocalPort()`
  - `vtpGetFirmwareZ7()`, `vtpGetFirmwareV7()`

### 2. vtp/vtpConfig.c
**Changes:**
- **vtpInitGlobals()**: Added initialization for all new `vtpConf.streaming.*` fields with defaults matching previous hard-coded values

- **vtpReadConfigFile()**: Added parsing for 8 new config keywords with validation:
  - `VTP_STATS_HOST` - String validation (non-empty)
  - `VTP_STATS_PORT` - Range validation (1-65535)
  - `VTP_STATS_INST` - Range validation (0-3)
  - `VTP_SYNC_PKT_LEN` - Range validation (1-1499)
  - `VTP_NUM_CONNECTIONS` - Range validation (1-4)
  - `VTP_NET_MODE` - Value validation (0 or 1)
  - `VTP_ENABLE_EJFAT` - Value validation (0 or 1)
  - `VTP_LOCAL_PORT` - Range validation (1-65535)

- **Accessor functions**: Implemented 10 getter functions at end of file

**Validation Strategy:**
- Invalid values trigger WARNING message
- Continue with default value (fail-safe behavior)
- All warnings clearly identify the problematic value

### 3. rol/vtp_stream3_1udp_vg.c
**Changes:**

#### A. Removed Hard-Coded Defines
- Lines 51-53: Removed `NUM_VTP_CONNECTIONS`, `VTP_NET_MODE`, `ENABLE_EJFAT` defines
- Lines 130-144: Removed `VTP_STATS_HOST`, `VTP_STATS_PORT`, `VTP_STATS_INST`, `VTP_SYNC_PKT_LEN` defines
- Replaced with explanatory comments referencing config file

#### B. rocDownload() - Firmware Files
- **Before**: Hard-coded strings `"fe_vtp_z7_streamingv3_ejfat_v5.bin"` and `"fe_vtp_v7_fadc_streamingv3_ejfat.bin"`
- **After**:
  - Calls `vtpGetFirmwareZ7()` and `vtpGetFirmwareV7()`
  - Added validation with fallback to defaults if empty
  - Clear WARNING messages if config values missing

#### C. rocPrestart() - Network Configuration
- **Before**: Hard-coded `int netMode=1;`, `int localport=10001;`
- **After**:
  - Added local variables: `netMode`, `localport`, `numConnections`, `enableEjfat`
  - Read from config via accessor functions
  - Updated `vtpStreamingSetEbCfg()` to use `numConnections`
  - Updated `emuData[6]` to use `numConnections`
  - Updated EJFAT enable to use `enableEjfat` variable
  - Updated all loops from `NUM_VTP_CONNECTIONS` to `numConnections`

#### D. rocGo() - Stats Sender
- **Before**: Hard-coded `VTP_STATS_HOST`, `VTP_STATS_PORT`, `VTP_STATS_INST`
- **After**:
  - Calls `vtpGetStatsHost()`, `vtpGetStatsPort()`, `vtpGetStatsInst()`
  - **Preserved backward compatibility**: Environment variables `VTP_STATS_HOST` and `VTP_STATS_PORT` still override config values
  - Added local `numConnections` variable
  - Updated loop to use `numConnections`

#### E. Stats Sender Thread - Packet Length
- **Before**: Hard-coded `VTP_SYNC_PKT_LEN` in static buffer
- **After**: Calls `vtpGetSyncPktLen()` dynamically

#### F. rocEnd() - Cleanup
- Added local `numConnections` variable
- Updated all loops to use `numConnections`

### 4. config/streaming_vtp3.cnf
**Changes:**
- Updated firmware filenames to match current defaults
- Added comprehensive documentation sections:
  - **Firmware Configuration** section
  - **UDP Statistics/Sync Sender Configuration** section with 4 new keys
  - **Network Streaming Configuration** section with 4 new keys
- Each key includes:
  - Descriptive comment explaining purpose
  - Valid range or allowed values
  - Default value

**New Configuration Keys:**
```
VTP_STATS_HOST            129.57.29.231
VTP_STATS_PORT            19531
VTP_STATS_INST            0
VTP_SYNC_PKT_LEN          28
VTP_NUM_CONNECTIONS       1
VTP_NET_MODE              1
VTP_ENABLE_EJFAT          1
VTP_LOCAL_PORT            10001
```

## Parameters Now Config-Driven

### Previously Hard-Coded (Now from Config):
1. **z7file** → `VTP_FIRMWARE_Z7` (reused existing config key)
2. **v7file** → `VTP_FIRMWARE_V7` (reused existing config key)
3. **VTP_STATS_HOST** → Config file or env var override
4. **VTP_STATS_PORT** → Config file or env var override
5. **VTP_STATS_INST** → Config file
6. **VTP_SYNC_PKT_LEN** → Config file
7. **NUM_VTP_CONNECTIONS** → `VTP_NUM_CONNECTIONS`
8. **VTP_NET_MODE** → Config file
9. **ENABLE_EJFAT** → `VTP_ENABLE_EJFAT`
10. **localport (10001)** → `VTP_LOCAL_PORT`

## Backward Compatibility

✓ **Environment Variables**: `VTP_STATS_HOST` and `VTP_STATS_PORT` env vars still work (checked in rocGo)
✓ **Missing Config Keys**: Use sensible defaults matching previous hard-coded values
✓ **Missing Config File**: vtpConfig library handles gracefully, uses all defaults
✓ **Behavior**: When config matches previous hard-coded values, behavior is identical

## Default Values (Match Previous Hard-Coded)

| Parameter | Default | Notes |
|-----------|---------|-------|
| VTP_STATS_HOST | 129.57.29.231 | indra-s2 for EJFAT forwarding |
| VTP_STATS_PORT | 19531 | UDP stats port |
| VTP_STATS_INST | 0 | First stream instance |
| VTP_SYNC_PKT_LEN | 28 | Standard EJFAT sync packet |
| VTP_NUM_CONNECTIONS | 1 | Single stream |
| VTP_NET_MODE | 1 | UDP mode |
| VTP_ENABLE_EJFAT | 1 | EJFAT headers enabled |
| VTP_LOCAL_PORT | 10001 | Base port number |

## Validation Strategy

All numeric parameters include range checking:
- **Ports**: 1-65535
- **Connections**: 1-4
- **Stream instance**: 0-3
- **Packet length**: 1-1499 (MTU safe)
- **Mode flags**: 0 or 1

Invalid values log clear WARNING and use defaults.

## Testing Notes

### Syntax Validation (Mac)
✓ `vtpConfig.c` - Compiles with 6 warnings (pre-existing style issues, no errors)
✓ `vtpConfig.h` - Syntax clean
✓ All accessor functions properly declared and implemented
✓ All accessor calls in ROL file verified

### Runtime Testing (Target Hardware Required)
The following should be tested on actual VTP hardware:

1. **Default Behavior**:
   - Run with current config → Should behave identically to previous version
   - Verify firmware loads correctly
   - Verify network connections establish

2. **Config Override**:
   - Modify `VTP_STATS_PORT` → Verify stats packets sent to new port
   - Change `VTP_NUM_CONNECTIONS` → Verify correct number of streams
   - Toggle `VTP_NET_MODE` → Verify TCP vs UDP selection

3. **Missing Keys**:
   - Remove keys from config → Should use defaults + log warnings
   - Check log output for clear warning messages

4. **Env Var Override** (backward compat):
   - Set `VTP_STATS_HOST=192.168.1.100` → Should override config
   - Set `VTP_STATS_PORT=9999` → Should override config

5. **Validation**:
   - Set `VTP_STATS_PORT=99999` → Should warn and use default
   - Set `VTP_NUM_CONNECTIONS=10` → Should warn and use default

## Code Style

- Maintained existing indentation (2 spaces in vtpConfig.c, tabs in ROL)
- Followed existing printf format style
- Used existing error handling patterns
- Preserved all comments explaining logic
- Added clear section headers in config file

## Summary Statistics

- **Files Modified**: 4
- **Lines Added**: ~250
- **Lines Removed**: ~20
- **Net Change**: ~230 lines
- **Hard-Coded Parameters Eliminated**: 10
- **New Config Keys**: 8 (2 reused existing firmware keys)
- **Accessor Functions Added**: 10
- **Breaking Changes**: 0 (fully backward compatible)

## Compilation

**Note**: Full compilation requires ARM Linux target environment with:
- Linux kernel headers (`linux/types.h`, `linux/i2c-dev.h`, etc.)
- CODA environment ($CODA set)
- ARM cross-compilation toolchain

Syntax validation successful on Mac using gcc.
