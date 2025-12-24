#include <stdio.h>
#include <string.h>
#include "../../Jaffx.hpp"
#include "../../Gimmel/include/oscillator.hpp"
#include "fatfs.h"

#define TEST_FILE_NAME "SDSine.txt"

float* globalPtr = nullptr;

class SDWriter {
private:
    // Internal SD card objects
    SdmmcHandler*   pSD = nullptr;
    FatFSInterface* pFSI = nullptr;
    FIL*            pSDFile = nullptr;

    // Vars and buffs.
    char   outbuff[8192];
    size_t len, failcnt, byteswritten;

public:
    bool InitSDCard() {
        // Initialize test data
        sprintf(outbuff, "Init Text \n");
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
        sd_cfg.speed = SdmmcHandler::Speed::FAST;
        sd_cfg.width = SdmmcHandler::BusWidth::BITS_4;
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

    bool writeAudioBuffer(float* data, size_t size) {
        size_t offset = 0;
        for (size_t i = 0; i < 64; i++) {
            sprintf(outbuff + offset, "#%u:" FLT_FMT3 "\n", (unsigned)i, FLT_VAR3(data[i]));
            offset += strlen(outbuff + offset);
        }
        len = strlen(outbuff);

        UINT bytes_written;
        size_t bytes_to_write = len;

        // Open and write the test file to the SD Card.        
        FRESULT res = f_write(pSDFile, outbuff, len, &bytes_written);

        if (bytes_written < bytes_to_write) {
            // Error writing - stop recording to prevent corruption
            return false ;
        }

        res= f_sync(pSDFile);
        
        if(res != FR_OK || bytes_written != len) {
            // Error writing - stop recording to prevent corruption
            return false ;
        }

        return true;
    }


};

class SDSine : public Jaffx::Firmware {
private:
    SDWriter sdWriter;
    giml::SinOsc<float> mOsc{samplerate};
    bool firstRun = true;


public:
    void init() override {
        hardware.SetLed(sdWriter.InitSDCard());
        mOsc.setFrequency(220.0f);
        System::Delay(100);
        hardware.SetLed(false);
    }

    void CustomAudioBlockCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) override {
        for (size_t i = 0; i < size; i++) {
            out[0][i] = mOsc.processSample();
            out[1][i] = out[0][i];
        }
        if (firstRun) {
            giml::free(globalPtr);
            globalPtr = (float*)giml::malloc(sizeof(float) * size);
            memcpy(globalPtr, out[0], sizeof(float) * size);
            firstRun = false;
        }
        return;
    }

    void loop() override {
        if (globalPtr != nullptr && !firstRun) {
            hardware.SetLed(sdWriter.writeAudioBuffer(globalPtr, buffersize));
            while (true) {} // halt after one write
        }
        System::Delay(1);
    }
};

int main(void) {
    SDSine mSDSine;
    mSDSine.start();
}