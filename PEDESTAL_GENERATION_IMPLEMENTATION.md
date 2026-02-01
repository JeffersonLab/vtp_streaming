# Pedestal Generation and Config File Creation Implementation

## Overview

Modified `vme_rol/fadc_master_stream_vg.c` to automatically generate FADC250 pedestals and create ROC-specific configuration files during the **download transition**.

## Changes Summary

### 1. Download Transition Handling

**Location:** `rocDownload()` function (starts at line ~74 in original file)

**Failure Reporting:**
- Uses `printf("ERROR: ...")` for error messages
- Returns early from `rocDownload()` on any failure, preventing further initialization
- This signals transition failure to the CODA framework

### 2. Implementation Flow

The download transition now executes the following sequence:

```
1. Get hostname (sanitized for filename use)
   ↓
2. Execute: $CODA/linuxvme/fadc-peds/fadc250peds $CODA_DATA/<hostname>_peds.txt
   ↓
3. Parse rocname from: $CODA_DATA/<hostname>_peds.txt
   ↓
4. Generate: $CODA_CONFIG/vme_<rocname>.cnf
   ↓
5. Generate: $CODA_CONFIG/vtp_<rocname>.cnf
   ↓
6. Continue with normal FADC initialization
```

### 3. Error Handling

At any failure point, the code:
- Prints a clear ERROR message describing the problem
- Prints "ERROR: rocDownload - DOWNLOAD TRANSITION FAILED"
- Returns immediately from `rocDownload()` (prevents further initialization)
- The CODA framework interprets early return as transition failure

**Failure scenarios handled:**
- Hostname resolution failure (gethostname/uname both fail)
- Missing environment variables (CODA, CODA_DATA, CODA_CONFIG)
- fadc250peds command execution failure (non-zero exit code)
- Generated peds file missing or unreadable
- Invalid peds file format (missing/malformed first line)
- Config file write failures

## Added Helper Functions

### `get_sanitized_hostname()`
- **Purpose:** Get system hostname and sanitize for filename use
- **Method:** Try `gethostname()`, fallback to `uname()`
- **Sanitization:** Only allows `[A-Za-z0-9._-]`, replaces others with `_`
- **Returns:** 0 on success, -1 on failure

### `parse_rocname_from_peds()`
- **Purpose:** Extract ROC name from peds file first line
- **Expected format:** `FADC250_CRATE <rocname>`
- **Features:** Trims whitespace, validates format
- **Returns:** 0 on success (rocname in buffer), -1 on failure

### `generate_vme_config()`
- **Purpose:** Create `vme_<rocname>.cnf` file
- **Template source:** `config/vme_rocname.cnf`
- **Content:**
  - Standard FADC250 configuration template
  - Appends entire contents of `<hostname>_peds.txt`
- **Returns:** 0 on success, -1 on failure

### `generate_vtp_config()`
- **Purpose:** Create `vtp_<rocname>.cnf` file
- **Template source:** `config/vtp_rocname.cnf`
- **Content:** VTP streaming configuration with rocname substitution
- **Returns:** 0 on success, -1 on failure

## Example File Generation

For **hostname=daqhost1** and **rocname=test2**:

### Generated files:
```
$CODA_DATA/daqhost1_peds.txt         (by fadc250peds command)
$CODA_CONFIG/vme_test2.cnf           (VME/FADC config + pedestal data)
$CODA_CONFIG/vtp_test2.cnf           (VTP streaming config)
```

### Sample peds.txt format:
```
FADC250_CRATE test2
FADC250_SLOT 3
FADC250_ALLCH_PED  159.083  149.752  157.937  ...
FADC250_SLOT 4
FADC250_ALLCH_PED  104.905  119.968  176.521  ...
FADC250_CRATE end
```

## Testing Steps

### Success Path Testing

1. **Environment setup:**
   ```bash
   export CODA=/path/to/coda
   export CODA_DATA=/path/to/data
   export CODA_CONFIG=/path/to/config
   ```

2. **Ensure fadc250peds tool exists:**
   ```bash
   ls -l $CODA/linuxvme/fadc-peds/fadc250peds
   ```

3. **Trigger download transition:**
   - Start CODA run control
   - Configure system
   - Trigger download
   - Monitor log output for INFO messages

4. **Verify generated files:**
   ```bash
   ls -l $CODA_DATA/$(hostname)_peds.txt
   ls -l $CODA_CONFIG/vme_*.cnf
   ls -l $CODA_CONFIG/vtp_*.cnf
   ```

5. **Check log output:**
   ```
   INFO: Hostname: daqhost1
   INFO: Executing pedestal generation command:
   INFO:   /path/to/coda/linuxvme/fadc-peds/fadc250peds /path/to/data/daqhost1_peds.txt
   INFO: fadc250peds command completed successfully
   INFO: Parsed rocname='test2' from peds file
   INFO: Generating VME config file: /path/to/config/vme_test2.cnf
   INFO: Successfully created VME config file '/path/to/config/vme_test2.cnf' (6 lines from peds appended)
   INFO: Generating VTP config file: /path/to/config/vtp_test2.cnf
   INFO: Successfully created VTP config file '/path/to/config/vtp_test2.cnf'
   INFO: ============================================
   INFO: Pedestal generation and config creation complete
   INFO:   Hostname: daqhost1
   INFO:   ROC name: test2
   INFO:   Peds file: /path/to/data/daqhost1_peds.txt
   INFO:   VME config: /path/to/config/vme_test2.cnf
   INFO:   VTP config: /path/to/config/vtp_test2.cnf
   INFO: ============================================
   ```

### Failure Path Testing

#### Test 1: Missing environment variable
```bash
unset CODA_DATA
# Trigger download
# Expected: ERROR: rocDownload - CODA_DATA environment variable not set
#           ERROR: rocDownload - DOWNLOAD TRANSITION FAILED
```

#### Test 2: fadc250peds command fails
```bash
# Rename or remove fadc250peds binary temporarily
mv $CODA/linuxvme/fadc-peds/fadc250peds{,.backup}
# Trigger download
# Expected: ERROR: rocDownload - fadc250peds command failed with exit code 127
#           ERROR: rocDownload - DOWNLOAD TRANSITION FAILED
mv $CODA/linuxvme/fadc-peds/fadc250peds{.backup,}
```

#### Test 3: Invalid peds file format
```bash
# Create invalid peds file
echo "INVALID FORMAT" > $CODA_DATA/$(hostname)_peds.txt
# Trigger download
# Expected: ERROR: First line of peds file does not match expected format
#           ERROR: rocDownload - Failed to parse rocname from peds file
#           ERROR: rocDownload - DOWNLOAD TRANSITION FAILED
rm $CODA_DATA/$(hostname)_peds.txt
```

#### Test 4: Read-only config directory
```bash
chmod -w $CODA_CONFIG
# Trigger download
# Expected: ERROR: Cannot create VME config file: Permission denied
#           ERROR: rocDownload - Failed to generate VME config file
#           ERROR: rocDownload - DOWNLOAD TRANSITION FAILED
chmod +w $CODA_CONFIG
```

## Backward Compatibility

- Existing FADC initialization code unchanged
- Config file generation happens **before** FADC programming
- If pedestal generation fails, download transition aborts (safe fail)
- No changes to prestart, go, end, or trigger functions

## Code Style Consistency

- Follows existing C style conventions
- Uses existing printf() logging patterns
- Consistent indentation (2 spaces)
- Clear ERROR/INFO prefixes
- Minimal changes to existing code structure

## Dependencies

- **System calls:** gethostname(), uname(), fork(), execl(), waitpid()
- **File I/O:** fopen(), fgets(), fprintf(), access()
- **String handling:** sscanf(), snprintf(), strcmp()
- **External tool:** `$CODA/linuxvme/fadc-peds/fadc250peds`

## Security Considerations

- Hostname sanitization prevents shell injection
- Environment variables validated before use
- File paths constructed safely using snprintf()
- Command executed via fork/exec (safer than system())
- No user-controlled input in command execution

## Performance Impact

- Adds ~1-2 seconds to download transition (pedestal generation time)
- File I/O operations are minimal (small config files)
- No impact on readout performance (happens only at download)

## Future Improvements

- Add configurable pedestal generation timeout
- Support for multiple hostname formats
- Validation of generated config file syntax
- Backup of existing config files before overwriting
