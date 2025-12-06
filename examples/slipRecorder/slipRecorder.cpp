#include "../../Jaffx.hpp"
// #include "StateMachine.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "stm32h750xx.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr_ex.h"



// State machine stuffs
enum class SlipRecorderState : unsigned char { // inherit from unsigned char to save space
    RECORDING,
    DEEPSLEEP
};

volatile SlipRecorderState currentState = SlipRecorderState::DEEPSLEEP;


// TODO: Move these to the dedicated DMA section exposed in daisy_core.h
// Global SD resources (hardware-required placement in AXI SRAM for DMA)
SdmmcHandler global_sdmmc_handler __attribute__((section(".sram1_bss")));
FatFSInterface global_fsi_handler __attribute__((section(".sram1_bss")));
FIL global_wav_file __attribute__((section(".sram1_bss")));
#include "SDCard.hpp"

float DMA_BUFFER_MEM_SECTION dmaAudioBuffer[2][BLOCKSIZE/2];

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
  float audioBuffer[WriteBufferSize];
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

  bool sdStatus() const { return this->sdCardOk; }
  bool recording() const { return this->isRecording; }

  bool FRESULT_Error_Print(FRESULT res) {
    switch (res) {
            case FR_DISK_ERR: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Disk I/O error");
                    return false;
                }
                break;

            case FR_INT_ERR: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Internal assertion error");
                    return false;
                }
                break;

            case FR_NOT_READY: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Physical drive not ready");
                    return false;
                }
                break;

            case FR_NO_FILE: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: File not found");
                    return false;
                }
                break;

            case FR_NO_PATH: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Path not found");
                    return false;
                }
                break;

            case FR_INVALID_NAME: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Invalid path name format");
                    return false;
                }
                break;

            case FR_DENIED: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Access denied or directory full");
                    return false;
                }
                break;

            case FR_EXIST: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Access denied (object already exists)");
                    return false;
                }
                break;

            case FR_INVALID_OBJECT: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Invalid file or directory object");
                    return false;
                }
                break;

            case FR_WRITE_PROTECTED: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Drive is write protected");
                    return false;
                }
                break;

            case FR_INVALID_DRIVE: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Invalid logical drive number");
                    return false;
                }
                break;

            case FR_NOT_ENABLED: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Volume has no work area");
                    return false;
                }
                break;

            case FR_NO_FILESYSTEM: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: No valid FAT filesystem");
                    return false;
                }
                break;

            case FR_MKFS_ABORTED: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: f_mkfs() aborted");
                    return false;
                }
                break;

            case FR_TIMEOUT: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Timeout waiting for volume access");
                    return false;
                }
                break;

            case FR_LOCKED: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Operation rejected by file sharing policy");
                    return false;
                }
                break;

            case FR_NOT_ENOUGH_CORE: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Could not allocate LFN working buffer");
                    return false;
                }
                break;

            case FR_TOO_MANY_OPEN_FILES: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Too many open files");
                    return false;
                }
                break;

            case FR_INVALID_PARAMETER: {
                    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: Invalid parameter");
                    return false;
                }
                break;

            case FR_OK:
            default:
                break;
        }
  }

  bool InitSDCard() {
    Jaffx::Firmware::instance->hardware.PrintLine("SD Card Initialization Begin...");
    // Initialize SDMMC hardware
    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sd_cfg.speed = SdmmcHandler::Speed::FAST;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_4; // Take advantage of full-hardware setup available
    sd_cfg.clock_powersave = false;
    
    if(sdmmc_handler.Init(sd_cfg) != SdmmcHandler::Result::OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("SDMMC Handler Init Failed");
      return false;
    }
    
    System::Delay(100);
    
    // Initialize FatFS interface
    FatFSInterface::Config fsi_cfg;
    fsi_cfg.media = FatFSInterface::Config::MEDIA_SD;

    FatFSInterface::Result res1 = fsi_handler.Init(fsi_cfg);
    
    if(res1 != FatFSInterface::Result::OK) {
        switch (res1) {
            case FatFSInterface::Result::ERR_TOO_MANY_VOLUMES: {
                Jaffx::Firmware::instance->hardware.PrintLine("FSI Handler Init Failed: ERR_TOO_MANY_VOLUMES");
                return false;
            }
            case FatFSInterface::Result::ERR_NO_MEDIA_SELECTED: {
                Jaffx::Firmware::instance->hardware.PrintLine("FSI Handler Init Failed: ERR_NO_MEDIA_SELECTED");
                return false;
            }
            case FatFSInterface::Result::ERR_GENERIC: {
                Jaffx::Firmware::instance->hardware.PrintLine("FSI Handler Init Failed: ERR_GENERIC");
                return false;
            }
        }
      
      return false;
    }

    System::Delay(1000);
    Jaffx::Firmware::instance->hardware.PrintLine("Now mounting the filesystem...");
    
    // Mount filesystem
    FATFS& fs = fsi_handler.GetSDFileSystem();
    FRESULT res = f_mount(&fs, fsi_handler.GetSDPath(), 1);
    if (res != FRESULT::FR_OK) {
        return FRESULT_Error_Print(res);
    }
    
    if(res != FR_OK) {
      // Try disk initialize
      if(disk_initialize(0) == 0) {
        System::Delay(200);
        Jaffx::Firmware::instance->hardware.PrintLine("Now mounting the filesystem again...");
        res = f_mount(&fs, fsi_handler.GetSDPath(), 1);
      }
      else {
        Jaffx::Firmware::instance->hardware.PrintLine("Disk Initialize 0 failed");
        return false;
      }
    }
    
    if(res != FR_OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("SDMMC Handler Init Failed");
      return false;
    }
    switch (disk_status(0)) {
        case DRESULT::RES_ERROR: {
                Jaffx::Firmware::instance->hardware.PrintLine("Disk Status: R/W error");
                return false;
            }
            break;
        case DRESULT::RES_WRPRT: {
                Jaffx::Firmware::instance->hardware.PrintLine("Disk Status: Write protected error");
                return false;
            }
            break;
        case DRESULT::RES_NOTRDY: {
                Jaffx::Firmware::instance->hardware.PrintLine("Disk Status: Not ready error");
                return false;
            }
            break;
        case DRESULT::RES_PARERR: {
                Jaffx::Firmware::instance->hardware.PrintLine("Disk Status: Invalid parameter error");
                return false;
            }
            break;
    }
    switch (disk_initialize(0)) {
        case DRESULT::RES_ERROR: {
                Jaffx::Firmware::instance->hardware.PrintLine("Disk Initialize: R/W error");
                return false;
            }
            break;
        case DRESULT::RES_WRPRT: {
                Jaffx::Firmware::instance->hardware.PrintLine("Disk Initialize: Write protected error");
                return false;
            }
            break;
        case DRESULT::RES_NOTRDY: {
                Jaffx::Firmware::instance->hardware.PrintLine("Disk Initialize: Not ready error");
                return false;
            }
            break;
        case DRESULT::RES_PARERR: {
                Jaffx::Firmware::instance->hardware.PrintLine("Disk Initialize: Invalid parameter error");
                return false;
            }
            break;
    }
    // // Check disk status
    // if(disk_status(0) != 0) {
    //   if(disk_initialize(0) != 0) {
    //     return false;
    //   }
    // }
    
    System::Delay(100);
    sdCardOk = true;
    return true;
  }  

  bool StartRecording() {
    Jaffx::Firmware::instance->hardware.PrintLine("Attempt Start Recording...");
    if(!sdCardOk || isRecording) {
      return false;
    }
    Jaffx::Firmware::instance->hardware.PrintLine("Getting next number");
    // Get next recording number from counter file
    // TODO: React accordingly to errors, change return type + out-pointer (see comments within function)
    unsigned int recordNum = GetNextRecordingNumber();
    
    // Generate filename with sequential number
    // Format: JAFFX_SlipRecorder/rec_NNNNN.wav where NNNNN is a sequential number
    char filename[128];
    snprintf(filename, sizeof(filename), "JAFFX_SlipRecorder/rec_%05u.wav", recordNum);
    Jaffx::Firmware::instance->hardware.PrintLine("Filename created");
    // Open file for writing
    FRESULT res = f_open(&wav_file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if(res != FR_OK) {
        bool returnVal = FRESULT_Error_Print(res);
        sdCardOk = false;
        return returnVal;
    }
    
    // Write WAV header (will be updated periodically)
    WavHeader header;
    // Initialize header fields
    header.riff[0] = 'R'; header.riff[1] = 'I'; header.riff[2] = 'F'; header.riff[3] = 'F';
    header.fileSize = 36; // Will be updated
    header.wave[0] = 'W'; header.wave[1] = 'A'; header.wave[2] = 'V'; header.wave[3] = 'E';
    header.fmt[0] = 'f'; header.fmt[1] = 'm'; header.fmt[2] = 't'; header.fmt[3] = ' ';
    header.fmtSize = 16;
    header.audioFormat = 3; // 1 - PCM, 3 - IEEE float
    header.numChannels = 1; // Mono
    header.sampleRate = 48000;
    header.byteRate = 48000 * 4; // SampleRate * NumChannels * BitsPerSample/8
    header.blockAlign = 4; // NumChannels * BitsPerSample/8
    header.bitsPerSample = 32; // b/c float-32
    header.data[0] = 'd'; header.data[1] = 'a'; header.data[2] = 't'; header.data[3] = 'a';
    header.dataSize = 0; // Will be updated
    
    UINT bytes_written;
    res = f_write(&wav_file, &header, sizeof(header), &bytes_written);

    if (res != FR_OK) {
        f_close(&wav_file);
        sdCardOk = false;
        return FRESULT_Error_Print(res);
    }
    
    if(bytes_written != sizeof(header)) {
        Jaffx::Firmware::instance->hardware.PrintLine("Could not write header");
      f_close(&wav_file);
      sdCardOk = false;
      return false;
    }
    
    // Initialize recording state
    recordedSamples = 0;
    bufferIndex = 0;
    recordStartTime = System::GetNow();
    isRecording = true;
    
    // Sync to ensure header is written
    f_sync(&wav_file);
    Jaffx::Firmware::instance->hardware.PrintLine("Starting recording!");
    return true;
  }
#define JAFFX_RECORD_COUNTER_FILENAME "JAFFX_SlipRecorder/.rec_counter"
  // TODO: Change return type to bool (for error or not) & have out-pointer for return value
  unsigned int GetNextRecordingNumber() {
    // Try to read the counter file
    FIL counter_file;
    unsigned int recordNum = 0;

    FRESULT res = f_mkdir("JAFFX_SlipRecorder");
    if (res != FR_OK && res != FR_EXIST) {
        // Folder creation failed
        return FRESULT_Error_Print(res);
    }
    
    res = f_open(&counter_file, JAFFX_RECORD_COUNTER_FILENAME, FA_READ);
    switch (res) {
        case FR_OK: {
            // This means the file exists, there's a previous number we want from here
            // File exists, read the counter
            UINT bytes_read;
            char buffer[16];
            res = f_read(&counter_file, buffer, sizeof(buffer) - 1, &bytes_read);
            if (res != FR_OK) {
                // TODO: Close all files and all open pointers
                return FRESULT_Error_Print(res);
            }
            if (bytes_read <= 0) {
                // TODO: Close all files and all open pointers
                // TODO: Mark our state as unable to do this
                return false;
            }

            buffer[bytes_read] = '\0';
            recordNum = atoi(buffer);

            f_close(&counter_file);
            
        }
        break;
        case FR_NO_FILE: {
            // The file does not exist yet, this is fine as we'll end up creating it anyways
        }
        break;
        default: {
            // TODO: Close files and all open pointers
            return FRESULT_Error_Print(res);
        }

    }
    
    // Increment for next recording
    recordNum++;
    
    // Write back the incremented counter
    res = f_open(&counter_file, JAFFX_RECORD_COUNTER_FILENAME, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        // TODO: Close all files and all open pointers
        // TODO: Mark our state as unable to write back the incremented number, recording cannot start
        return FRESULT_Error_Print(res);
    }

    char buffer1[16];
    int len = snprintf(buffer1, sizeof(buffer1), "%u", recordNum);
    UINT bytes_written;

    res = f_write(&counter_file, buffer1, len, &bytes_written);
    if (res != FR_OK) {
        // TODO: Close all files and all open pointers
        // TODO: Mark our state as unable to write back the incremented number, recording cannot start
        return FRESULT_Error_Print(res);
    }
    f_close(&counter_file);
    
    // TODO: Change return type to return the actual error or bool, send `recordNum` in out-pointer
    return recordNum;
  }

  /*
  void WriteAudioSample(float sample) {
    if(!isRecording) {
      return;
    }
    
    // TODO: Take full buffer and arm_mult by max val at once
    // Convert float (-1.0 to 1.0) to 16-bit PCM
    int16_t pcm_sample = static_cast<int16_t>(sample * 32767.0f);
    
    // Add to buffer
    audioBuffer[bufferIndex++] = pcm_sample;
    recordedSamples++;
    
    // TODO: No file writes in audio loop
    // Write buffer to SD card when full
    if(bufferIndex >= WriteBufferSize) {
      FlushAudioBuffer();
    }
    
    // TODO: Use a hardware timer to periodically sync, NO DIVIDES IN AUDIO LOOP IF WE CAN HELP IT
    // Periodically sync and update header to prevent data loss (every ~5 seconds at 48kHz)
    if(recordedSamples % (48000 * 5) == 0) {
      UpdateWavHeader();  // Update header with current size
      f_sync(&wav_file);
    }
  }
*/

  void WriteAudioBlock(float* bufferLeft, size_t size) {
    if(!isRecording) {
      return;
    }

    // q15_t convertedLeft[size];
    // arm_float_to_q15(bufferLeft, convertedLeft, size);
    for (size_t i = 0; i < size; i++) {
        audioBuffer[bufferIndex++] = bufferLeft[i];
        recordedSamples++;
    }
    FlushAudioBuffer();
    
    if(recordedSamples % (48000 * 5) == 0) {
        UpdateWavHeader();  // Update header with current size
        f_sync(&wav_file);
    }

  }

  // TODO: Proper error handling
  void FlushAudioBuffer() {
    if(!isRecording || bufferIndex == 0) {
      return;
    }
    
    UINT bytes_written;
    size_t bytes_to_write = bufferIndex * sizeof(float);
    
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
    Jaffx::Firmware::instance->hardware.PrintLine("Stopping recording");
  }
  
  void UpdateWavHeader() {
    // TODO: Add all safety checks on each file operation
    // Save current file position
    FSIZE_t current_pos = f_tell(&wav_file);
    
    // Calculate actual data size
    unsigned int dataSize = recordedSamples * sizeof(float);
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


#include "Interrupts.hpp"

void sleepMode();

class SlipRecorder : public Jaffx::Firmware {
private:
  SlipRecorder() = default;
  ~SlipRecorder() = default;
  
public:
  static SlipRecorder& Instance() {
    static SlipRecorder instance;
    return instance;
  }
  SlipRecorder(const SlipRecorder&) = delete;
  SlipRecorder(SlipRecorder&&) = delete;
  SlipRecorder& operator=(const SlipRecorder&) = delete;
  SlipRecorder& operator=(SlipRecorder&&) = delete;

  GPIO mLeds[3];
  GPIO powerLED;
  float RmsReport = 0.f;
  SDCardWavWriter<> mWavWriter;  // Use default template parameter (4096 bytes)

  inline void deinit() {
    for (auto& led : mLeds) {
      led.DeInit();
    }
    mWavWriter.StopRecording();
    hardware.StopAudio();
    powerLED.Write(false); // Indicate power off
    powerLED.DeInit();
    DisableRecordingLED();
    DisableSDCardDetect();
    // No need to disable the USB or the Power Button detection as they are needed in sleep
    hardware.DeInit();
  }

  inline void shutdownSequence() {
    hardware.PrintLine("Initiating Shutdown Sequence...");
    deinit();
    sleepMode();
  }

  inline void init() override {
    hardware.PrintLine("Starting Init");
    // Initialize LEDs
    mLeds[0].Init(seed::D21, GPIO::Mode::OUTPUT);
    mLeds[1].Init(seed::D20, GPIO::Mode::OUTPUT);
    mLeds[2].Init(seed::D19, GPIO::Mode::OUTPUT);
    powerLED.Init(seed::D22, GPIO::Mode::OUTPUT);
    hardware.PrintLine("LEDs Init-ed");

    // System::Delay(2000); // Wait for 2s before going into deep sleep
    // deinit();
    // sleepMode(); // Enter sleep mode
    // return;
    // Initialize SD card
    if (!mWavWriter.InitSDCard()) {
      hardware.PrintLine("SD Init Failed!!");
    } 

    // TODO: check this is right???? choose state based on SD status
    // if (mWavWriter.sdStatus()) {
    //     currentState = SlipRecorderState::RECORDING;
    // } else {
    //   currentState = SlipRecorderState::DEEPSLEEP;
    // }
    
    // Enable detection interrupts
    EnableSDCardDetect();
    // EnableUSBDetect();
    // EnablePowerButtonDetect();
    EnableRecordingLED();

    
    // Start recording if SD card is OK
    if(mWavWriter.sdStatus()) {
      if (!mWavWriter.StartRecording()) {
        hardware.PrintLine("SD Start Failed!!!");
        currentState = SlipRecorderState::DEEPSLEEP;
      }
      else {
        currentState = SlipRecorderState::RECORDING;
        StartRecordingLED();
      }
    }

    powerLED.Write(true); // Indicate power on
    if (mWavWriter.recording()) {
      hardware.SetLed(true);
    }
  }

  inline void CustomAudioBlockCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) override {
    // TODO: Use this to send the whole block in to the SD card
    // For now, basic loop
    if (!mWavWriter.recording()) return;
    for (size_t i = 0; i < 2; i++) {
        const float* inChannel = in[i]; 
        float* dmaAudioBufferCorrespondingChannel = dmaAudioBuffer[i];
        for (size_t j = 0; j < size; j++) {
            dmaAudioBufferCorrespondingChannel[j] = inChannel[j];
        }
    }
    mWavWriter.WriteAudioBlock(dmaAudioBuffer[0], size);
    // for (size_t i = 0; i < size; i++) {
    //     mWavWriter.WriteAudioSample(dmaAudioBuffer[0][i]); // format is in/out[channel][sample]
    // }
    

    

    // TODO: Intrinsic STM32 memcpy here
    // out = in;
  }
  // inline float processAudio(float in) override {
  //   // TODO: return in and maybe store this somewhere else
  //   // Write sample to SD card if recording
  //   if(mWavWriter.recording()) {
  //     mWavWriter.WriteAudioSample(in);
  //   }
    
  //   // Calculate RMS for LED display
  //   static int counter = 0;
  //   static float RMS = 0.f;
  //   counter++;
  //   RMS += in * in;
  //   if (counter >= buffersize){
  //     RmsReport = sqrt(RMS); // update report
  //     counter = 0; // reset counter
  //     RMS = 0.f; // reset RMS
  //   }
    
  //   return in; // output throughput 
  // }

  void on_PB12_rising() {
      hardware.PrintLine("Rising Edge Detected");
    }
    inline void on_PB12_fully_risen() {
        hardware.PrintLine("SD Card Fully Removed");
        switch (currentState) {
            case SlipRecorderState::RECORDING: {
                /**
                 * TODO: Immediately stop recording
                 * Disable everything and shutdown
                 * 
                 */
                mWavWriter.StopRecording();
                currentState = SlipRecorderState::DEEPSLEEP;
                // shutdownSequence();

            }
                break;
            case SlipRecorderState::DEEPSLEEP: {
                /**
                 * Disable everything and shutdown again
                 * 
                 */
            }
                break;
                default: {
                    // TODO: Warn about unexpected state
                }
                    break;
        }
    }

  void on_PB12_falling() {
    hardware.PrintLine("Falling Edge Detected");
  }
    inline void on_PB12_fully_fallen() {
        hardware.PrintLine("SD Card Fully Inserted");
        switch (currentState) {
            case SlipRecorderState::RECORDING: {
                /**
                 * Should not be possible - warn!!!!
                 */

            }
                break;
            case SlipRecorderState::DEEPSLEEP: {
                /**
                 * Start recording again
                 * 
                 */
                mWavWriter.InitSDCard();
                if(mWavWriter.sdStatus()) {
                    mWavWriter.StartRecording();
                }
                currentState = SlipRecorderState::RECORDING;
            }
                break;
        }
    }

  void on_PA2_rising() {
      hardware.PrintLine("USB Connected");
  }
  
  void on_PA2_fully_risen() {
    hardware.PrintLine("USB Fully Connected");
    // TODO: We don't really care about this if we don't use the internal battery
  }

    void on_PA2_falling() {
        hardware.PrintLine("USB Disconnected");
    }

    void on_PA2_fully_fallen() {
        hardware.PrintLine("USB Fully Disconnected");
        // TODO: We don't really care about this without the battery
    }

    inline void on_PC0_short_press() {
        hardware.PrintLine("Power Button Short-Pressed");
        switch (currentState) {
            case SlipRecorderState::RECORDING: {
                // TODO: We want to save the recording safely and then shutdown gracefully
                mWavWriter.StopRecording();
                currentState = SlipRecorderState::DEEPSLEEP;
                // shutdownSequence();
            }
            break;
            case SlipRecorderState::DEEPSLEEP: {
                // TODO: We want to reinitialize everything and attempt to start the recording
                // If everything successful:
                currentState = SlipRecorderState::RECORDING;
            }
            break;
            default: {
                // TODO: Warn about unhandled state
            }

        }
    }

  void updateClipDetectorLEDs() {
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
    hardware.SetLed(mWavWriter.recording());
  }

    void loop() override {
        switch (currentState) {
            case SlipRecorderState::RECORDING: {
                // RMS calculation #quikMathss
                float myRMSLeft, myRMSRight;
                arm_rms_f32(dmaAudioBuffer[0], buffersize, &myRMSLeft);
                arm_rms_f32(dmaAudioBuffer[1], buffersize, &myRMSRight);
                RmsReport = (myRMSLeft > myRMSRight) ? myRMSLeft : myRMSRight;
                updateClipDetectorLEDs();
                // TODO: (if applies) handle battery states and such here
                // hardware.PrintLine("Current State: RECORDING");
                }
                break;
            case SlipRecorderState::DEEPSLEEP: {
                // TODO: Nothing much, hbu?
                // hardware.PrintLine("Current State: DEEPSLEEP");
            }
                break;
                default: {
                    // TODO: Warn about unhandled state here
                }
        }
        // System::Delay(5000);
        
    // if (usb_connected) {
    //   updateClipDetectorLEDs();
    // }

    // hardware.SetLed(mWavWriter.recording());
    
    // System::Delay(1);
  }
};

void wakeUp() {
    switch (currentState) {
        case SlipRecorderState::RECORDING: {
            // TODO: This shouldn't be possible, warn about this
        }
        break;
        case SlipRecorderState::DEEPSLEEP: {
            // TODO: Reinitialize and make sure everything is on
            // TODO: Make sure this doesn't clash with the power ON interrupt already
            currentState = SlipRecorderState::RECORDING;
        }
        break;
        default: {
            // TODO: Warn about unhandled case
        }
        break;
    }
}

inline void sleepMode() {
  // Clean D-Cache before entering sleep (recommended by documentation)
  SCB_CleanDCache();
  
  // Configure domains before putting CPU in deep sleep
  SET_BIT(PWR->CPUCR, PWR_CPUCR_PDDS_D1);      // D1 STANDBY
  SET_BIT(PWR->CPUCR, PWR_CPUCR_PDDS_D2);      // D2 STANDBY
  CLEAR_BIT(PWR->CPUCR, PWR_CPUCR_PDDS_D3);    // D3 STOP
  
  // Use low-power regulator as per existing EnterSTOP2Mode function
  MODIFY_REG(PWR->CR1, PWR_CR1_LPDS, PWR_LOWPOWERREGULATOR_ON);
  
  // Set deep sleep bit -> sends CPU to deep sleep after a WFI or WFE
  SET_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);

  // Waits for instructions to finish as per existing EnterSTOP2Mode function
  __DSB();
  __ISB();

  // WFI to put into sleep mode until interrupt detected
  __WFI();

  // Wake up
  CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);

  wakeUp();
}


inline void StartUSBDebounceTimer() {
  Jaffx::Firmware::instance->hardware.PrintLine("Starting USB Debounce Timer");
    TIM13->CNT = 0; // Reset the timer counter
    TIM13->CR1 |= TIM_CR1_CEN; // Start the timer
}

inline void StartSDDebounceTimer() {
  Jaffx::Firmware::instance->hardware.PrintLine("Starting SD Debounce Timer");
    TIM14->CNT = 0; // Reset the timer counter
    TIM14->CR1 |= TIM_CR1_CEN; // Start the timer
}


volatile uint32_t powerButtonInitiallyPressedAtTimeUs = 0;
volatile bool powerButtonPressedDown = false;
// Power Button IRQ Handler
extern "C" void EXTI0_IRQHandler(void) {
  // Check if EXTI0 triggered the interrupt
    if (EXTI->PR1 & EXTI_PR1_PR0) {
        /* Clear pending flag */
        EXTI->PR1 |= EXTI_PR1_PR0;

        /* Determine edge by reading input */
        SlipRecorder& mInstance = SlipRecorder::Instance();
        if (GPIOC->IDR & GPIO_IDR_ID0) { // Rising edge detected
            // Get System::GetUs(), set bool powerButtonPressActive = true;
            powerButtonInitiallyPressedAtTimeUs = System::GetUs();
            powerButtonPressedDown = true;
        //   mInstance.on_PC0_rising(); // This logic will go in falling edge now
        }
        else { // Falling edge detected
            // If powerButtonPressActive, see if time elapsed >= ~4 sec, reset bool
            //  if it is, run long press code (infinite timeout mode)
            //  else, run short press code (power button pressed rising edge)
            if (powerButtonPressedDown) {
                powerButtonPressedDown = false;
                uint32_t timeNow = System::GetUs();
                uint32_t timeElapsedUs = timeNow - powerButtonInitiallyPressedAtTimeUs;
                if (timeElapsedUs >= 4000000) {
                    // If it was pressed down for more than 4 sec, go into infinite timeout mode
                    System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
                }
                else {
                    // Run the normal button pressed
                    mInstance.on_PC0_short_press();
                }
            }
        }
    }
}

// Used to software debounce interrupts (using hardware timers) 
enum class InterruptState : unsigned char { // inherit from unsigned char to save space
    FALLING = 0,
    RISING = 1,  
    NONE = 2
};

volatile InterruptState USB_IRQ_State = InterruptState::NONE;

// USB 
extern "C" void EXTI2_IRQHandler(void) {
    // Check if EXTI2 triggered the interrupt
    if (EXTI->PR1 & EXTI_PR1_PR2) {
        // Clear the interrupt pending bit for EXTI2
        EXTI->PR1 |= EXTI_PR1_PR2;

        // Check if the debounce timer is already running
        if (TIM13->CR1 & TIM_CR1_CEN) return;
        
        // Else, determine if it was a rising or falling edge
        if (GPIOA->IDR & GPIO_IDR_ID2) {
            // Rising edge detected
            SlipRecorder::Instance().on_PA2_rising();
            // Save the state of what the new value is and we will see if it's the same as before
            USB_IRQ_State = InterruptState::RISING;
            
        } else {
            // Falling edge detected
            SlipRecorder::Instance().on_PA2_falling();
            // Save the state of what the new value is and we will see if it's the same as before
            USB_IRQ_State = InterruptState::FALLING;
        }
        StartUSBDebounceTimer();
    }
}

// For the USB connection debounce
extern "C" void TIM8_UP_TIM13_IRQHandler(void) {
    // Checks that TIM13 caused the interrupt
    Jaffx::Firmware::instance->hardware.PrintLine("TIM13 IRQ Handler Triggered");
    if (TIM13->SR & TIM_SR_UIF) { // Check update interrupt flag
        TIM13->SR &= ~TIM_SR_UIF; // Clear update interrupt flag

        // Check if the value is still the same as when the timer was started
        if (USB_IRQ_State == InterruptState::NONE) return;
        bool currentState = (GPIOA->IDR & GPIO_IDR_ID2) != 0; // (1 if high, 0 if low)
        if (USB_IRQ_State == InterruptState::RISING && currentState) {
            // We started rising and have settled on rising
            SlipRecorder::Instance().on_PA2_fully_risen();
        }
        else if (USB_IRQ_State == InterruptState::FALLING && !currentState) {
            // We started falling and have settled on falling
            SlipRecorder::Instance().on_PA2_fully_fallen();
        }
        else {
            // State changed during debounce period; no action taken
            Jaffx::Firmware::instance->hardware.PrintLine("USB: Not a valid bounce");
        }
        USB_IRQ_State = InterruptState::NONE;
        TIM13->CR1 &= ~TIM_CR1_CEN; // Stop the timer
    }
}

volatile InterruptState SD_IRQ_State = InterruptState::NONE;
// SD Card Connection Detection IRQ Handler
extern "C" void EXTI15_10_IRQHandler(void) {
    // Check if EXTI12 triggered the interrupt
    if (EXTI->PR1 & EXTI_PR1_PR12) {
        /* Clear pending flag */
        EXTI->PR1 |= EXTI_PR1_PR12;

        // Check if the debounce timer is already running
        if (TIM14->CR1 & TIM_CR1_CEN) return;

        // Else, determine if it was a rising or falling edge
        if (GPIOB->IDR & GPIO_IDR_ID12) {
            // Rising edge detected
            SlipRecorder::Instance().on_PB12_rising();
            // Save the state of what the new value is and we will see if it's the same as before
            SD_IRQ_State = InterruptState::RISING;
        } else {
            // Falling edge detected
            SlipRecorder::Instance().on_PB12_falling();
            // Save the state of what the new value is and we will see if it's the same as before
            SD_IRQ_State = InterruptState::FALLING;
        }
        StartSDDebounceTimer();
    }
}

// For the SD Card connection debounce
extern "C" void TIM8_TRG_COM_TIM14_IRQHandler(void) {
    // Checks that TIM14 caused the interrupt
    if (TIM14->SR & TIM_SR_UIF) { // Check update interrupt flag
        TIM14->SR &= ~TIM_SR_UIF; // Clear update interrupt flag

        if (SD_IRQ_State == InterruptState::NONE) return;
        bool currentState = (GPIOB->IDR & GPIO_IDR_ID12) != 0; // (1 if high, 0 if low)
        if (SD_IRQ_State == InterruptState::RISING && currentState) {
            // We started rising and have settled on rising
            SlipRecorder::Instance().on_PB12_fully_risen();
        }
        else if (SD_IRQ_State == InterruptState::FALLING && !currentState) {
            // We started falling and have settled on falling
            SlipRecorder::Instance().on_PB12_fully_fallen();
        }
        else {
            // State changed during debounce period; no action taken
            Jaffx::Firmware::instance->hardware.PrintLine("SD Card: Not a valid bounce");
        }

        SD_IRQ_State = InterruptState::NONE;
        TIM14->CR1 &= ~TIM_CR1_CEN; // Stop the timer
    }
}

int main() {
  SlipRecorder::Instance().start();
  // EXTIptr = mSlipRecorder::IRQHandler
  return 0;
}