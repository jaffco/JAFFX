#include <stdio.h>
#include <string.h>
#include "../../Jaffx.hpp"
#include "../../Gimmel/include/oscillator.hpp"
#include "fatfs.h"

#define TEST_FILE_NAME "SDSine.wav"

#define SAMPLES_TO_WRITE 48000 * 5 // 5 second buffer at 48kHz 

float** globalPtr = nullptr;
size_t blocksWritten = 0;

class SDWriter {
private:
    // Internal SD card objects
    SdmmcHandler*   pSD = nullptr;
    FatFSInterface* pFSI = nullptr;
    FIL*            pSDFile = nullptr;


    // size_t len, failcnt, byteswritten;

    struct WavHeader {
        char riff[4];
        unsigned int fileSize;
        char wave[4];
        char fmt[4];
        unsigned int fmtSize;
        unsigned short audioFormat;
        unsigned short numChannels;
        unsigned int sampleRate;
        unsigned int byteRate;
        unsigned short blockAlign;
        unsigned short bitsPerSample;
        char data[4];
        unsigned int dataSize;
    } __attribute__((packed));

public:
    bool InitSDCard() {

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

        // Open and write the test WAV header to the SD Card.
        if(f_open(pSDFile, TEST_FILE_NAME, (FA_CREATE_ALWAYS) | (FA_WRITE)) == FR_OK) {
            WavHeader header;
            memcpy(header.riff, "RIFF", 4);
            header.fileSize = 36;
            memcpy(header.wave, "WAVE", 4);
            memcpy(header.fmt, "fmt ", 4);
            header.fmtSize = 16;
            header.audioFormat = 3; // Float
            header.numChannels = 1;
            header.sampleRate = Jaffx::Firmware::instance->samplerate;
            header.bitsPerSample = 32;
            header.blockAlign = header.numChannels * (header.bitsPerSample / 8);
            header.byteRate = header.sampleRate * header.blockAlign;
            memcpy(header.data, "data", 4);
            header.dataSize = 0;

            UINT bw;
            f_write(pSDFile, &header, sizeof(header), &bw);
            f_sync(pSDFile);
        }

        return true; // we're recording!
    }

    bool writeAudioBuffer() {

        for (size_t i = 0; i < SAMPLES_TO_WRITE / Jaffx::Firmware::instance->buffersize; i++) {
            UINT bytes_written;
            size_t bytes_to_write = sizeof(float) * Jaffx::Firmware::instance->buffersize;


            // Open and write the test file to the SD Card.        
            FRESULT res = f_write(pSDFile, globalPtr[i], bytes_to_write, &bytes_written);

            if (bytes_written < bytes_to_write) {
                // Error writing - stop recording to prevent corruption
                return false;
            }
            
            if(res != FR_OK || bytes_written != bytes_to_write) {
                // Error writing - stop recording to prevent corruption
                return false ;
            }
        }
        FRESULT res = UpdateWavHeader();
        return true;
    }

    FRESULT UpdateWavHeader() {

        FSIZE_t current_pos = f_tell(pSDFile);
        unsigned int dataSize = SAMPLES_TO_WRITE * sizeof(float);
        unsigned int fileSize = dataSize + sizeof(WavHeader) - 8;

        FRESULT res;
        UINT bw;

        res = f_lseek(pSDFile, 4);
        if (res != FR_OK) {
            return res;
        }

        res = f_write(pSDFile, &fileSize, 4, &bw);
        if (res != FR_OK || bw != 4) {
            return res;
        }

        res = f_lseek(pSDFile, 40);
        if (res != FR_OK) {
            return res;
        }

        res = f_write(pSDFile, &dataSize, 4, &bw);
        if (res != FR_OK || bw != 4) {
            return res;
        }

        res = f_lseek(pSDFile, current_pos);
        if (res != FR_OK) {
            return res;
        }

        res = f_sync(pSDFile);
        if (res != FR_OK) {
            return res;
        }

        return FR_OK;
    }


};

class SDSineWav : public Jaffx::Firmware {
private:
    SDWriter sdWriter;
    giml::SinOsc<float> mOsc{samplerate};
    bool timeToWrite = false;


public:
    void init() override {
        hardware.SetLed(sdWriter.InitSDCard());
        mOsc.setFrequency(220.0f);
        giml::free(globalPtr);

        // Allocate the array of pointers FIRST
        globalPtr = (float**)giml::malloc(sizeof(float*) * (SAMPLES_TO_WRITE / buffersize));
        for (size_t i = 0; i < SAMPLES_TO_WRITE / buffersize; i++) {
            globalPtr[i] = nullptr;
        }

        System::Delay(100);
    }

    void CustomAudioBlockCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) override {
        for (size_t i = 0; i < size; i++) {
            out[0][i] = mOsc.processSample();
            out[1][i] = out[0][i];
        }

        if (!timeToWrite) {

            // get a block and copy data into it
            globalPtr[blocksWritten] = (float*)giml::malloc(sizeof(float) * size); 
            memcpy(globalPtr[blocksWritten], out[0], sizeof(float) * size);
            blocksWritten++;
            
            // check if we're done
            if (blocksWritten * size >= SAMPLES_TO_WRITE) {
                timeToWrite = true;
            }
        }
        return;
    }

    void loop() override {
        if (globalPtr != nullptr && timeToWrite) {
            hardware.SetLed(!sdWriter.writeAudioBuffer());
            while (true) {} // halt after one write
        }
        System::Delay(1);
    }
};

int main(void) {
    SDSineWav mSDSineWav;
    mSDSineWav.start();
}