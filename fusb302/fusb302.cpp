#include "../../Jaffx.hpp"

/**
 * Step 1: Get I2C working
 * Step 2: Establish communication with FUSB302 
 * Step 3: Start importing https://github.com/graycatlabs/usb-c-arduino:
 *     - will need to rewrite all the I2C communication
 *     - strip all other non-necessary logic (keep source+sink code in case we want to charge toothbrush with our guitar pedal)
 *     - use https://github.com/ReclaimerLabs/USB-C-Explorer/blob/1e312f194c6b402f8bfcd729c8bc48df8440bd78/firmware/USB-C%20Explorer/USB-C%20Explorer/main.c#L260
 *       in main loop to read + list all the possible power profiles
 * Step 4: Use the function at https://github.com/ReclaimerLabs/USB-C-Explorer/blob/1e312f194c6b402f8bfcd729c8bc48df8440bd78/firmware/USB-C%20Explorer/USB-C%20Explorer/main.c#L420
 * to detect BC1.2
 * 
 */

class FUSB302 : public Jaffx::Firmware {
    I2CHandle i2c4;
    GPIO intPin;
    bool chipDetected = false;
    uint32_t lastPrintTime = 0;
    
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
            
            // Configure chip
            hardware.PrintLine("Configuring FUSB302...");
            
            // Enable BMC receiver
            FUSB302_Write(0x08, 0x08); // CONTROL2 - enable BMC
            
            // Unmask interrupts
            FUSB302_Write(0x0A, 0x00); // MASK - unmask all
            FUSB302_Write(0x0E, 0x00); // MASKA - unmask all
            FUSB302_Write(0x0F, 0x00); // MASKB - unmask all
            
            hardware.PrintLine("Initialization complete!");
            
            // Print initial register values
            PrintRegisters();
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
        
        // Check for interrupts
        CheckInterrupts();
        
        // Print status periodically
        uint32_t now = System::GetNow();
        if (now - lastPrintTime > 2000) { // Every 2 seconds
            hardware.PrintLine("\n[Status Update - INT pin: %s]", 
                             intPin.Read() ? "HIGH" : "LOW");
            lastPrintTime = now;
        }
        
        System::Delay(100); // Small delay to prevent overwhelming serial
    }
  
};

int main() {
    FUSB302 mFUSB302;
    mFUSB302.start();
    return 0;
}


