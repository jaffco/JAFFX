#include "../../Jaffx.hpp"

// FUSB302 Register Definitions
#define REG_DEVICE_ID   0x01
#define REG_SWITCHES0   0x02
#define REG_SWITCHES1   0x03
#define REG_MEASURE     0x04
#define REG_CONTROL0    0x06
#define REG_CONTROL1    0x07
#define REG_CONTROL2    0x08
#define REG_CONTROL3    0x09
#define REG_MASK        0x0A
#define REG_POWER       0x0B
#define REG_RESET       0x0C
#define REG_MASKA       0x0E
#define REG_MASKB       0x0F
#define REG_STATUS0A    0x3C
#define REG_STATUS1A    0x3D
#define REG_INTERRUPTA  0x3E
#define REG_INTERRUPTB  0x3F
#define REG_STATUS0     0x40
#define REG_STATUS1     0x41
#define REG_INTERRUPT   0x42
#define REG_FIFOS       0x43

// SWITCHES0 bits (correct per datasheet)
#define SWITCHES0_VCONN_CC2     (1<<7)
#define SWITCHES0_VCONN_CC1     (1<<6)
#define SWITCHES0_MEAS_CC2      (1<<5)
#define SWITCHES0_MEAS_CC1      (1<<4)
#define SWITCHES0_PDWN2         (1<<3)  // Pull-down on CC2
#define SWITCHES0_PDWN1         (1<<2)  // Pull-down on CC1
#define SWITCHES0_PU_EN2        (1<<1)  // Pull-up on CC2
#define SWITCHES0_PU_EN1        (1<<0)  // Pull-up on CC1
// Legacy aliases
#define SWITCHES0_CC2_PD_EN     SWITCHES0_PDWN2
#define SWITCHES0_CC1_PD_EN     SWITCHES0_PDWN1
#define SWITCHES0_CC2_PU_EN     SWITCHES0_PU_EN2
#define SWITCHES0_CC1_PU_EN     SWITCHES0_PU_EN1

// SWITCHES1 bits
#define SWITCHES1_POWERROLE     (1<<7)
#define SWITCHES1_SPECREV1      (1<<6)
#define SWITCHES1_SPECREV0      (1<<5)
#define SWITCHES1_DATAROLE      (1<<4)
#define SWITCHES1_AUTO_GCRC     (1<<2)
#define SWITCHES1_TXCC2_EN      (1<<1)
#define SWITCHES1_TXCC1_EN      (1<<0)

// CONTROL2 bits
#define CONTROL2_MODE_DFP       (0x3 << 1)
#define CONTROL2_MODE_UFP       (0x2 << 1)
#define CONTROL2_MODE_DRP       (0x1 << 1)
#define CONTROL2_TOGGLE         (1<<0)

// CONTROL0 bits
#define CONTROL0_TX_START       (1<<5)
#define CONTROL0_AUTO_PRE       (1<<2)
#define CONTROL0_HOST_CUR_MASK  (0x3<<0) // bits 1:0 for HOST_CUR

// STATUS0 bits
#define STATUS0_VBUSOK          (1<<7)
#define STATUS0_ACTIVITY        (1<<6)
#define STATUS0_COMP            (1<<5)
#define STATUS0_CRC_CHK         (1<<4)
#define STATUS0_ALERT           (1<<3)
#define STATUS0_WAKE            (1<<2)
#define STATUS0_BC_LVL_MASK     (0x03)

// STATUS1 bits
#define STATUS1_RXSOP2DB        (1<<7)
#define STATUS1_RXSOP1DB        (1<<6)
#define STATUS1_RX_EMPTY        (1<<5)
#define STATUS1_RX_FULL         (1<<4)
#define STATUS1_TX_EMPTY        (1<<3)
#define STATUS1_TX_FULL         (1<<2)

// STATUS1A bits
#define STATUS1A_TOGSS_MASK     (0x07 << 3)
#define STATUS1A_TOGSS_RUNNING  (0x0 << 3)
#define STATUS1A_TOGSS_SRC1     (0x1 << 3)
#define STATUS1A_TOGSS_SRC2     (0x2 << 3)
#define STATUS1A_TOGSS_SNK1     (0x5 << 3)
#define STATUS1A_TOGSS_SNK2     (0x6 << 3)

// PD Message types
#define PD_DATA_SOURCE_CAP      1
#define PD_DATA_REQUEST         2
#define PD_CTRL_GOODCRC         1
#define PD_CTRL_GETSRCCAP       7
#define PD_CTRL_ACCEPT          3
#define PD_CTRL_REJECT          4
#define PD_CTRL_PS_RDY          6

// FIFO tokens
#define FUSB302_TKN_TXON        0xA1
#define FUSB302_TKN_SOP1        0x12
#define FUSB302_TKN_SOP2        0x13
#define FUSB302_TKN_SOP3        0x1B
#define FUSB302_TKN_RESET1      0x15
#define FUSB302_TKN_RESET2      0x16
#define FUSB302_TKN_PACKSYM     0x80
#define FUSB302_TKN_JAM_CRC     0xFF
#define FUSB302_TKN_EOP         0x14
#define FUSB302_TKN_TXOFF       0xFE

// PDO definitions
#define PDO_TYPE_FIXED          (0 << 30)
#define PDO_TYPE_BATTERY        (1 << 30)
#define PDO_TYPE_VARIABLE       (2 << 30)
#define PDO_TYPE_MASK           (3 << 30)

class FUSB302 : public Jaffx::Firmware {
    I2CHandle i2c4;
    GPIO intPin;
    GPIO vbusPin;  // D28 - VBUS sense
    bool chipDetected = false;
    uint32_t lastPrintTime = 0;
    uint32_t lastPDCheck = 0;
    
    // CC detection state
    int activeCC = 0;  // 0=none, 1=CC1, 2=CC2
    bool ccDetected = false;
    
    // PD state
    uint32_t srcCaps[7];  // Up to 7 PDOs
    int srcCapCount = 0;
    bool capsReceived = false;
    
    void InitI2C4() {
        I2CHandle::Config i2c_config;
        i2c_config.periph = I2CHandle::Config::Peripheral::I2C_4;
        i2c_config.speed  = I2CHandle::Config::Speed::I2C_400KHZ;
        i2c_config.mode   = I2CHandle::Config::Mode::I2C_MASTER;
        i2c_config.pin_config.scl = DaisySeed::GetPin(13); // D13 - SCL
        i2c_config.pin_config.sda = DaisySeed::GetPin(14); // D14 - SDA

        i2c4.Init(i2c_config);
    }
    
    void InitInterruptPin() {
        intPin.Init(DaisySeed::GetPin(27), GPIO::Mode::INPUT, GPIO::Pull::PULLUP); // D27 - INT
    }
    
    void InitVBusPin() {
        vbusPin.Init(DaisySeed::GetPin(28), GPIO::Mode::INPUT, GPIO::Pull::NOPULL); // D28 - VBUS
    }
    
    bool ReadVBus() {
        return vbusPin.Read(); // Returns true if VBUS is present (5V)
    }
    
    // Detect which CC line is active
    int DetectCC() {
        // Debug: Check current state
        uint8_t power_before = FUSB302_Read(REG_POWER);
        uint8_t switches0_before = FUSB302_Read(REG_SWITCHES0);
        uint8_t status0_before = FUSB302_Read(REG_STATUS0);
        hardware.PrintLine("Before: POWER=0x%02X SWITCHES0=0x%02X STATUS0=0x%02X", 
                          power_before, switches0_before, status0_before);
        
        // Enable pull-downs on both CC lines first
        uint8_t switches0_val = SWITCHES0_PDWN1 | SWITCHES0_PDWN2;
        hardware.PrintLine("Writing SWITCHES0 = 0x%02X (PDWN1|PDWN2)", switches0_val);
        FUSB302_Write(REG_SWITCHES0, switches0_val);
        
        // Power on measurement block
        uint8_t power = FUSB302_Read(REG_POWER);
        hardware.PrintLine("Current POWER = 0x%02X, setting PWR_MEASURE", power);
        FUSB302_Write(REG_POWER, power | 0x04); // PWR_MEASURE bit
        
        System::Delay(50); // Allow pull-downs to stabilize (longer delay)
        
        // Measure CC1
        switches0_val = SWITCHES0_PDWN1 | SWITCHES0_PDWN2 | SWITCHES0_MEAS_CC1;
        hardware.PrintLine("Measuring CC1: SWITCHES0 = 0x%02X", switches0_val);
        FUSB302_Write(REG_SWITCHES0, switches0_val);
        System::Delay(50);
        uint8_t status0_cc1 = FUSB302_Read(REG_STATUS0);
        uint8_t bc_lvl_cc1 = status0_cc1 & STATUS0_BC_LVL_MASK;
        hardware.PrintLine("  CC1: STATUS0=0x%02X BC_LVL=%d", status0_cc1, bc_lvl_cc1);
        
        // Measure CC2
        switches0_val = SWITCHES0_PDWN1 | SWITCHES0_PDWN2 | SWITCHES0_MEAS_CC2;
        hardware.PrintLine("Measuring CC2: SWITCHES0 = 0x%02X", switches0_val);
        FUSB302_Write(REG_SWITCHES0, switches0_val);
        System::Delay(50);
        uint8_t status0_cc2 = FUSB302_Read(REG_STATUS0);
        uint8_t bc_lvl_cc2 = status0_cc2 & STATUS0_BC_LVL_MASK;
        hardware.PrintLine("  CC2: STATUS0=0x%02X BC_LVL=%d", status0_cc2, bc_lvl_cc2);
        
        hardware.PrintLine("CC1 BC_LVL: %d, CC2 BC_LVL: %d", bc_lvl_cc1, bc_lvl_cc2);
        
        // BC_LVL > 0 indicates connection
        if (bc_lvl_cc1 > 0 && bc_lvl_cc2 == 0) {
            return 1; // CC1 active
        } else if (bc_lvl_cc2 > 0 && bc_lvl_cc1 == 0) {
            return 2; // CC2 active
        }
        
        return 0; // No connection or both (error)
    }
    
    void ConfigureBMCReceiver(int cc) {
        hardware.PrintLine("Configuring BMC receiver on CC%d...", cc);
        
        // Stop any toggle operation first
        FUSB302_Write(REG_CONTROL2, 0x00);
        System::Delay(10);
        
        // Configure SWITCHES0: Enable pull-downs on BOTH CC lines (sink mode)
        // and select which line to measure for BMC RX
        uint8_t switches0 = SWITCHES0_PDWN1 | SWITCHES0_PDWN2; // Pull-downs on both lines
        if (cc == 1) {
            switches0 |= SWITCHES0_MEAS_CC1; // Measure/RX on CC1
            // VCONN on the opposite line (CC2) is handled by source, not sink
        } else {
            switches0 |= SWITCHES0_MEAS_CC2; // Measure/RX on CC2
            // VCONN on the opposite line (CC1) is handled by source, not sink
        }
        
        hardware.PrintLine("  Setting SWITCHES0 = 0x%02X (PDWN1|PDWN2|MEAS_CC%d)", switches0, cc);
        FUSB302_Write(REG_SWITCHES0, switches0);
        
        // Configure SWITCHES1 for PD communication
        // Enable Auto GoodCRC response and TX on active CC
        uint8_t switches1 = SWITCHES1_AUTO_GCRC | SWITCHES1_SPECREV0; // PD 2.0
        if (cc == 1) {
            switches1 |= SWITCHES1_TXCC1_EN;
        } else {
            switches1 |= SWITCHES1_TXCC2_EN;
        }
        
        hardware.PrintLine("  Setting SWITCHES1 = 0x%02X (AUTO_GCRC|TXCC%d_EN)", switches1, cc);
        FUSB302_Write(REG_SWITCHES1, switches1);
        
        // Set CONTROL0 for TX configuration
        // TX_START (bit 5) + AUTO_PRE (bit 2)
        FUSB302_Write(REG_CONTROL0, CONTROL0_TX_START | CONTROL0_AUTO_PRE);
        
        // Enable BMC receiver in CONTROL2 - UFP mode enables RX automatically
        hardware.PrintLine("  Setting CONTROL2 = 0x%02X (UFP mode)", CONTROL2_MODE_UFP);
        FUSB302_Write(REG_CONTROL2, CONTROL2_MODE_UFP);
        
        // Flush RX FIFO to start fresh
        uint8_t control1 = FUSB302_Read(REG_CONTROL1);
        FUSB302_Write(REG_CONTROL1, control1 | (1<<2)); // RX_FLUSH
        System::Delay(10);
        
        // Enable relevant interrupts
        FUSB302_Write(REG_MASK, 0xFE);      // Unmask BC_LVL (bit 0)
        FUSB302_Write(REG_MASKA, 0xE6);     // Unmask important events
        FUSB302_Write(REG_MASKB, 0xFE);     // Unmask GCRCSENT (bit 0)
        
        hardware.PrintLine("BMC receiver configured - sink mode with Rd on both CC lines");
    }
    
    // Send a PD control message
    void SendPDControl(uint8_t msgType) {
        hardware.PrintLine("Sending PD control message type %d...", msgType);
        
        // Clear any pending interrupts first
        uint8_t int_a = FUSB302_Read(REG_INTERRUPTA);
        uint8_t int_b = FUSB302_Read(REG_INTERRUPTB);
        uint8_t int_ = FUSB302_Read(REG_INTERRUPT);
        if (int_a || int_b || int_) {
            hardware.PrintLine("  Cleared interrupts: A=0x%02X B=0x%02X I=0x%02X", int_a, int_b, int_);
        }
        
        // Flush TX FIFO
        uint8_t control1 = FUSB302_Read(REG_CONTROL1);
        FUSB302_Write(REG_CONTROL1, control1 | (1<<0)); // TX_FLUSH
        System::Delay(1);
        
        // Build PD header (16 bits) using correct USB-PD format
        // PD_HEADER(type, prole, drole, id, cnt, rev, ext)
        // Bits: type[3:0] | rev[7:6] | drole[5] | prole[8] | id[11:9] | cnt[14:12] | ext[15]
        uint16_t header = (msgType & 0x0F);      // Message type in bits 3:0
        header |= (1 << 6);                      // Spec revision = 01 (PD 2.0) at bits 7:6
        header |= (0 << 5);                      // Data role = 0 (UFP) at bit 5
        header |= (0 << 8);                      // Power role = 0 (Sink) at bit 8
        header |= (0 << 9);                      // Message ID = 0 at bits 11:9
        header |= (0 << 12);                     // Number of data objects = 0 at bits 14:12
        header |= (0 << 15);                     // Extended = 0 at bit 15
        
        hardware.PrintLine("  Header: 0x%04X (type=%d prole=0 drole=0 id=0 cnt=0 rev=1 ext=0)", 
                          header, msgType);
        
        // Write FIFO tokens
        uint8_t fifo[8];
        int idx = 0;
        
        // SOP sequence (SOP1, SOP1, SOP2, SOP3)
        fifo[idx++] = FUSB302_TKN_SOP1;
        fifo[idx++] = FUSB302_TKN_SOP1;
        fifo[idx++] = FUSB302_TKN_SOP2;
        fifo[idx++] = FUSB302_TKN_SOP3;
        
        // PACKSYM token with byte count (2 bytes for header only)
        fifo[idx++] = FUSB302_TKN_PACKSYM | 2;
        
        // Header (little-endian)
        fifo[idx++] = header & 0xFF;
        fifo[idx++] = (header >> 8) & 0xFF;
        
        // JAM_CRC, EOP, TXOFF
        fifo[idx++] = FUSB302_TKN_JAM_CRC;
        
        // Write to FIFO
        for (int i = 0; i < idx; i++) {
            FUSB302_Write(REG_FIFOS, fifo[i]);
        }
        
        // EOP and TXOFF
        FUSB302_Write(REG_FIFOS, FUSB302_TKN_EOP);
        FUSB302_Write(REG_FIFOS, FUSB302_TKN_TXOFF);
        
        // Trigger transmission
        FUSB302_Write(REG_FIFOS, FUSB302_TKN_TXON);
        
        hardware.PrintLine("Message queued, waiting for transmission...");
        System::Delay(50); // Wait for transmission
        
        // Check status
        uint8_t status1 = FUSB302_Read(REG_STATUS1);
        hardware.PrintLine("  Post-TX STATUS1: 0x%02X (TX_EMPTY=%d)", 
                          status1, (status1 & STATUS1_TX_EMPTY) ? 1 : 0);
        
        // Read interrupts again to see what happened
        int_a = FUSB302_Read(REG_INTERRUPTA);
        int_b = FUSB302_Read(REG_INTERRUPTB);
        int_ = FUSB302_Read(REG_INTERRUPT);
        if (int_a || int_b || int_) {
            hardware.PrintLine("  TX interrupts: A=0x%02X B=0x%02X I=0x%02X", int_a, int_b, int_);
        }
    }
    
    bool ReadPDMessage(uint8_t* header, uint32_t* data, int* dataLen) {
        // Check if RX FIFO has data
        uint8_t status1 = FUSB302_Read(REG_STATUS1);
        hardware.PrintLine("  ReadPDMessage: STATUS1=0x%02X RX_EMPTY=%d", 
                          status1, (status1 & STATUS1_RX_EMPTY) ? 1 : 0);
        
        if (status1 & STATUS1_RX_EMPTY) {
            return false; // No data
        }
        
        hardware.PrintLine("  FIFO has data! Reading...");
        
        // Read tokens from FIFO until we get a complete message
        uint8_t token;
        uint8_t fifo[64];
        int fifoLen = 0;
        
        // Read FIFO
        for (int i = 0; i < 40; i++) { // Max iterations to prevent infinite loop
            token = FUSB302_Read(REG_FIFOS);
            fifo[fifoLen++] = token;
            
            // Check for SOP token (0xE0 - SOP, 0xC0 - SOP', 0xA0 - SOP'')
            if ((token & 0xE0) == 0xE0) {
                // Found SOP, read header (2 bytes)
                if (fifoLen >= 3) {
                    header[0] = fifo[fifoLen-2];
                    header[1] = fifo[fifoLen-1];
                    
                    // Decode header
                    int numDataObjs = (header[0] >> 4) & 0x07;
                    
                    // Read data objects
                    for (int j = 0; j < numDataObjs * 4; j++) {
                        if (fifoLen < 64) {
                            fifo[fifoLen++] = FUSB302_Read(REG_FIFOS);
                        }
                    }
                    
                    // Read CRC (4 bytes) and EOP (1 byte)
                    for (int j = 0; j < 5; j++) {
                        if (fifoLen < 64) {
                            fifo[fifoLen++] = FUSB302_Read(REG_FIFOS);
                        }
                    }
                    
                    // Extract data objects
                    int dataStart = 3; // After SOP and 2-byte header
                    for (int j = 0; j < numDataObjs; j++) {
                        data[j] = fifo[dataStart] | 
                                 (fifo[dataStart+1] << 8) | 
                                 (fifo[dataStart+2] << 16) | 
                                 (fifo[dataStart+3] << 24);
                        dataStart += 4;
                    }
                    *dataLen = numDataObjs;
                    
                    return true;
                }
            }
            
            // Check if FIFO is empty
            status1 = FUSB302_Read(REG_STATUS1);
            if (status1 & STATUS1_RX_EMPTY) {
                break;
            }
        }
        
        return false;
    }
    
    void ParseSourceCaps(uint32_t* caps, int count) {
        hardware.PrintLine("\n=== Source Capabilities Received ===");
        hardware.PrintLine("PDO Count: %d\n", count);
        
        for (int i = 0; i < count; i++) {
            uint32_t pdo = caps[i];
            uint32_t type = pdo & PDO_TYPE_MASK;
            
            if (type == PDO_TYPE_FIXED) {
                // Fixed PDO: bits 19-10 = voltage (50mV units), bits 9-0 = current (10mA units)
                uint32_t voltage_mv = ((pdo >> 10) & 0x3FF) * 50;
                uint32_t current_ma = (pdo & 0x3FF) * 10;
                uint32_t power_w = (voltage_mv * current_ma) / 1000000;
                
                hardware.PrintLine("PDO %d: %dmV @ %dmA (%dW) [FIXED]", 
                                 i+1, voltage_mv, current_ma, power_w);
                
                // Decode additional flags for first PDO
                if (i == 0) {
                    if (pdo & (1<<29)) hardware.PrintLine("  - Dual Role Power");
                    if (pdo & (1<<28)) hardware.PrintLine("  - USB Suspend Supported");
                    if (pdo & (1<<27)) hardware.PrintLine("  - Externally Powered");
                    if (pdo & (1<<26)) hardware.PrintLine("  - USB Communications Capable");
                    if (pdo & (1<<25)) hardware.PrintLine("  - Data Role Swap");
                }
            } else if (type == PDO_TYPE_VARIABLE) {
                uint32_t max_voltage_mv = ((pdo >> 20) & 0x3FF) * 50;
                uint32_t min_voltage_mv = ((pdo >> 10) & 0x3FF) * 50;
                uint32_t current_ma = (pdo & 0x3FF) * 10;
                
                hardware.PrintLine("PDO %d: %d-%dmV @ %dmA [VARIABLE]", 
                                 i+1, min_voltage_mv, max_voltage_mv, current_ma);
            } else if (type == PDO_TYPE_BATTERY) {
                uint32_t max_voltage_mv = ((pdo >> 20) & 0x3FF) * 50;
                uint32_t min_voltage_mv = ((pdo >> 10) & 0x3FF) * 50;
                uint32_t power_mw = (pdo & 0x3FF) * 250;
                
                hardware.PrintLine("PDO %d: %d-%dmV @ %dmW [BATTERY]", 
                                 i+1, min_voltage_mv, max_voltage_mv, power_mw);
            } else {
                hardware.PrintLine("PDO %d: 0x%08X [AUGMENTED/UNKNOWN]", i+1, pdo);
            }
        }
        
        hardware.PrintLine("===================================\n");
    }
    
    void CheckPDMessages() {
        // Read ALL status/interrupt registers for debugging
        uint8_t status0 = FUSB302_Read(REG_STATUS0);
        uint8_t status1 = FUSB302_Read(REG_STATUS1);
        uint8_t interrupt = FUSB302_Read(REG_INTERRUPT);
        uint8_t interrupta = FUSB302_Read(REG_INTERRUPTA);
        uint8_t interruptb = FUSB302_Read(REG_INTERRUPTB);
        
        hardware.PrintLine("[Check] ST0=0x%02X ST1=0x%02X INT=0x%02X INTA=0x%02X INTB=0x%02X",
                          status0, status1, interrupt, interrupta, interruptb);
        
        // Try reading 10 bytes from FIFO regardless of RX_EMPTY flag
        hardware.Print("  Raw FIFO: ");
        for (int i = 0; i < 10; i++) {
            uint8_t b = FUSB302_Read(REG_FIFOS);
            hardware.Print("%02X ", b);
        }
        hardware.PrintLine("");
        
        uint8_t header[2];
        uint32_t data[7];
        int dataLen = 0;
        
        if (ReadPDMessage(header, data, &dataLen)) {
            uint8_t msgType = header[0] & 0x0F;
            uint8_t numDataObjs = (header[0] >> 4) & 0x07;
            
            hardware.PrintLine("\n!!! PD Message Received !!!");
            hardware.PrintLine("Header: 0x%02X%02X", header[1], header[0]);
            hardware.PrintLine("Message Type: %d, Data Objects: %d", msgType, numDataObjs);
            
            // Check if this is Source Capabilities
            if (msgType == PD_DATA_SOURCE_CAP && dataLen > 0) {
                // Store capabilities
                srcCapCount = dataLen;
                for (int i = 0; i < dataLen; i++) {
                    srcCaps[i] = data[i];
                }
                capsReceived = true;
                
                // Parse and display
                ParseSourceCaps(data, dataLen);
            } else {
                // Display raw data
                for (int i = 0; i < dataLen; i++) {
                    hardware.PrintLine("Data[%d]: 0x%08X", i, data[i]);
                }
            }
        }
    }

    void init() override {
        // Enable serial logging
        hardware.StartLog(true);
        System::Delay(200); // Wait for serial to initialize
        hardware.PrintLine("=== FUSB302 USB-C PHY Test ===");
        hardware.PrintLine("Initializing I2C4...");
        
        InitI2C4();
        System::Delay(100);
        
        hardware.PrintLine("Initializing INT pin (D27)...");
        InitInterruptPin();
        
        hardware.PrintLine("Initializing VBUS pin (D28)...");
        InitVBusPin();
        
        // Check VBUS from external source
        bool vbusExt = ReadVBus();
        hardware.PrintLine("External VBUS (D28): %s", vbusExt ? "PRESENT" : "NOT PRESENT");
        
        // Scan I2C bus first
        I2CScan();
        
        // CRITICAL: Power on the chip BEFORE reading device ID
        hardware.PrintLine("Powering on FUSB302 (register 0x0B)...");
        FUSB302_Write(0x0B, 0x0F); // POWER register - enable all blocks
        System::Delay(50); // Wait for power-up
        
        // Perform software reset
        hardware.PrintLine("Performing software reset (register 0x0C)...");
        FUSB302_Write(0x0C, 0x01); // RESET register - SW reset
        System::Delay(50); // Wait for reset to complete
        
        // Re-enable power after reset
        FUSB302_Write(0x0B, 0x0F); // POWER register - enable all blocks
        System::Delay(10);
        
        // Now read device ID
        hardware.PrintLine("Reading device ID from register 0x01...");
        uint8_t deviceId = FUSB302_Read(0x01);
        
        // Per datasheet: bits 7:4 = Version ID (0x8=A, 0x9=B), bits 3:0 = Revision ID
        uint8_t versionId = (deviceId >> 4) & 0x0F;
        uint8_t revisionId = deviceId & 0x0F;
        
        hardware.PrintLine("Device ID: 0x%02X", deviceId);
        hardware.PrintLine("  Version ID: 0x%X (bits 7:4)", versionId);
        hardware.PrintLine("  Revision ID: 0x%X (bits 3:0)", revisionId);
        
        // Check Version ID (upper nibble) - should be 0x8 (A) or 0x9 (B) or 0xA (C)
        if (versionId >= 0x8 && versionId <= 0xA) {
            char versionLetter = 'A' + (versionId - 0x8);
            char revisionLetter = 'A' + revisionId;
            hardware.PrintLine("FUSB302%c detected! (Revision %c)", versionLetter, revisionLetter);
            chipDetected = true;
            
            // Phase 2: CC Detection & PD Protocol
            hardware.PrintLine("\n=== Phase 2: CC Detection & BMC Setup ===");
            
            // Step 1: Detect active CC line
            hardware.PrintLine("\nStep 1: Detecting CC line orientation...");
            activeCC = DetectCC();
            
            if (activeCC == 0) {
                hardware.PrintLine("WARNING: No CC connection detected!");
                hardware.PrintLine("Please connect a USB-C power adapter");
            } else {
                hardware.PrintLine("SUCCESS: CC%d is active!", activeCC);
                ccDetected = true;
                
                // Step 2: Configure BMC receiver
                hardware.PrintLine("\nStep 2: Configuring BMC receiver...");
                ConfigureBMCReceiver(activeCC);
                
                // Step 3: Enable power blocks
                hardware.PrintLine("\nStep 3: Enabling power blocks...");
                FUSB302_Write(REG_POWER, 0x0F); // Enable all blocks
                
                // Step 3.5: Wait for VBUS to be detected
                hardware.PrintLine("\nStep 3.5: Waiting for VBUS detection...");
                bool vbusOk = false;
                for (int i = 0; i < 20; i++) {
                    // Check external VBUS pin
                    bool vbusExt = ReadVBus();
                    
                    // Check FUSB302 internal VBUS detection
                    uint8_t status0 = FUSB302_Read(REG_STATUS0);
                    bool vbusChip = (status0 & STATUS0_VBUSOK) != 0;
                    
                    hardware.PrintLine("  VBUS check %d/20: D28=%s FUSB=%s STATUS0=0x%02X", 
                                      i+1, vbusExt ? "YES" : "NO", vbusChip ? "YES" : "NO", status0);
                    
                    if (vbusChip) {
                        hardware.PrintLine("VBUS detected by FUSB302!");
                        vbusOk = true;
                        break;
                    }
                    
                    if (vbusExt && !vbusChip) {
                        hardware.PrintLine("WARNING: VBUS present on D28 but FUSB302 doesn't see it!");
                        hardware.PrintLine("This may indicate VBUS pin not connected to FUSB302, or threshold issue.");
                    }
                    
                    System::Delay(100);
                }
                
                if (!vbusOk) {
                    hardware.PrintLine("WARNING: VBUS not detected by FUSB302!");
                    hardware.PrintLine("PD messaging requires VBUS present.");
                    bool vbusExt = ReadVBus();
                    if (vbusExt) {
                        hardware.PrintLine("But VBUS IS present on D28 - check FUSB302 VBUS pin connection!");
                    } else {
                        hardware.PrintLine("VBUS also not present on D28 - MacBook may not be sourcing power.");
                    }
                }
                
                // Step 4: Wait for automatic Source_Capabilities from source
                // Per USB-PD spec, source should send caps automatically when sink is detected
                hardware.PrintLine("\nStep 4: Waiting for automatic Source_Capabilities from source...");
                hardware.PrintLine("(Source should detect our Rd pull-downs and send caps within ~500ms)");
                
                // Wait longer for source to detect us and send caps
                for (int i = 0; i < 10; i++) {
                    System::Delay(100);
                    CheckPDMessages();
                    if (capsReceived) {
                        hardware.PrintLine("Received Source Capabilities!");
                        break;
                    }
                }
                
                // Step 5: If no caps received, try requesting them
                if (!capsReceived) {
                    hardware.PrintLine("\nStep 5: No automatic caps received, requesting Source_Capabilities...");
                    SendPDControl(PD_CTRL_GETSRCCAP);
                    System::Delay(100);
                    CheckPDMessages();
                }
                
                hardware.PrintLine("\n=== Initialization Complete ===");
                hardware.PrintLine("System ready to receive PD messages");
                if (!capsReceived) {
                    hardware.PrintLine("Waiting for Source Capabilities...\n");
                }
                
                // Print initial register values
                PrintRegisters();
            }
        } else {
            hardware.PrintLine("ERROR: Unknown FUSB302 version!");
            hardware.PrintLine("  Expected Version ID 0x8-0xA, got 0x%X", versionId);
            hardware.PrintLine("  This could mean:");
            hardware.PrintLine("  - Wrong chip at address 0x22");
            hardware.PrintLine("  - Chip not powered correctly");
            hardware.PrintLine("  - Hardware connection issue");
        }
        lastPrintTime = System::GetNow();
    }

    void FUSB302_Write(uint8_t reg, uint8_t data) {
        uint8_t buf[2];
        buf[0] = reg;
        buf[1] = data;
        I2CHandle::Result result = i2c4.TransmitBlocking(0x22, buf, 2, 1000);
        if(result != I2CHandle::Result::OK) {
            hardware.PrintLine("I2C Write Error: %d", (int)result);
        }
    }

    uint8_t FUSB302_Read(uint8_t reg) {
        uint8_t data = 0;
        I2CHandle::Result result = i2c4.ReadDataAtAddress(0x22, reg, 1, &data, 1, 1000);
        if(result != I2CHandle::Result::OK) {
            hardware.PrintLine("I2C Read Error: %d", (int)result);
        }
        return data;
    }
    
    void I2CScan() {
        hardware.PrintLine("\nScanning I2C bus...");
        int found = 0;
        for(uint8_t addr = 0x08; addr < 0x78; addr++) {
            uint8_t dummy = 0;
            I2CHandle::Result result = i2c4.ReceiveBlocking(addr, &dummy, 1, 10);
            if(result == I2CHandle::Result::OK) {
                hardware.PrintLine("Found device at address 0x%02X", addr);
                found++;
            }
        }
        if(found == 0) {
            hardware.PrintLine("No I2C devices found!");
        } else {
            hardware.PrintLine("Found %d device(s)\n", found);
        }
    }

    uint8_t fifo[64];

    void ReadFIFO(int len) {
        uint8_t reg = 0x43; // FIFOS register
        i2c4.TransmitBlocking(0x22, &reg, 1, 100);
        i2c4.ReceiveBlocking(0x22, fifo, len, 100);
    }
    
    void PrintRegisters() {
        hardware.PrintLine("\n--- FUSB302 Register Dump ---");
        hardware.PrintLine("DEVICE_ID (0x01): 0x%02X", FUSB302_Read(0x01));
        hardware.PrintLine("SWITCHES0 (0x02): 0x%02X", FUSB302_Read(0x02));
        hardware.PrintLine("SWITCHES1 (0x03): 0x%02X", FUSB302_Read(0x03));
        hardware.PrintLine("MEASURE   (0x04): 0x%02X", FUSB302_Read(0x04));
        hardware.PrintLine("SLICE     (0x05): 0x%02X", FUSB302_Read(0x05));
        hardware.PrintLine("CONTROL0  (0x06): 0x%02X", FUSB302_Read(0x06));
        hardware.PrintLine("CONTROL1  (0x07): 0x%02X", FUSB302_Read(0x07));
        hardware.PrintLine("CONTROL2  (0x08): 0x%02X", FUSB302_Read(0x08));
        hardware.PrintLine("CONTROL3  (0x09): 0x%02X", FUSB302_Read(0x09));
        hardware.PrintLine("MASK      (0x0A): 0x%02X", FUSB302_Read(0x0A));
        hardware.PrintLine("POWER     (0x0B): 0x%02X", FUSB302_Read(0x0B));
        hardware.PrintLine("RESET     (0x0C): 0x%02X", FUSB302_Read(0x0C));
        hardware.PrintLine("STATUS0A  (0x3C): 0x%02X", FUSB302_Read(0x3C));
        hardware.PrintLine("STATUS1A  (0x3D): 0x%02X", FUSB302_Read(0x3D));
        hardware.PrintLine("INTERRUPTA(0x3E): 0x%02X", FUSB302_Read(0x3E));
        hardware.PrintLine("INTERRUPTB(0x3F): 0x%02X", FUSB302_Read(0x3F));
        hardware.PrintLine("STATUS0   (0x40): 0x%02X", FUSB302_Read(0x40));
        hardware.PrintLine("STATUS1   (0x41): 0x%02X", FUSB302_Read(0x41));
        hardware.PrintLine("INTERRUPT (0x42): 0x%02X", FUSB302_Read(0x42));
        hardware.PrintLine("-----------------------------\n");
    }
    
    void CheckInterrupts() {
        // Check INT pin state
        bool intAsserted = !intPin.Read(); // Active low
        
        if (intAsserted) {
            hardware.PrintLine("\n!!! Interrupt asserted !!!");
            
            // Read interrupt registers
            uint8_t interruptA = FUSB302_Read(0x3E);
            uint8_t interruptB = FUSB302_Read(0x3F);
            uint8_t interrupt = FUSB302_Read(0x42);
            
            hardware.PrintLine("INTERRUPTA: 0x%02X", interruptA);
            hardware.PrintLine("INTERRUPTB: 0x%02X", interruptB);
            hardware.PrintLine("INTERRUPT:  0x%02X", interrupt);
            
            // Read status registers
            uint8_t status0A = FUSB302_Read(0x3C);
            uint8_t status1A = FUSB302_Read(0x3D);
            uint8_t status0 = FUSB302_Read(0x40);
            uint8_t status1 = FUSB302_Read(0x41);
            
            hardware.PrintLine("STATUS0A: 0x%02X", status0A);
            hardware.PrintLine("STATUS1A: 0x%02X", status1A);
            hardware.PrintLine("STATUS0:  0x%02X", status0);
            hardware.PrintLine("STATUS1:  0x%02X", status1);
            
            // Decode CC status
            uint8_t bc_lvl = (status0 >> 0) & 0x03;
            uint8_t comp = (status0 >> 5) & 0x01;
            hardware.PrintLine("BC_LVL: %d, COMP: %d", bc_lvl, comp);
        }
    }
    
    void loop() override {
        if (!chipDetected) {
            System::Delay(1000);
            return;
        }
        
        // If CC not detected, keep trying
        if (!ccDetected) {
            uint32_t now = System::GetNow();
            if (now - lastPrintTime > 2000) {
                hardware.PrintLine("Retrying CC detection...");
                activeCC = DetectCC();
                if (activeCC > 0) {
                    hardware.PrintLine("CC%d detected! Configuring BMC...", activeCC);
                    ConfigureBMCReceiver(activeCC);
                    ccDetected = true;
                }
                lastPrintTime = now;
            }
            System::Delay(100);
            return;
        }
        
        // Check for PD messages frequently
        uint32_t now = System::GetNow();
        if (now - lastPDCheck > 50) { // Check every 50ms
            CheckPDMessages();
            lastPDCheck = now;
        }
        
        // Check for interrupts
        bool intAsserted = !intPin.Read(); // Active low
        if (intAsserted) {
            hardware.PrintLine("\n[INT asserted]");
            
            uint8_t interruptA = FUSB302_Read(REG_INTERRUPTA);
            uint8_t interruptB = FUSB302_Read(REG_INTERRUPTB);
            uint8_t interrupt = FUSB302_Read(REG_INTERRUPT);
            
            if (interruptA) hardware.PrintLine("INTERRUPTA: 0x%02X", interruptA);
            if (interruptB) hardware.PrintLine("INTERRUPTB: 0x%02X", interruptB);
            if (interrupt) hardware.PrintLine("INTERRUPT:  0x%02X", interrupt);
            
            // Check for RX data
            CheckPDMessages();
        }
        
        // Periodic status update
        if (now - lastPrintTime > 5000) { // Every 5 seconds
            if (!capsReceived) {
                hardware.PrintLine("\n[Still waiting for Source Capabilities...]");
            } else {
                hardware.PrintLine("\n[Monitoring PD messages - %d PDOs cached]", srcCapCount);
            }
            lastPrintTime = now;
        }
        
        System::Delay(10); // Small delay
    }
  
};

int main() {
    FUSB302 mFUSB302;
    mFUSB302.start();
    return 0;
}


