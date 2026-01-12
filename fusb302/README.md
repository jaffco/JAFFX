 # FUSB302 USB-C PHY Integration

USB-C Power Delivery controller integration for Daisy Seed, enabling power profile detection and negotiation.

## Hardware Configuration
- **Chip**: FUSB302A (Revision B) at I2C address 0x22
- **I2C4**: D13 (SCL), D14 (SDA) - 400kHz, blocking mode
- **INT Pin**: D27 (GPIO input for interrupt monitoring)
- **USB-C Jack**: Separate from Daisy's programming USB port

## Current Status

### ✅ Completed (Phase 1: Basic Communication)
- [x] I2C4 peripheral configured and operational
- [x] Device detection and identification working
  - Device ID: 0x81 (FUSB302A Version A, Revision B)
  - All registers readable/writable
  - Power-on and reset sequences functional
- [x] Serial logging via hardware.PrintLine()
- [x] Register dump utility for diagnostics
- [x] Interrupt pin monitoring (GPIO polling)

### 🚧 In Progress (Phase 2: CC Detection & PD Protocol)
- [ ] CC line detection for cable orientation
- [ ] BMC receiver configuration
- [ ] USB-PD protocol stack integration from [usb-c-arduino](https://github.com/graycatlabs/usb-c-arduino)
- [ ] Source Capabilities (PDO) parsing
- [ ] Power profile enumeration from [USB-C-Explorer](https://github.com/ReclaimerLabs/USB-C-Explorer)

### 📋 TODO (Phase 3: Advanced Features)
- [ ] BC1.2 detection for legacy chargers
- [ ] Power role swap support
- [ ] Data role configuration
- [ ] VCONN management
- [ ] PD message transmission
- [ ] GoodCRC automatic response

## Register Status (Working)
```
DEVICE_ID (0x01): 0x81 ✓
POWER     (0x0B): 0x0F (all blocks enabled)
STATUS0   (0x40): 0x80 (VBUSOK detected)
STATUS1   (0x41): 0x28 (chip operational)
CONTROL2  (0x08): 0x08 (BMC receiver ready)
```

## References
- **Datasheet**: [fusb302/datasheet.pdf](datasheet.pdf)
- **Protocol Stack**: [usb-c-arduino](https://github.com/graycatlabs/usb-c-arduino) (Chromium EC port)
- **Power Profiles**: [USB-C-Explorer](https://github.com/ReclaimerLabs/USB-C-Explorer/blob/main/firmware/USB-C%20Explorer/USB-C%20Explorer/main.c#L260)
- **BC1.2 Detection**: [USB-C-Explorer BC1.2](https://github.com/ReclaimerLabs/USB-C-Explorer/blob/main/firmware/USB-C%20Explorer/USB-C%20Explorer/main.c#L420)

## Build & Flash
```bash
cd path/to/JAFFX
./run.sh fusb302/fusb302.cpp
```