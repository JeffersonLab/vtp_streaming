# User Config Parsing - Testing Guide

## Overview

This implementation separates user config parsing from file generation. The system now:
1. Parses ALL parameters (VME + VTP) from user config FIRST
2. Runs fadc250peds AFTER parsing
3. Generates both `vme_<rocname>.cnf` and `vtp_<rocname>.cnf` using parsed parameters
4. Uses the GENERATED files for configuration (not `rol->usrConfig`)

## Implementation Summary

### Files Modified

1. **vme_rol/fadc_master_stream_vg.c**
   - Added `user_config_params_t` struct to hold all parsed parameters
   - Added `parse_user_config()` function to parse user config file
   - Modified `generate_vme_config()` to use parsed VME parameters
   - Modified `generate_vtp_config()` to use parsed VTP parameters
   - Updated `rocDownload()` flow:
     - PHASE 1: Parse user config
     - PHASE 2: Generate pedestals (fadc250peds)
     - PHASE 3: Generate config files (using parsed params + peds)
     - PHASE 4: Configure FADC using GENERATED vme_<rocname>.cnf

2. **rol/vtp_stream3_1udp_vg.c**
   - Added helper functions to construct generated VTP config path
   - Modified `rocPrestart()` to use `$CODA_CONFIG/vtp_<hostname>.cnf`
   - Added fallback logic to use `rol->usrConfig` if generated file doesn't exist

## Test Configuration

Use the example config file: `config/userConfig.txt`

This file contains both VME/FADC and VTP parameters:
- VME/FADC: MODE, W_OFFSET, W_WIDTH, NSB, NSA, NPEAK, TET, DAC, masks
- VTP: STREAMING_ROCID, NFRAME_BUF, FRAMELEN, MAC, IPADDR, SUBNET, GATEWAY, DESTIP, ports, etc.

## Testing Procedure

### Prerequisites

1. Set environment variables:
```bash
export CODA=/path/to/coda
export CODA_DATA=/path/to/data
export CODA_CONFIG=/path/to/config
```

2. Ensure `fadc250peds` tool exists:
```bash
ls -l $CODA/linuxvme/fadc-peds/fadc250peds
```

3. Compile the modified code:
```bash
cd $CODA/src/vtp/vtp_streaming

# Compile FADC master ROL
cd vme_rol
make fadc_master_stream_vg.so

# Compile VTP ROL
cd ../rol
make vtp_stream3_1udp_vg.so
```

### Test Case 1: Normal Operation

**Objective**: Verify complete flow with valid user config

**Steps**:
1. Start CODA run control with `config/userConfig.txt` as the configuration file
2. Configure and download the run
3. Monitor console output

**Expected Output** (in order):
```
***** rocDownload() ENTERED - START OF FUNCTION *****
INFO: ============================================
INFO: PHASE 1: Parse user configuration
INFO: ============================================
INFO: Parsing user config file: /path/to/userConfig.txt
INFO:   FADC250_MODE = 1
INFO:   FADC250_W_OFFSET = 1200
INFO:   FADC250_W_WIDTH = 800
INFO:   FADC250_NSB = 8
INFO:   FADC250_NSA = 60
INFO:   FADC250_NPEAK = 1
INFO:   FADC250_TET = 20
INFO:   FADC250_DAC = 3270
INFO:   VTP_STREAMING_ROCID = 0
INFO:   VTP_STREAMING_NFRAME_BUF = 1000
INFO:   VTP_STREAMING_FRAMELEN = 65536
INFO:   VTP_STREAMING = 0
INFO:   VTP_STREAMING_MAC = CE:BA:F0:03:00:9D
INFO:   VTP_STREAMING_NSTREAMS = 1
INFO:   VTP_STREAMING_IPADDR = 129.57.69.14
INFO:   VTP_STREAMING_SUBNET = 255.255.255.0
INFO:   VTP_STREAMING_GATEWAY = 129.57.69.1
INFO:   VTP_STREAMING_DESTIP = 129.57.177.25
INFO:   VTP_STREAMING_DESTIPPORT = 19522
INFO:   VTP_STREAMING_LOCALPORT = 10001
INFO:   VTP_STREAMING_CONNECT = 1
INFO: User config parsing completed successfully
INFO: ============================================
INFO: PHASE 2: Generate pedestals
INFO: ============================================
INFO: Hostname: test2vtp
INFO: Executing pedestal generation command:
INFO:   /path/to/coda/linuxvme/fadc-peds/fadc250peds /path/to/data/test2vtp_peds.txt
INFO: fadc250peds command completed successfully
INFO: ============================================
INFO: PHASE 3: Generate configuration files
INFO: ============================================
INFO: Parsed rocname='test2' from peds file
INFO: Generating VME config file: /path/to/config/vme_test2.cnf
INFO: Using parameters from user config
INFO: Successfully created VME config file '/path/to/config/vme_test2.cnf' (6 lines from peds appended)
INFO: Generating VTP config file: /path/to/config/vtp_test2.cnf
INFO: Found active FADC slot 3 in peds file
INFO: Found active FADC slot 4 in peds file
INFO: Mapped FADC slot 3 to VTP payload 15 (enabled)
INFO: Mapped FADC slot 4 to VTP payload 13 (enabled)
INFO: Using VTP parameters from user config
INFO: Successfully created VTP config file '/path/to/config/vtp_test2.cnf'
INFO: ============================================
INFO: Config file generation complete
INFO:   User config (input):  /path/to/userConfig.txt
INFO:   Hostname: test2vtp
INFO:   ROC name: test2
INFO:   Peds file: /path/to/data/test2vtp_peds.txt
INFO:   VME config (generated): /path/to/config/vme_test2.cnf
INFO:   VTP config (generated): /path/to/config/vtp_test2.cnf
INFO: ============================================
INFO: ============================================
INFO: PHASE 4: Configure FADC using generated VME config
INFO: ============================================
INFO: Using generated VME config file: /path/to/config/vme_test2.cnf
INFO: Successfully loaded FADC250 Config from generated file
```

**VTP ROL Output** (during prestart):
```
INFO: Using GENERATED VTP config file: /path/to/config/vtp_test2.cnf
VTP_STREAMING_ROCID = 0
VTP_STREAMING_NFRAME_BUF = 1000
VTP_STREAMING_FRAMELEN = 65536
...
```

**Verification**:
1. Check generated files exist:
```bash
ls -l $CODA_CONFIG/vme_test2.cnf
ls -l $CODA_CONFIG/vtp_test2.cnf
```

2. Verify VME config contains parsed parameters:
```bash
grep "FADC250_MODE" $CODA_CONFIG/vme_test2.cnf
# Should show: FADC250_MODE 1

grep "FADC250_W_OFFSET" $CODA_CONFIG/vme_test2.cnf
# Should show: FADC250_W_OFFSET  1200

grep "FADC250_DAC" $CODA_CONFIG/vme_test2.cnf
# Should show: FADC250_DAC 3270
```

3. Verify VME config contains pedestals:
```bash
grep "FADC250_SLOT" $CODA_CONFIG/vme_test2.cnf
# Should show slot entries from peds.txt
```

4. Verify VTP config contains parsed parameters:
```bash
grep "VTP_STREAMING_ROCID" $CODA_CONFIG/vtp_test2.cnf
# Should show: VTP_STREAMING_ROCID       0

grep "VTP_STREAMING_DESTIP" $CODA_CONFIG/vtp_test2.cnf
# Should show: VTP_STREAMING_DESTIP      129.57.177.25

grep "VTP_STREAMING_MAC" $CODA_CONFIG/vtp_test2.cnf
# Should show: VTP_STREAMING_MAC         0xCE 0xBA 0xF0 0x03 0x00 0x9d
```

5. Verify VTP config contains dynamic payload enable:
```bash
grep "VTP_PAYLOAD_EN" $CODA_CONFIG/vtp_test2.cnf
# Should show: VTP_PAYLOAD_EN  0  0  0  0  0  0  0  0  0  0  0  0  1  0  1  0
# (payloads 13 and 15 enabled based on active FADC slots 3 and 4)
```

### Test Case 2: Missing Required VTP Parameter

**Objective**: Verify validation of required VTP parameters

**Steps**:
1. Create a test config file with missing `VTP_STREAMING_ROCID`
2. Attempt to download

**Expected Behavior**:
- Parsing fails with error:
```
ERROR: Missing required parameter: VTP_STREAMING_ROCID
ERROR: rocDownload - Failed to parse user config file
ERROR: rocDownload - DOWNLOAD TRANSITION FAILED
```
- Download transition fails
- No config files are generated

### Test Case 3: VTP Fallback Mode

**Objective**: Verify VTP falls back to `rol->usrConfig` if generated file doesn't exist

**Steps**:
1. Remove generated VTP config:
```bash
rm $CODA_CONFIG/vtp_test2.cnf
```
2. Start prestart phase

**Expected Output**:
```
WARNING: Generated VTP config file '/path/to/config/vtp_test2.cnf' not found or not readable
WARNING: Falling back to user config from rol->usrConfig
rocPrestart: Fallback - rol->usrConfig = /path/to/userConfig.txt
```

### Test Case 4: Verify Parameter Propagation

**Objective**: Verify that changes to user config are reflected in generated files

**Steps**:
1. Modify `userConfig.txt`:
   - Change `FADC250_DAC` from 3270 to 3300
   - Change `VTP_STREAMING_ROCID` from 0 to 5
   - Change `VTP_STREAMING_DESTIP` to a different IP

2. Re-run download

3. Check generated files:
```bash
grep "FADC250_DAC" $CODA_CONFIG/vme_test2.cnf
# Should show new value: FADC250_DAC 3300

grep "VTP_STREAMING_ROCID" $CODA_CONFIG/vtp_test2.cnf
# Should show new value: VTP_STREAMING_ROCID       5

grep "VTP_STREAMING_DESTIP" $CODA_CONFIG/vtp_test2.cnf
# Should show new IP address
```

## Key Validation Points

### 1. Correct Order of Operations
✓ User config parsed BEFORE fadc250peds runs
✓ Config files generated AFTER fadc250peds completes
✓ Generated files used for configuration (not user config)

### 2. File Generation
✓ `vme_<rocname>.cnf` created in `$CODA_CONFIG`
✓ `vtp_<rocname>.cnf` created in `$CODA_CONFIG`
✓ Both files contain parameters from user config
✓ VME config includes pedestal data from peds.txt
✓ VTP config includes dynamic VTP_PAYLOAD_EN from active FADC slots

### 3. Configuration Usage
✓ FADC configured using `$CODA_CONFIG/vme_<rocname>.cnf` (NOT `rol->usrConfig`)
✓ VTP configured using `$CODA_CONFIG/vtp_<rocname>.cnf` (NOT `rol->usrConfig`)
✓ Log messages clearly indicate which files are being used

### 4. Error Handling
✓ Missing required VTP parameters cause early failure
✓ Clear error messages for each failure point
✓ VTP has fallback to `rol->usrConfig` if generated file missing

## Troubleshooting

### Problem: "ERROR: Missing required parameter: VTP_STREAMING_XXX"
**Solution**: Add the missing parameter to your user config file. All VTP_STREAMING parameters are required.

### Problem: Generated files not created
**Solution**: Check that:
- `$CODA_CONFIG` environment variable is set
- Directory exists and is writable
- fadc250peds command completed successfully
- Rocname was parsed from peds.txt

### Problem: FADC config fails
**Solution**: Check that:
- Generated `vme_<rocname>.cnf` exists in `$CODA_CONFIG`
- File is readable
- File contains valid FADC configuration parameters

### Problem: VTP config fails
**Solution**: Check that:
- Generated `vtp_<rocname>.cnf` exists in `$CODA_CONFIG`
- File is readable
- Hostname matches rocname (or modify VTP helper function)

## Summary

The new implementation ensures:
1. **Single source of truth**: User config contains ALL parameters
2. **Correct order**: Parse → Validate → Generate pedestals → Create files → Use files
3. **Traceability**: Clear log messages show which files are parsed/generated/used
4. **Robustness**: Validation catches missing parameters early
5. **Maintainability**: No hardcoded values in generation functions
6. **Safety**: Fallback mechanisms for VTP if generated file doesn't exist
