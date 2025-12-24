#include <stdio.h>
#include <string.h>
#include "../../Jaffx.hpp"
#include "../../Gimmel/include/oscillator.hpp"
#include "fatfs.h"

#define TEST_FILE_NAME "SDCounter.txt"

using namespace daisy;

// SD card objects placed in SDRAM for testing
// SdmmcHandler*   pSD;
// FatFSInterface* pFSI;
// FIL*            pSDFile;

class SDWriter {
private:
    // Internal SD card objects
    SdmmcHandler*   pSD = nullptr;
    FatFSInterface* pFSI = nullptr;
    FIL*            pSDFile = nullptr;

    // Vars and buffs.
    char   outbuff[512];
    char   inbuff[512];
    size_t len, failcnt, byteswritten;

public:
    bool InitSDCard() {
        // Initialize test data
        sprintf(outbuff, "Init Text");
        memset(inbuff, 0, 512);
        len = strlen(outbuff);
        failcnt = 0;

        // malloc for ptrs 
        pSD = (SdmmcHandler*)giml::malloc(sizeof(SdmmcHandler));
        pFSI = (FatFSInterface*)giml::malloc(sizeof(FatFSInterface));
        pSDFile = (FIL*)giml::malloc(sizeof(FIL));

        pSD = new (pSD) SdmmcHandler();
        pFSI = new (pFSI) FatFSInterface();
        pSDFile = new (pSDFile) FIL();

        // Init SD Card
        SdmmcHandler::Config sd_cfg;
        sd_cfg.speed = SdmmcHandler::Speed::STANDARD;
        sd_cfg.width = SdmmcHandler::BusWidth::BITS_1;
        pSD->Init(sd_cfg);

        // Links libdaisy i/o to fatfs driver.
        pFSI->Init(FatFSInterface::Config::MEDIA_SD);

        // Mount SD Card
        f_mount(&pFSI->GetSDFileSystem(), "/", 1);

        // Open and write the test file to the SD Card.
        if(f_open(pSDFile, TEST_FILE_NAME, (FA_CREATE_ALWAYS) | (FA_WRITE)) == FR_OK) {
            f_write(pSDFile, outbuff, len, &byteswritten);
            f_sync(pSDFile);
        }

        return true;
    }

    bool incrementAndWriteCount() {
        static int count = 0;
        count++;

        sprintf(outbuff, "Count: %d\n", count);
        len = strlen(outbuff);

        UINT bytes_written;
        size_t bytes_to_write = len;

        // Open and write the test file to the SD Card.        
        FRESULT res = f_write(pSDFile, outbuff, len, &bytes_written);
        f_sync(pSDFile);
        
        if(res != FR_OK || bytes_written != len) {
            // Error writing - stop recording to prevent corruption
            return false ;
        }

        return true;
    }

};

class SDCounter : public Jaffx::Firmware {
private:
    SDWriter sdWriter;
    giml::SinOsc<float> mOsc{samplerate};

public:
    void init() override {
        hardware.SetLed(sdWriter.InitSDCard());
        mOsc.setFrequency(220.0f);
    }

    float processAudio(float in) override {
        return mOsc.processSample(); // pass-through
    }

    void loop() override {
        hardware.SetLed(sdWriter.incrementAndWriteCount());
        System::Delay(1);   
    }
};

int main(void) {
    SDCounter mSDCounter;
    mSDCounter.start();
}