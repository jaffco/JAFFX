#include "../../Jaffx.hpp"
#include "fatfs.h"
#include "diskio.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "fsm.h"
#include "state.h"


// Global SD resources (hardware-required placement in AXI SRAM for DMA)
SdmmcHandler global_sdmmc_handler __attribute__((section(".sram1_bss")));
FatFSInterface global_fsi_handler __attribute__((section(".sram1_bss")));
FIL global_wav_file __attribute__((section(".sram1_bss")));

template<size_t WriteBufferSize = 4096>
class SDCardWavWriter {
private:
  // References to global SD handlers
  SdmmcHandler& sdmmc_handler = global_sdmmc_handler;
  FatFSInterface& fsi_handler = global_fsi_handler;
  FIL& wav_file = global_wav_file;

  // Recording state
  bool sdCardOk = false;
  bool isRecording = false;
  unsigned int recordedSamples = 0;
  unsigned int recordStartTime = 0;

  // Audio buffer (template parameter allows flexible sizing)
  int16_t audioBuffer[WriteBufferSize];
  size_t bufferIndex = 0;

  // WAV file header structure
  struct WavHeader {
    // RIFF header
    char riff[4];
    unsigned int fileSize;
    char wave[4];
    
    // fmt subchunk
    char fmt[4];
    unsigned int fmtSize;
    unsigned short audioFormat;
    unsigned short numChannels;
    unsigned int sampleRate;
    unsigned int byteRate;
    unsigned short blockAlign;
    unsigned short bitsPerSample;
    
    // data subchunk
    char data[4];
    unsigned int dataSize;
  } __attribute__((packed));

public:

  const bool sdStatus() { return this->sdCardOk; }
  const bool recording() { return this->isRecording; }

  void InitSDCard() {
    
    // Initialize SDMMC hardware
    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sd_cfg.speed = SdmmcHandler::Speed::STANDARD;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_1; // 1-bit mode for compatibility
    sd_cfg.clock_powersave = false;
    
    if(sdmmc_handler.Init(sd_cfg) != SdmmcHandler::Result::OK) {
      return;
    }
    
    System::Delay(100);
    
    // Initialize FatFS interface
    FatFSInterface::Config fsi_cfg;
    fsi_cfg.media = FatFSInterface::Config::MEDIA_SD;
    
    if(fsi_handler.Init(fsi_cfg) != FatFSInterface::OK) {
      return;
    }
    
    System::Delay(500);
    
    // Mount filesystem
    FATFS& fs = fsi_handler.GetSDFileSystem();
    FRESULT res = f_mount(&fs, fsi_handler.GetSDPath(), 1);
    
    if(res != FR_OK) {
      // Try disk initialize
      if(disk_initialize(0) == 0) {
        System::Delay(200);
        res = f_mount(&fs, fsi_handler.GetSDPath(), 1);
      }
    }
    
    if(res != FR_OK) {
      return;
    }
    
    // Check disk status
    if(disk_status(0) != 0) {
      if(disk_initialize(0) != 0) {
        return;
      }
    }
    
    System::Delay(100);
    sdCardOk = true;
  }  

  void StartRecording() {
    if(!sdCardOk || isRecording) {
      return;
    }
    
    // Get next recording number from counter file
    unsigned int recordNum = GetNextRecordingNumber();
    
    // Generate filename with sequential number
    // Format: rec_NNNNN.wav where NNNNN is a sequential number
    char filename[32];
    snprintf(filename, sizeof(filename), "rec_%05u.wav", recordNum);
    
    // Open file for writing
    FRESULT res = f_open(&wav_file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if(res != FR_OK) {
      sdCardOk = false;
      return;
    }
    
    // Write WAV header (will be updated periodically)
    WavHeader header;
    // Initialize header fields
    header.riff[0] = 'R'; header.riff[1] = 'I'; header.riff[2] = 'F'; header.riff[3] = 'F';
    header.fileSize = 36; // Will be updated
    header.wave[0] = 'W'; header.wave[1] = 'A'; header.wave[2] = 'V'; header.wave[3] = 'E';
    header.fmt[0] = 'f'; header.fmt[1] = 'm'; header.fmt[2] = 't'; header.fmt[3] = ' ';
    header.fmtSize = 16;
    header.audioFormat = 1; // PCM
    header.numChannels = 1; // Mono
    header.sampleRate = 48000;
    header.byteRate = 48000 * 2; // SampleRate * NumChannels * BitsPerSample/8
    header.blockAlign = 2; // NumChannels * BitsPerSample/8
    header.bitsPerSample = 16;
    header.data[0] = 'd'; header.data[1] = 'a'; header.data[2] = 't'; header.data[3] = 'a';
    header.dataSize = 0; // Will be updated
    
    UINT bytes_written;
    res = f_write(&wav_file, &header, sizeof(header), &bytes_written);
    
    if(res != FR_OK || bytes_written != sizeof(header)) {
      f_close(&wav_file);
      sdCardOk = false;
      return;
    }
    
    // Initialize recording state
    recordedSamples = 0;
    bufferIndex = 0;
    recordStartTime = System::GetNow();
    isRecording = true;
    
    // Sync to ensure header is written
    f_sync(&wav_file);
  }

  unsigned int GetNextRecordingNumber() {
    // Try to read the counter file
    FIL counter_file;
    unsigned int recordNum = 0;
    
    FRESULT res = f_open(&counter_file, ".rec_counter", FA_READ);
    if(res == FR_OK) {
      // File exists, read the counter
      UINT bytes_read;
      char buffer[16];
      res = f_read(&counter_file, buffer, sizeof(buffer) - 1, &bytes_read);
      if(res == FR_OK && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        recordNum = atoi(buffer);
      }
      f_close(&counter_file);
    }
    
    // Increment for next recording
    recordNum++;
    
    // Write back the incremented counter
    res = f_open(&counter_file, ".rec_counter", FA_CREATE_ALWAYS | FA_WRITE);
    if(res == FR_OK) {
      char buffer[16];
      int len = snprintf(buffer, sizeof(buffer), "%u", recordNum);
      UINT bytes_written;
      f_write(&counter_file, buffer, len, &bytes_written);
      f_close(&counter_file);
    }
    
    return recordNum;
  }

  void WriteAudioSample(float sample) {
    if(!isRecording) {
      return;
    }
    
    // Convert float (-1.0 to 1.0) to 16-bit PCM
    int16_t pcm_sample = static_cast<int16_t>(sample * 32767.0f);
    
    // Add to buffer
    audioBuffer[bufferIndex++] = pcm_sample;
    recordedSamples++;
    
    // Write buffer to SD card when full
    if(bufferIndex >= WriteBufferSize) {
      FlushAudioBuffer();
    }
    
    // Periodically sync and update header to prevent data loss (every ~5 seconds at 48kHz)
    if(recordedSamples % (48000 * 5) == 0) {
      UpdateWavHeader();  // Update header with current size
      f_sync(&wav_file);
    }
  }

  void FlushAudioBuffer() {
    if(!isRecording || bufferIndex == 0) {
      return;
    }
    
    UINT bytes_written;
    size_t bytes_to_write = bufferIndex * sizeof(int16_t);
    
    FRESULT res = f_write(&wav_file, audioBuffer, bytes_to_write, &bytes_written);
    
    if(res != FR_OK || bytes_written != bytes_to_write) {
      // Error writing - stop recording to prevent corruption
      StopRecording();
      sdCardOk = false;
      return;
    }
    
    bufferIndex = 0;
  }

  void StopRecording() {
    if(!isRecording) {
      return;
    }
    
    // Flush any remaining samples
    FlushAudioBuffer();
    
    // Update WAV header with actual file size
    UpdateWavHeader();
    
    // Close file
    f_close(&wav_file);
    
    isRecording = false;
  }
  
  void UpdateWavHeader() {
    // Save current file position
    FSIZE_t current_pos = f_tell(&wav_file);
    
    // Calculate actual data size
    unsigned int dataSize = recordedSamples * sizeof(short);
    unsigned int fileSize = dataSize + sizeof(WavHeader) - 8; // Exclude RIFF header (8 bytes)
    
    // Seek to file size field (offset 4)
    f_lseek(&wav_file, 4);
    UINT bytes_written;
    f_write(&wav_file, &fileSize, sizeof(fileSize), &bytes_written);
    
    // Seek to data size field (offset 40)
    f_lseek(&wav_file, 40);
    f_write(&wav_file, &dataSize, sizeof(dataSize), &bytes_written);
    
    // Restore file position to continue writing audio data
    f_lseek(&wav_file, current_pos);
    
    // Sync to ensure header is updated
    f_sync(&wav_file);
  }

};

class SlipRecorder : public Jaffx::Firmware {
  GPIO mLeds[3];
  float RmsReport = 0.f;
  SDCardWavWriter<> mWavWriter;  // Use default template parameter (4096 bytes)
  bool usb_connected = false;
  FSM mFSM;

  void init() override {
    // Initialize LEDs
    mLeds[0].Init(seed::D14, GPIO::Mode::OUTPUT);
    mLeds[1].Init(seed::D12, GPIO::Mode::OUTPUT);
    mLeds[2].Init(seed::D9, GPIO::Mode::OUTPUT);
    
    // Initialize SD card
    mWavWriter.InitSDCard();

    // choose initial state based on SD status
    if (mWavWriter.sdStatus()) {
      mFSM.setState(SD_Check::getInstance());
    } else {
      mFSM.setState(Sleep::getInstance());
    }
    
    // Start recording if SD card is OK
    if(mWavWriter.sdStatus()) {
      mWavWriter.StartRecording();
    }
  }

  float processAudio(float in) override {
    // Write sample to SD card if recording
    if(mWavWriter.recording()) {
      mWavWriter.WriteAudioSample(in);
    }
    
    // Calculate RMS for LED display
    static int counter = 0;
    static float RMS = 0.f;
    counter++;
    RMS += in * in;
    if (counter >= buffersize){
      RmsReport = sqrt(RMS); // update report
      counter = 0; // reset counter
      RMS = 0.f; // reset RMS
    }
    
    return in; // output throughput 
  }

  void indicateLEDs() {
    // Convert RMS to dB - Add small offset to avoid log(0)   
    float dB = 20.0f * log10f(RmsReport + 1e-12f); 
    
    // LED logic based on dB levels (same as dBMeter example)
    if (dB >= 0.0f) {
      // >= 0dB: all three LEDs on
      mLeds[0].Write(true);
      mLeds[1].Write(true);
      mLeds[2].Write(true);
    } else if (dB >= -6.0f) {
      // >= -6dB: LEDs 1 and 2 on, LED 0 off
      mLeds[0].Write(false);
      mLeds[1].Write(true);
      mLeds[2].Write(true);
    } else if (dB >= -20.0f) {
      // >= -20dB: LED 2 on, LEDs 0 and 1 off
      mLeds[0].Write(false);
      mLeds[1].Write(false);
      mLeds[2].Write(true);
    } else {
      // < -20dB: all LEDs off
      mLeds[0].Write(false);
      mLeds[1].Write(false);
      mLeds[2].Write(false);
    }
  }

  void loop() override {
    
    if(usb_connected) {
      indicateLEDs();
    }

    


    hardware.SetLed(mWavWriter.recording());
    
    System::Delay(1);
  }
};

int main() {
  SlipRecorder mSlipRecorder;
  mSlipRecorder.start();
  return 0;
}


