# VTP Streaming Configuration System

CODA 3.0 VTP/FADC streaming readout with automatic pedestal generation and dynamic configuration file management.

## Overview

This system automatically generates FADC pedestals and creates runtime configuration files during the CODA download transition. It separates user-defined configuration parameters from runtime-generated configurations, ensuring proper ordering and parameter propagation.

## Architecture

### Configuration Flow

```
User Config (input)
    ↓
Parse ALL parameters (VME + VTP)
    ↓
Validate required parameters
    ↓
Generate pedestals (fadc250peds)
    ↓
Generate runtime configs:
  - vme_<rocname>.cnf (VME/FADC settings + pedestals)
  - vtp_<rocname>vtp.cnf (VTP streaming settings + dynamic payload enable)
    ↓
Use generated configs for runtime configuration
```

### Generated File Naming Convention

Two runtime configuration files are created during the download transition.
Their names are built from `<rocname>`, which is the short hostname of the
ROC (e.g. `test2`).

| File | Pattern | Example |
|------|---------|---------|
| VME / FADC config | `vme_<rocname>.cnf` | `vme_test2.cnf` |
| VTP config | `vtp_<rocname>vtp.cnf` | `vtp_test2vtp.cnf` |

Both files are written into the directory given by `$CODA_CONFIG`.

**How `<rocname>` is determined — producer vs consumer:**

- **Producer** (`fadc_master_stream_vg.c`): reads `<rocname>` from the first
  line of the pedestals file (`$CODA_DATA/<hostname>_peds.txt`).  That line
  has the format `FADC250_CRATE <rocname>`, where `<rocname>` is the short
  hostname written by `fadc250peds`.

- **Consumer** (`vtp_stream3_1udp_vg.c`): derives the same value
  independently at runtime by calling `gethostname()` and then
  (1) stripping any trailing whitespace/newlines,
  (2) removing the domain suffix (everything from the first `.` onward), and
  (3) replacing any remaining non-alphanumeric characters (other than `_`
  and `-`) with `_`.

The two sides must agree.  As long as the short hostname reported by the
kernel matches the `FADC250_CRATE` value in the pedestals file, the consumer
will find the file the producer created.

### Key Design Choices

**1. Parse Before Generate**
- User config is parsed FIRST to extract all VME and VTP parameters
- All required parameters are validated before any file generation
- Ensures consistency and catches configuration errors early

**2. Single Source of Truth**
- All configuration parameters defined in user config file (`config/userConfig.txt`)
- No hardcoded values in runtime configuration generation
- Changes to user config automatically propagate to generated files

**3. Generated Files for Runtime**
- FADC configured using generated `$CODA_CONFIG/vme_<rocname>.cnf`
- VTP configured using generated `$CODA_CONFIG/vtp_<rocname>vtp.cnf`
- User config is input only; runtime uses generated files

**4. Dynamic Payload Configuration**
- VTP payload ports configured based on `VTP_PAYLOAD_EN` from config
- `VTP_PAYLOAD_EN` dynamically generated from active FADC slots in pedestals
- Slot-to-payload translation ensures correct mapping

**5. Correct ppmask Accumulation**
- Payload mask (`ppmask`) accumulated across ALL active payloads using `|=` operator
- Ensures streaming event builder receives data from all configured payloads
- Critical for proper event synchronization

## Files

### Configuration
- `config/userConfig.txt` - User-defined configuration (VME + VTP parameters)
- `$CODA_CONFIG/vme_<rocname>.cnf` - Generated FADC config (runtime)
- `$CODA_CONFIG/vtp_<rocname>vtp.cnf` - Generated VTP config (runtime)
- `$CODA_DATA/<hostname>_peds.txt` - Generated pedestals

### Readout Lists
- `vme_rol/fadc_master_stream_vg.c` - FADC master ROL with config generation
- `rol/vtp_stream3_1udp_vg.c` - VTP streaming ROL with dynamic payload config

### Libraries
- `vtp/vtpLib.{h,c}` - VTP hardware interface
- `vtp/vtpConfig.{h,c}` - VTP configuration parsing with streaming parameters
- `fadc250/fadc250Config.{h,c}` - FADC configuration parsing

## Usage

### Environment Setup
```bash
export CODA=/path/to/coda
export CODA_DATA=/path/to/data
export CODA_CONFIG=/path/to/config
```

### Configuration File
Create user config based on `config/userConfig.txt` template:
```
# VME/FADC parameters
FADC250_MODE 1
FADC250_W_OFFSET 1200
FADC250_DAC 3270
...

# VTP parameters
VTP_STREAMING_ROCID 0
VTP_STREAMING_DESTIP 129.57.177.25
VTP_STREAMING_MAC 0xCE 0xBA 0xF0 0x03 0x00 0x9d
...
```

### Build
```bash
cd vme_rol && make fadc_master_stream_vg.so
cd ../rol && make vtp_stream3_1udp_vg.so
```

### Run
Point CODA run control to your user config file. During download transition:
1. System parses user config and validates parameters
2. Executes `fadc250peds` to generate pedestals
3. Creates `vme_<rocname>.cnf` and `vtp_<rocname>vtp.cnf` in `$CODA_CONFIG`
4. FADC and VTP configured using generated files

## Key Features

- **Automatic pedestal generation** during download transition
- **Dynamic payload configuration** based on active FADC slots
- **Parameter validation** catches missing/invalid config early
- **Comprehensive logging** shows which files are used at each phase
- **Fallback support** for VTP if generated config missing
- **ROC name discovery** from pedestal file format

## Requirements

- CODA 3.0 framework
- `fadc250peds` tool in `$CODA/linuxvme/fadc-peds/`
- VTP firmware: streamingv3 with EJFAT support
- FADC250 modules in VME crate

## Version

VTP v3.10.2 with streaming configuration system
