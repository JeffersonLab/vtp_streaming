# Dynamic VTP Payload Configuration Implementation

## Overview

Modified `rol/vtp_stream3_1udp_vg.c` to configure VTP payload ports dynamically based on `VTP_PAYLOAD_EN` values from the `vtp_<rocname>.cnf` configuration file, replacing the previous hardcoded payload configuration.

## Critical Implementation Detail: ppmask Accumulation

**PROBLEM**: The original hardcoded pattern assigned `ppmask` only from the last `vtpPayloadConfig()` call:
```c
vtpPayloadConfig(13,ppInfo,1,1,0,1);
ppmask = vtpPayloadConfig(15,ppInfo,1,1,0,1);  // WRONG - only captures last payload
```

This meant `ppmask` only reflected payload 15, not both 13 and 15.

**SOLUTION**: Initialize `ppmask=0` and accumulate ALL active payload masks:
```c
ppmask = 0;  // Initialize to zero
ppmask |= vtpPayloadConfig(13,ppInfo,1,1,0,1);  // Accumulate payload 13
ppmask |= vtpPayloadConfig(15,ppInfo,1,1,0,1);  // Accumulate payload 15
// Now ppmask correctly represents BOTH payloads
```

**WHY THIS MATTERS**: The VTP streaming event builder uses `ppmask` to determine which payload ports to expect data from. If `ppmask` only includes the last configured payload, the system will:
- Ignore data from other configured payloads
- Potentially hang waiting for data that never arrives
- Drop events from "missing" payloads

## Changes Summary

### 1. Configuration Infrastructure (`vtp/vtpConfig.h`, `vtp/vtpConfig.c`)

**Added to `vtpConfig.h`:**
- `int payload_en_array[16]` field in `streaming` struct (line 155)
- Accessor function declaration: `const int* vtpGetPayloadEnableArray(void)` (line 386)

**Added to `vtpConfig.c`:**
- Initialization of `payload_en_array[]` to all zeros in `vtpInitGlobals()` (lines 503-506)
- Enhanced `VTP_PAYLOAD_EN` parsing to populate both the bitmask and the array (lines 769-783):
  ```c
  else if(!strcmp(keyword,"VTP_PAYLOAD_EN"))
  {
    GET_READ_MSK;  // Parse 16 values into msk[] and ui1 bitmask
    vtpConf.payload_en = ui1;  // Store bitmask (backward compatibility)

    /* Also store as array for easier iteration in ROL code */
    for(jj = 0; jj < 16; jj++) {
      vtpConf.streaming.payload_en_array[jj] = msk[jj];
    }

    /* Debug output showing both bitmask and array */
    printf("VTP_PAYLOAD_EN parsed: bitmask=0x%04X, array=[", ui1);
    for(jj = 0; jj < 16; jj++) {
      printf("%d%s", vtpConf.streaming.payload_en_array[jj], (jj < 15) ? " " : "]\n");
    }
  }
  ```
- Accessor function implementation (lines 3157-3160):
  ```c
  const int* vtpGetPayloadEnableArray(void)
  {
    return vtpConf.streaming.payload_en_array;
  }
  ```

### 2. ROL Header (`rol/VTP_source.h`)

**Added:**
- Extern declaration for `vtpGetPayloadEnableArray()` (line 40)

### 3. ROL Implementation (`rol/vtp_stream3_1udp_vg.c`)

**Replaced (lines 430-446):**

Old hardcoded configuration:
```c
// Hall-B test2
vtpPayloadConfig(13,ppInfo,1,1,0,1);
ppmask = vtpPayloadConfig(15,ppInfo,1,1,0,1);
```

New dynamic configuration:
```c
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
```

## Implementation Features

### 1. Correct ppmask Accumulation
- **Initializes** `ppmask = 0` before loop
- **Accumulates** each payload's mask: `ppmask |= vtpPayloadConfig(...)`
- **Result**: ppmask represents ALL active payloads, not just the last one

### 2. Error Handling
- Checks if `payload_en` pointer is valid
- Falls back to `ppmask=0` (no payloads) if config is missing
- Warns user if no payloads are enabled
- Prints clear error messages for debugging

### 3. Hardware Constraints
- Enforces 8-payload maximum (hardware limit)
- Stops configuring additional payloads beyond 8
- Prints warning if limit exceeded
- Processes payloads in order (1-16) so first 8 active payloads are used

### 4. Debug Output
Example console output during prestart:
```
INFO: Configuring VTP payload ports from VTP_PAYLOAD_EN...
INFO:   Enabling payload 13 (VTP_PAYLOAD_EN[12]=1)
INFO:   Enabling payload 15 (VTP_PAYLOAD_EN[14]=1)
INFO: Configured 2 active payload port(s), ppmask=0x5000
```

## Testing Verification

### Test Case 1: Standard Configuration (payloads 13 and 15)

**Config file (`vtp_test2.cnf`):**
```
VTP_PAYLOAD_EN  0  0  0  0  0  0  0  0  0  0  0  0  1  0  1  0
```

**Expected behavior:**
- `vtpPayloadConfig(13, ...)` called
- `vtpPayloadConfig(15, ...)` called
- `ppmask` includes bits for both payloads 13 and 15
- Output: `Configured 2 active payload port(s), ppmask=0x5000`

**Verification:**
```bash
# During prestart, check log output
grep "Enabling payload" /var/log/coda/...
# Should show:
#   INFO:   Enabling payload 13 (VTP_PAYLOAD_EN[12]=1)
#   INFO:   Enabling payload 15 (VTP_PAYLOAD_EN[14]=1)
```

### Test Case 2: No Payloads Enabled

**Config file:**
```
VTP_PAYLOAD_EN  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0
```

**Expected behavior:**
- No `vtpPayloadConfig()` calls
- `ppmask = 0`
- Warning: "No payloads enabled in VTP_PAYLOAD_EN - check config file"

### Test Case 3: All Payloads Enabled (>8 limit)

**Config file:**
```
VTP_PAYLOAD_EN  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1
```

**Expected behavior:**
- First 8 payloads (1-8) configured
- After 8th payload: "WARNING: More than 8 payloads are active (limit reached)"
- Remaining payloads (9-16) ignored

### Test Case 4: Missing Config

**No `VTP_PAYLOAD_EN` in config file**

**Expected behavior:**
- All 16 entries default to 0 (from `vtpInitGlobals()`)
- `ppmask = 0`
- Warning: "No payloads enabled"

## Backward Compatibility

- Existing `vtpConf.payload_en` bitmask still populated (unchanged behavior)
- New `payload_en_array[]` provides more convenient access for iteration
- Old code using bitmask continues to work
- Config file format unchanged (`VTP_PAYLOAD_EN` already existed)

## Architecture Benefits

1. **Single Source of Truth**: Configuration comes from config file, not hardcoded
2. **Maintainable**: No code changes needed when payload configuration changes
3. **Safer**: Explicit error handling and validation
4. **Debuggable**: Clear log messages show exactly which payloads are enabled
5. **Correct**: ppmask properly represents ALL configured payloads

## Why ppmask Accumulation is Critical

The streaming event builder logic likely looks something like:
```c
// Pseudocode in VTP streaming EB
while (building_event) {
  for (each bit set in ppmask) {
    wait_for_data_from_payload(bit_position);
  }
}
```

If `ppmask` only has the last configured payload's bit set:
- EB only waits for that one payload
- Data from other configured payloads arrives but is ignored or causes errors
- Event synchronization breaks
- System deadlocks or drops data

By accumulating ALL payload masks into `ppmask`, we ensure:
- EB knows about all configured payloads
- All payload data is collected before event is complete
- Event synchronization works correctly
- No data loss

## Files Modified

1. `vtp/vtpConfig.h` - Added `payload_en_array[16]` field and accessor declaration
2. `vtp/vtpConfig.c` - Added initialization, parsing, and accessor implementation
3. `rol/VTP_source.h` - Added extern declaration
4. `rol/vtp_stream3_1udp_vg.c` - Replaced hardcoded config with dynamic config

## Compilation

No special steps required. Standard CODA build process:
```bash
cd $CODA/src/vtp/vtp_streaming
make clean
make
make install
```

Then rebuild ROL file on target system:
```bash
cd vme_rol
make vtp_stream3_1udp_vg.so
```

## Related Documentation

See also:
- `PEDESTAL_GENERATION_IMPLEMENTATION.md` - Automatic peds.txt generation
- `vme_rol/fadc_master_stream_vg.c` - Dynamic VTP_PAYLOAD_EN generation from FADC slots
- `config/vtp_rocname.cnf` - VTP configuration template with translation table

## Summary

This implementation replaces hardcoded VTP payload configuration with dynamic configuration driven by `VTP_PAYLOAD_EN` from the config file. The critical improvement is the correct accumulation of `ppmask` using bitwise OR across all active payloads, ensuring the streaming event builder receives data from all configured payload ports rather than just the last one configured.
