#include "../../Jaffx.hpp"

/**
 * Step 1: Get I2C4 working w/ DMA on Daisy Seed
 * Step 2: Read raw register values and make sure they match defaults on FUSB302
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
    void InitI2C4() {
        // using namespace daisy;
        // using namespace daisy::i2c;

        I2CHandle::Config i2c_config;
        i2c_config.periph = I2CHandle::Config::Peripheral::I2C_4;
        i2c_config.speed  = I2CHandle::Config::Speed::I2C_400KHZ;
        i2c_config.mode   = I2CHandle::Config::Mode::I2C_MASTER;
        // TODO: Verify these pins
        i2c_config.pin_config.scl = DaisySeed::GetPin(28); // Pin 29 on silkscreen
        i2c_config.pin_config.sda = DaisySeed::GetPin(27); // Pin 28 on silkscreen

        i2c4.Init(i2c_config);
    }

    void init() override {
        InitI2C4();
        // Power on everything
        FUSB_Write(0x0F, 0x0F);

        // Enable BMC receiver
        FUSB_Write(0x08, 0x08);

        // Enable interrupts on FUSB302
        FUSB_Write(0x0E, 0x00); // unmask all
    }

    void FUSB302_Write(uint8_t reg, uint8_t data) {
        uint8_t buf[2];
        buf[0] = reg;
        buf[1] = data;
        i2c4.TransmitBlocking(0x22, buf, 2, 1000);
    }

    uint8_t FUSB302_Read(uint8_t reg) {
        uint8_t data;
        i2c4.TransmitBlocking(0x22, &reg, 1, 1000);
        i2c4.ReceiveBlocking(0x22, &data, 1, 1000);
        return data;
    }

    uint8_t fifo[64];

    void ReadFIFO(int len) {
        uint8_t reg = 0x40;
        i2c4.TransmitBlocking(0x22, &reg, 1, 100);
        i2c4.ReceiveBlocking(0x22, fifo, len, 100);
    }
    
    void loop() override {

    }
  
};

int main() {
    FUSB302 mFUSB302;
    mFUSB302.start();
    return 0;
}


