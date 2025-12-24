#include <stdio.h>
#include <string.h>
#include "../../Jaffx.hpp"
#include "fatfs.h"

#define TEST_FILE_NAME "test3.txt"

using namespace daisy;

// SD card objects placed in SDRAM for testing
// SdmmcHandler*   pSD;
// FatFSInterface* pFSI;
// FIL*            pSDFile;

class SDTest : public Jaffx::Firmware {
private:
    // Vars and buffs.
    char   outbuff[512];
    char   inbuff[512];
    size_t len, failcnt, byteswritten;
    bool testCompleted = false;
    bool ledstate = true;
    SdmmcHandler*   pSD;
    FatFSInterface* pFSI;
    FIL*            pSDFile;    

public:
    void init() override {
        // Initialize test data
        sprintf(outbuff, "Daisy...Testing...\n1...\n2...\n3...\n");
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
            f_close(pSDFile);
        }

        // Read back the test file from the SD Card.
        if(f_open(pSDFile, TEST_FILE_NAME, FA_READ) == FR_OK) {
            f_read(pSDFile, inbuff, len, &byteswritten);
            f_close(pSDFile);
        }

        // Check for sameness.
        for(size_t i = 0; i < len; i++) {
            if(inbuff[i] != outbuff[i]) {
                failcnt++;
            }
        }

        // If what was read does not match what was written, print error
        if(failcnt) {
            hardware.PrintLine("SD Test FAILED - failcnt: %d", failcnt);
            // Could set an error flag here instead of halting
        } else {
            hardware.PrintLine("SD Test PASSED");
        }

        testCompleted = true;
    }

    void loop() override {
        if (testCompleted) {
            // Blink LED to indicate test completion
            System::Delay(250);
            hardware.SetLed(ledstate);
            ledstate = !ledstate;
        }
    }
};

int main(void) {
    SDTest sdTest;
    sdTest.start();
}