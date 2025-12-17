#include "../../Gimmel/include/utility.hpp"
#include "../../Jaffx.hpp"
#include "stm32h750xx.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr_ex.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Interrupts.hpp"

// TODO: Move these to the dedicated DMA section exposed in daisy_core.h
// Global SD resources (hardware-required placement in AXI SRAM for DMA)
SdmmcHandler global_sdmmc_handler __attribute__((section(".sram1_bss")));
FatFSInterface global_fsi_handler __attribute__((section(".sram1_bss")));
FIL global_wav_file __attribute__((section(".sram1_bss")));
// #include "SDCard.hpp"

// Assuming BLOCKSIZE is defined in triggers/hal, and dmaAudioBuffer is used for
// DMA transfers. We'll keep this as is.
float DMA_BUFFER_MEM_SECTION dmaAudioBuffer[2][BLOCKSIZE / 2];

#include "diskio.h"

// SD write chunk in float samples (interleaved). Keep bytes aligned to SD blocks.
// 4096 floats * 4 bytes = 16384 bytes.
static constexpr size_t SD_WRITE_CHUNK_SAMPLES = 4096;

// Global aligned write buffer for SD operations (32-byte aligned)
static __attribute__((aligned(32))) float global_write_buffer[SD_WRITE_CHUNK_SAMPLES];

// Ring buffer size in floats (200k floats = 800KB in SDRAM)
static constexpr size_t RING_BUFFER_SIZE = 200000;

// Single-producer (audio callback), single-consumer (main loop) FIFO that uses
// giml::CircularBuffer as the backing storage.
template <typename T> class CircularFifo : public giml::CircularBuffer<T> {
public:
  void allocate(size_t size) {
    giml::CircularBuffer<T>::allocate(size);
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  void Reset() {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
    this->setWriteIndex(0);
  }

  size_t Available() const {
    size_t head = head_.load(std::memory_order_acquire);
    size_t tail = tail_.load(std::memory_order_relaxed);
    if (head >= tail) {
      return head - tail;
    } else {
      return head + (this->size() - tail);
    }
  }

  size_t Free() const {
    size_t available = Available();
    if (available >= this->size() - 1) return 0;
    return this->size() - 1 - available;
  }

  // Producer: push block. Returns elements written.
  size_t Push(const T *data, size_t count) {
    if (!this->getBuffer() || this->size() == 0 || count == 0)
      return 0;

    const size_t current_head = head_.load(std::memory_order_relaxed);
    const size_t current_tail = tail_.load(std::memory_order_acquire);

    const size_t available = (current_head >= current_tail) ? (current_head - current_tail) : (current_head + (this->size() - current_tail));
    const size_t free_space = (available >= this->size() - 1) ? 0 : this->size() - 1 - available;
    if (count > free_space)
      count = free_space;
    if (count == 0)
      return 0;

    size_t write_idx = current_head % this->size();
    size_t first_chunk = this->size() - write_idx;
    if (count <= first_chunk) {
      std::memcpy(&this->getBuffer()[write_idx], data, count * sizeof(T));
    } else {
      std::memcpy(&this->getBuffer()[write_idx], data,
                  first_chunk * sizeof(T));
      std::memcpy(&this->getBuffer()[0], data + first_chunk,
                  (count - first_chunk) * sizeof(T));
    }

    head_.store(current_head + count, std::memory_order_release);
    return count;
  }

  // Consumer: pop block. Returns elements read.
  size_t Pop(T *out, size_t count) {
    if (!this->getBuffer() || this->size() == 0 || count == 0)
      return 0;

    const size_t current_tail = tail_.load(std::memory_order_relaxed);
    const size_t current_head = head_.load(std::memory_order_acquire);

    const size_t available = (current_head >= current_tail) ? (current_head - current_tail) : (current_head + (this->size() - current_tail));
    if (count > available)
      count = available;
    if (count == 0)
      return 0;

    size_t read_idx = current_tail % this->size();
    size_t first_chunk = this->size() - read_idx;
    if (count <= first_chunk) {
      std::memcpy(out, &this->getBuffer()[read_idx], count * sizeof(T));
    } else {
      std::memcpy(out, &this->getBuffer()[read_idx], first_chunk * sizeof(T));
      std::memcpy(out + first_chunk, &this->getBuffer()[0],
                  (count - first_chunk) * sizeof(T));
    }

    tail_.store(current_tail + count, std::memory_order_release);
    return count;
  }

private:
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
};

// -- RING BUFFER --
enum class SlipRecorderState { RECORDING, DEEPSLEEP };

volatile SlipRecorderState currentState = SlipRecorderState::DEEPSLEEP;

class SDCardWavWriter {
private:
  SdmmcHandler &sdmmc_handler = global_sdmmc_handler;
  FatFSInterface &fsi_handler = global_fsi_handler;
  FIL &wav_file = global_wav_file;

  // buffer handling
  CircularFifo<float> mAudioFifo;
  volatile bool bufferOverflowed = false;


  volatile bool sdCardInserted = false;
  bool isRecording = false;
  unsigned int recordedSamples = 0;
  unsigned int lastSyncTime = 0;

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
  volatile bool sdSyncNeeded = false;

  void SetSDInserted(bool inserted) {
    sdCardInserted = inserted;
  }

  bool sdStatus() const { return this->sdCardInserted; }
  bool recording() const { return this->isRecording; }

  bool FRESULT_Error_Print(FRESULT res) {
    if (res == FR_OK)
      return true;
    Jaffx::Firmware::instance->hardware.PrintLine("=== FATFS ERROR DEBUG ===");
    Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: %d", res);
    extern SD_HandleTypeDef hsd1;
    HAL_SD_StateTypeDef sd_state = HAL_SD_GetState(&hsd1);
    uint32_t hal_error = HAL_SD_GetError(&hsd1);
    Jaffx::Firmware::instance->hardware.PrintLine("SDMMC State: %d", sd_state);
    Jaffx::Firmware::instance->hardware.PrintLine("HAL SD Error Code: 0x%08X", hal_error);
    Jaffx::Firmware::instance->hardware.PrintLine("=== END FATFS ERROR DEBUG ===");
    return false;
  }

  bool InitSDCard() {

    // Init ring buffer
    Jaffx::Firmware::instance->hardware.PrintLine("Initializing Ring Buffer..."); 
    float* ringBuffer = static_cast<float*>(Jaffx::mSDRAM.calloc(RING_BUFFER_SIZE, sizeof(float)));
    if (!ringBuffer) {
      Jaffx::Firmware::instance->hardware.PrintLine("SDRAM allocation failed!");
      return false;
    }
    mAudioFifo.setBuffer(ringBuffer, RING_BUFFER_SIZE);
    Jaffx::Firmware::instance->hardware.PrintLine("Audio Ring Buffer Initialized!");

    // Init SD card
    Jaffx::Firmware::instance->hardware.PrintLine("Initializing SD Card...");

    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sd_cfg.speed = SdmmcHandler::Speed::FAST;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_4;
    sd_cfg.clock_powersave = false;

    if (sdmmc_handler.Init(sd_cfg) != SdmmcHandler::Result::OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("=== SDMMC INIT ERROR DEBUG ===");
      extern SD_HandleTypeDef hsd1;
      HAL_SD_StateTypeDef sd_state = HAL_SD_GetState(&hsd1);
      uint32_t hal_error = HAL_SD_GetError(&hsd1);
      Jaffx::Firmware::instance->hardware.PrintLine("SDMMC State: %d", sd_state);
      Jaffx::Firmware::instance->hardware.PrintLine("HAL SD Error Code: 0x%08X", hal_error);
      Jaffx::Firmware::instance->hardware.PrintLine("SD Config: Speed=%d, Width=%d, PowerSave=%d", 
                                                   (int)sd_cfg.speed, (int)sd_cfg.width, sd_cfg.clock_powersave);
      Jaffx::Firmware::instance->hardware.PrintLine("=== END SDMMC INIT ERROR DEBUG ===");
      Jaffx::Firmware::instance->hardware.PrintLine("SDMMC Init Failed");
      return false;
    }

    System::Delay(10);
    FatFSInterface::Config fsi_cfg;
    fsi_cfg.media = FatFSInterface::Config::MEDIA_SD;

    if (fsi_handler.Init(fsi_cfg) != FatFSInterface::Result::OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("=== FATFS INIT ERROR DEBUG ===");
      extern SD_HandleTypeDef hsd1;
      HAL_SD_StateTypeDef sd_state = HAL_SD_GetState(&hsd1);
      uint32_t hal_error = HAL_SD_GetError(&hsd1);
      Jaffx::Firmware::instance->hardware.PrintLine("SDMMC State: %d", sd_state);
      Jaffx::Firmware::instance->hardware.PrintLine("HAL SD Error Code: 0x%08X", hal_error);
      Jaffx::Firmware::instance->hardware.PrintLine("FatFS Config: Media=%d", (int)fsi_cfg.media);
      Jaffx::Firmware::instance->hardware.PrintLine("=== END FATFS INIT ERROR DEBUG ===");
      Jaffx::Firmware::instance->hardware.PrintLine("FatFS Handler Init Failed");
      return false;
    }

    System::Delay(100);
    FATFS &fs = fsi_handler.GetSDFileSystem();
    FRESULT res = f_mount(&fs, fsi_handler.GetSDPath(), 1);

    if (res != FR_OK) {
      if (disk_initialize(0) == 0) {
        res = f_mount(&fs, fsi_handler.GetSDPath(), 1);
      }
    }

    if (res != FR_OK) {
      // Detailed mount error debugging
      Jaffx::Firmware::instance->hardware.PrintLine("=== SD MOUNT ERROR DEBUG ===");
      Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: %d", res);
      extern SD_HandleTypeDef hsd1;
      HAL_SD_StateTypeDef sd_state = HAL_SD_GetState(&hsd1);
      uint32_t hal_error = HAL_SD_GetError(&hsd1);
      Jaffx::Firmware::instance->hardware.PrintLine("SDMMC State: %d", sd_state);
      Jaffx::Firmware::instance->hardware.PrintLine("HAL SD Error Code: 0x%08X", hal_error);
      Jaffx::Firmware::instance->hardware.PrintLine("SD Path: %s", fsi_handler.GetSDPath());
      Jaffx::Firmware::instance->hardware.PrintLine("=== END SD MOUNT ERROR DEBUG ===");
      Jaffx::Firmware::instance->hardware.PrintLine("Mount Failed");
      return false;
    }

    sdCardInserted = true;
    Jaffx::Firmware::instance->hardware.PrintLine("SD Init Success");
    return true;
  }

  bool StartRecording() {
    if (!sdCardInserted || isRecording)
      return false;

    unsigned int recordNum = GetNextRecordingNumber();
    char filename[128];
    snprintf(filename, sizeof(filename), "JAFFX_SlipRecorder/rec_%05u.wav",
             recordNum);

    FRESULT res = f_open(&wav_file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
      FRESULT_Error_Print(res);
      return false;
    }

    WavHeader header;
    memcpy(header.riff, "RIFF", 4);
    header.fileSize = 36;
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    header.fmtSize = 16;
    header.audioFormat = 3; // Float
    header.numChannels = 2;
    header.sampleRate = 48000;
    header.bitsPerSample = 32;
    header.blockAlign = header.numChannels * (header.bitsPerSample / 8);
    header.byteRate = header.sampleRate * header.blockAlign;
    memcpy(header.data, "data", 4);
    header.dataSize = 0;

    UINT bw;
    f_write(&wav_file, &header, sizeof(header), &bw);
    f_sync(&wav_file);

    mAudioFifo.Reset();
    bufferOverflowed = false;
    recordedSamples = 0;
    isRecording = true;
    lastSyncTime = System::GetNow();

    Jaffx::Firmware::instance->hardware.PrintLine("Recording to: %s", filename);
    return true;
  }

  void StopRecording() {

    if (!isRecording) {
      return;
    }

    isRecording = false;
    ProcessBackgroundWrite(); // Flush remaining
    UpdateWavHeader();
    f_close(&wav_file);
    Jaffx::Firmware::instance->hardware.PrintLine("Recording Stopped. Samples: %u", recordedSamples);
  }

  unsigned int GetNextRecordingNumber() {
    FIL counter_file;
    unsigned int recordNum = 0;
    f_mkdir("JAFFX_SlipRecorder");
    if (f_open(&counter_file, "JAFFX_SlipRecorder/.rec_counter", FA_READ) ==
        FR_OK) {
      char buffer[16];
      UINT br;
      f_read(&counter_file, buffer, sizeof(buffer) - 1, &br);
      buffer[br] = 0;
      recordNum = atoi(buffer);
      f_close(&counter_file);
    }
    recordNum++;
    if (f_open(&counter_file, "JAFFX_SlipRecorder/.rec_counter",
               FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
      char buffer[16];
      int len = snprintf(buffer, sizeof(buffer), "%u", recordNum);
      UINT bw;
      f_write(&counter_file, buffer, len, &bw);
      f_close(&counter_file);
    }
    return recordNum;
  }

  void WriteAudioBlock(const float *left, const float *right, size_t size) {
    if (!isRecording)
      return;
    if (size > 256)
      return; // Safety

    // Interleave into a small stack buffer and push as a block.
    float interleaved[512];
    const size_t interleavedCount = size * 2;
    for (size_t i = 0; i < size; ++i) {
      interleaved[i * 2] = left[i];
      interleaved[i * 2 + 1] = right[i];
    }

    const size_t written = mAudioFifo.Push(interleaved, interleavedCount);
    if (written != interleavedCount) {
      // Never stop/close files from the audio callback.
      bufferOverflowed = true;
    }
  }

  void ProcessBackgroundWrite() {

    // Critical check to avoid hanging if card removed
    if (!sdCardInserted && isRecording) {
      Jaffx::Firmware::instance->hardware.PrintLine("ProcessBackgroundWrite: SD Card Removed! Stopping recording.");
      isRecording = false;
      return;
    }

    if (!isRecording) {
      return;
    }


    if (bufferOverflowed) {
      Jaffx::Firmware::instance->hardware.PrintLine("ERROR: Audio FIFO overflow (dropping samples). Stopping recording.");
      StopRecording();
      return;
    }

    float* tempWriteBuffer = global_write_buffer;

    // Write as many full chunks as are available.
    while (mAudioFifo.Available() >= SD_WRITE_CHUNK_SAMPLES) {
      const size_t samplesRead = mAudioFifo.Pop(tempWriteBuffer, SD_WRITE_CHUNK_SAMPLES);
      if (samplesRead == 0)
        break;

      // Pre-write debugging: Only log on error, but prepare info
      FSIZE_t pre_write_pos = f_tell(&wav_file);
      extern SD_HandleTypeDef hsd1;
      HAL_SD_StateTypeDef sd_state = HAL_SD_GetState(&hsd1);
      uint32_t hal_error = HAL_SD_GetError(&hsd1);
      uintptr_t buffer_addr = (uintptr_t)tempWriteBuffer;
      bool buffer_aligned = (buffer_addr % 32) == 0;
      uint32_t expected_bytes = samplesRead * sizeof(float);

      UINT bw;
      uint32_t write_start_time = System::GetUs();
      FRESULT res = f_write(&wav_file, tempWriteBuffer,
                            samplesRead * sizeof(float), &bw);
      uint32_t write_end_time = System::GetUs();
      uint32_t write_duration_us = write_end_time - write_start_time;

      if (res != FR_OK || bw != expected_bytes) {
        // Detailed error debugging
        Jaffx::Firmware::instance->hardware.PrintLine("=== SD WRITE ERROR DEBUG ===");
        Jaffx::Firmware::instance->hardware.PrintLine("FRESULT: %d (FR_DISK_ERR=%d)", res, FR_DISK_ERR);
        Jaffx::Firmware::instance->hardware.PrintLine("Bytes Written: %u, Expected: %u", bw, expected_bytes);
        Jaffx::Firmware::instance->hardware.PrintLine("Write Duration: %u us", write_duration_us);
        Jaffx::Firmware::instance->hardware.PrintLine("File Position Before: %u", (unsigned int)pre_write_pos);
        Jaffx::Firmware::instance->hardware.PrintLine("File Position After: %u", (unsigned int)f_tell(&wav_file));
        Jaffx::Firmware::instance->hardware.PrintLine("SDMMC State: %d (READY=%d, BUSY=%d, ERROR=%d)", 
                                                     sd_state, HAL_SD_STATE_READY, HAL_SD_STATE_BUSY, HAL_SD_STATE_ERROR);
        Jaffx::Firmware::instance->hardware.PrintLine("HAL SD Error Code: 0x%08X", hal_error);
        Jaffx::Firmware::instance->hardware.PrintLine("Buffer Address: 0x%08X, Aligned (32B): %s", 
                                                     (unsigned int)buffer_addr, buffer_aligned ? "YES" : "NO");
        Jaffx::Firmware::instance->hardware.PrintLine("Chunk Samples: %u, Chunk Bytes: %u", (unsigned int)samplesRead, expected_bytes);
        Jaffx::Firmware::instance->hardware.PrintLine("FIFO Available Before Pop: %u", (unsigned int)mAudioFifo.Available() + samplesRead);
        Jaffx::Firmware::instance->hardware.PrintLine("Recorded Samples So Far: %u", recordedSamples);
        Jaffx::Firmware::instance->hardware.PrintLine("System Time: %u ms", System::GetNow());
        Jaffx::Firmware::instance->hardware.PrintLine("=== END SD WRITE ERROR DEBUG ===");

        Jaffx::Firmware::instance->hardware.PrintLine("ProcessBackgroundWrite: SD Write Error! Stopping recording.");
        StopRecording();
        return;
      }

      recordedSamples += samplesRead;

      if (System::GetNow() - lastSyncTime > 5000) {
        UpdateWavHeader();
        lastSyncTime = System::GetNow();
      }
    }
  }

  void UpdateWavHeader() {
    if (!sdCardInserted)
      return;

    FSIZE_t current_pos = f_tell(&wav_file);
    unsigned int dataSize = recordedSamples * sizeof(float);
    unsigned int fileSize = dataSize + sizeof(WavHeader) - 8;

    FRESULT res;
    UINT bw;

    res = f_lseek(&wav_file, 4);
    if (res != FR_OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("UpdateWavHeader: f_lseek(4) failed, res=%d", res);
      return;
    }
    res = f_write(&wav_file, &fileSize, 4, &bw);
    if (res != FR_OK || bw != 4) {
      Jaffx::Firmware::instance->hardware.PrintLine("UpdateWavHeader: f_write(fileSize) failed, res=%d, bw=%u", res, bw);
      return;
    }

    res = f_lseek(&wav_file, 40);
    if (res != FR_OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("UpdateWavHeader: f_lseek(40) failed, res=%d", res);
      return;
    }
    res = f_write(&wav_file, &dataSize, 4, &bw);
    if (res != FR_OK || bw != 4) {
      Jaffx::Firmware::instance->hardware.PrintLine("UpdateWavHeader: f_write(dataSize) failed, res=%d, bw=%u", res, bw);
      return;
    }

    res = f_lseek(&wav_file, current_pos);
    if (res != FR_OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("UpdateWavHeader: f_lseek(back) failed, res=%d", res);
      return;
    }

    res = f_sync(&wav_file);
    if (res != FR_OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("UpdateWavHeader: f_sync failed, res=%d", res);
      return;
    }
  }
};

class SlipRecorder : public Jaffx::Firmware {
private:
  SlipRecorder() = default;
  ~SlipRecorder() = default;

public:
  static SlipRecorder &Instance() {
    static SlipRecorder instance;
    return instance;
  }
  SlipRecorder(const SlipRecorder &) = delete;
  SlipRecorder(SlipRecorder &&) = delete;
  SlipRecorder &operator=(const SlipRecorder &) = delete;
  SlipRecorder &operator=(SlipRecorder &&) = delete;

  GPIO mLeds[3];
  GPIO powerLED;
  float RmsReport = 0.f;
  SDCardWavWriter mWavWriter;

  inline void deinit() {
    for (auto &led : mLeds) {
      led.DeInit();
    }
    mWavWriter.StopRecording();
    hardware.StopAudio();
    powerLED.Write(false); // Indicate power off
    powerLED.DeInit();
    DisableRecordingLED();
    DisableSDCardDetect();
    // No need to disable the USB or the Power Button detection as they are
    // needed in sleep
    hardware.DeInit();
  }

  inline void shutdownSequence() {
    hardware.PrintLine("Initiating Shutdown Sequence...");
    deinit();
  }

  inline void init() override {
    System::Delay(100);
    hardware.StartLog(true);
    System::Delay(100);
    hardware.PrintLine("Starting Init");
    // Initialize LEDs
    mLeds[0].Init(seed::D21, GPIO::Mode::OUTPUT);
    mLeds[1].Init(seed::D20, GPIO::Mode::OUTPUT);
    mLeds[2].Init(seed::D19, GPIO::Mode::OUTPUT);
    powerLED.Init(seed::D22, GPIO::Mode::OUTPUT);
    hardware.PrintLine("LEDs Init-ed");

    // Initialize SD card
    if (!mWavWriter.InitSDCard()) {
      hardware.PrintLine("SD Init Failed!!");
      // If failed, assume not inserted
      mWavWriter.SetSDInserted(false);
    }

    // Enable detection interrupts
    EnableSDCardDetect();
    // EnableUSBDetect();
    // EnablePowerButtonDetect();
    EnableRecordingLED();
    EnableSDSyncTimer();

    // Start recording if SD card is OK
    if (mWavWriter.sdStatus()) {
      if (!mWavWriter.StartRecording()) {
        hardware.PrintLine("SD Start Failed!!!");
        currentState = SlipRecorderState::DEEPSLEEP;
      } else {
        currentState = SlipRecorderState::RECORDING;
        StartRecordingLED();
      }
    }

    powerLED.Write(true); // Indicate power on
    if (mWavWriter.recording()) {
      hardware.SetLed(true);
    }
  }

  inline void CustomAudioBlockCallback(AudioHandle::InputBuffer in,
                                       AudioHandle::OutputBuffer out,
                                       size_t size) override {
    if (!mWavWriter.recording())
      return;
    for (size_t i = 0; i < 2; i++) {
      const float *inChannel = in[i];
      float *dmaAudioBufferCorrespondingChannel = dmaAudioBuffer[i];
      memcpy(dmaAudioBufferCorrespondingChannel, inChannel,
             size * sizeof(float));
    }
    // Push to Ring Buffer
    mWavWriter.WriteAudioBlock(dmaAudioBuffer[0], dmaAudioBuffer[1], size);
  }

  void on_PB12_rising() { 
    hardware.PrintLine("SD Card: Rising Edge Detected"); 
  }

  inline void on_PB12_fully_risen() {
    hardware.PrintLine("SD Card Fully Removed");

    mWavWriter.StopRecording();

    hardware.PrintLine("Resetting to Bootloader...");
    System::ResetToBootloader(daisy::System::DAISY);
  }

  void on_PB12_falling() { 
    hardware.PrintLine("SD Card: Falling Edge Detected"); 
  }

  inline void on_PB12_fully_fallen() {
    hardware.PrintLine("SD Card Fully Inserted");
    hardware.PrintLine("Resetting to Bootloader...");
    System::ResetToBootloader(daisy::System::DAISY);
  }

  void updateClipDetectorLEDs() {
    float dB = 20.0f * log10f(RmsReport + 1e-12f);

    if (dB >= 0.0f) {
      mLeds[0].Write(true);
      mLeds[1].Write(true);
      mLeds[2].Write(true);
    } else if (dB >= -6.0f) {
      mLeds[0].Write(false);
      mLeds[1].Write(true);
      mLeds[2].Write(true);
    } else if (dB >= -20.0f) {
      mLeds[0].Write(false);
      mLeds[1].Write(false);
      mLeds[2].Write(true);
    } else {
      mLeds[0].Write(false);
      mLeds[1].Write(false);
      mLeds[2].Write(false);
    }
    hardware.SetLed(mWavWriter.recording());
  }

  void loop() override {
    // Process Background Write in main loop
    mWavWriter.ProcessBackgroundWrite();

    switch (currentState) {
    case SlipRecorderState::RECORDING: {
      float myRMSLeft, myRMSRight;
      arm_rms_f32(dmaAudioBuffer[0], buffersize, &myRMSLeft);
      arm_rms_f32(dmaAudioBuffer[1], buffersize, &myRMSRight);
      RmsReport = (myRMSLeft > myRMSRight) ? myRMSLeft : myRMSRight;
      updateClipDetectorLEDs();
    } break;

    case SlipRecorderState::DEEPSLEEP: {
    } break;

    default: {
    }
    }
  }
};

void wakeUp() {
  switch (currentState) {
  case SlipRecorderState::RECORDING: {
  } break;

  case SlipRecorderState::DEEPSLEEP: {
    currentState = SlipRecorderState::RECORDING;
  } break;

  default: {
  } break;
  }
}

inline void sleepMode() {
  SCB_CleanDCache();

  SET_BIT(PWR->CPUCR, PWR_CPUCR_PDDS_D1);   // D1 STANDBY
  SET_BIT(PWR->CPUCR, PWR_CPUCR_PDDS_D2);   // D2 STANDBY
  CLEAR_BIT(PWR->CPUCR, PWR_CPUCR_PDDS_D3); // D3 STOP

  MODIFY_REG(PWR->CR1, PWR_CR1_LPDS, PWR_LOWPOWERREGULATOR_ON);

  SET_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);

  __DSB();
  __ISB();

  __WFI();

  CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);

  wakeUp();
}

inline void StartUSBDebounceTimer() {
  Jaffx::Firmware::instance->hardware.PrintLine("Starting USB Debounce Timer");
  TIM13->CNT = 0;            // Reset the timer counter
  TIM13->CR1 |= TIM_CR1_CEN; // Start the timer
}

inline void StartSDDebounceTimer() {
  Jaffx::Firmware::instance->hardware.PrintLine("Starting SD Debounce Timer");
  TIM14->CNT = 0;            // Reset the timer counter
  TIM14->CR1 |= TIM_CR1_CEN; // Start the timer
}


// Used to software debounce interrupts (using hardware timers)
enum class InterruptState : unsigned char {                                           
  FALLING = 0,
  RISING = 1,
  NONE = 2
};

volatile InterruptState SD_IRQ_State = InterruptState::NONE;
// SD Card Connection Detection IRQ Handler
extern "C" void EXTI15_10_IRQHandler(void) {
  if (EXTI->PR1 & EXTI_PR1_PR12) {
    EXTI->PR1 |= EXTI_PR1_PR12;

    if (TIM14->CR1 & TIM_CR1_CEN)
      return;

    if (GPIOB->IDR & GPIO_IDR_ID12) {
      SlipRecorder::Instance().on_PB12_rising();
      SD_IRQ_State = InterruptState::RISING;
    } else {
      SlipRecorder::Instance().on_PB12_falling();
      SD_IRQ_State = InterruptState::FALLING;
    }
    StartSDDebounceTimer();
  }
}

// For the SD Card connection debounce
extern "C" void TIM8_TRG_COM_TIM14_IRQHandler(void) {
  if (TIM14->SR & TIM_SR_UIF) {
    TIM14->SR &= ~TIM_SR_UIF;

    if (SD_IRQ_State == InterruptState::NONE)
      return;

    bool currentState = (GPIOB->IDR & GPIO_IDR_ID12) != 0;

    if (SD_IRQ_State == InterruptState::RISING && currentState) {
      SlipRecorder::Instance().on_PB12_fully_risen();
    } else if (SD_IRQ_State == InterruptState::FALLING && !currentState) {
      SlipRecorder::Instance().on_PB12_fully_fallen();
    } else {
      Jaffx::Firmware::instance->hardware.PrintLine(
          "SD Card: Not a valid bounce");
    }

    SD_IRQ_State = InterruptState::NONE;
    TIM14->CR1 &= ~TIM_CR1_CEN;
  }
}

// For periodically syncing the SD card
extern "C" void TIM17_IRQHandler(void) {
  if (TIM17->SR & TIM_SR_UIF) {
    TIM17->SR &= ~TIM_SR_UIF;
    if (SlipRecorder::Instance().mWavWriter.recording()) {
      Jaffx::Firmware::instance->hardware.PrintLine("Triggering Sync now...");
      SlipRecorder::Instance().mWavWriter.sdSyncNeeded = true;
    }
  }
}

int main() {
  SlipRecorder::Instance().start();
  // EXTIptr = mSlipRecorder::IRQHandler
  return 0;
}