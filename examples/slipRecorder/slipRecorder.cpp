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
// 4096 floats * 4 bytes = 16384 bytes (aligned to SD page size for better throughput)
static constexpr size_t SD_WRITE_CHUNK_SAMPLES = 4096;

// Ring buffer size in floats (1M floats = 4MB in SDRAM, ~10 seconds of audio)
// Large buffer needed because f_sync() can block for several seconds
static constexpr size_t RING_BUFFER_SIZE = 10000000;

// Monitoring interval in milliseconds
static constexpr uint32_t MONITOR_INTERVAL_MS = 2000;

// Sync interval - with pre-allocated file, sync should be fast (no cluster allocation)
static constexpr uint32_t SYNC_INTERVAL_MS = 10000; // 10 seconds - longer interval, but we have 104s buffer

// Pre-allocation size in bytes (2GB max for FAT32 safety)
static constexpr uint32_t PREALLOC_SIZE_BYTES = 2UL * 1024UL * 1024UL * 1024UL - 1024UL; // ~2GB minus small margin

// Warning threshold: warn when buffer is more than 25% full
static constexpr size_t BUFFER_WARNING_THRESHOLD = RING_BUFFER_SIZE / 4;

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

  // Get fill percentage (0-100) for monitoring
  uint8_t FillPercent() const {
    size_t avail = Available();
    if (this->size() == 0) return 0;
    return (uint8_t)((avail * 100) / this->size());
  }

  // Normalize head/tail indices to prevent unbounded growth
  // Call this from the consumer when buffer is nearly empty
  // Returns true if normalization was performed
  bool TryNormalize() {
    size_t current_head = head_.load(std::memory_order_acquire);
    size_t current_tail = tail_.load(std::memory_order_acquire);

    // Only normalize when buffer is empty and indices are large
    if (current_head == current_tail && current_head > this->size() * 2) {
      // Reset both to 0 atomically (safe because buffer is empty)
      tail_.store(0, std::memory_order_relaxed);
      head_.store(0, std::memory_order_release);
      return true;
    }
    return false;
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
  volatile bool recordingStopped = false;

  volatile bool sdCardInserted = false;
  volatile bool isRecording = false;
  unsigned int recordedSamples = 0;
  unsigned int lastSyncTime = 0;

  // Monitoring variables
  uint32_t lastMonitorTime = 0;
  uint32_t totalWriteCount = 0;
  uint32_t slowWriteCount = 0;       // Writes taking > 15ms
  uint32_t writeRetryCount = 0;      // Track write retries
  uint32_t maxWriteTimeUs = 0;       // Track worst-case write time
  uint32_t maxSyncTimeUs = 0;        // Track worst-case sync time
  uint32_t syncErrorCount = 0;       // Track sync failures
  uint8_t peakBufferFillPercent = 0; // Track peak buffer usage

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
    Jaffx::Firmware::instance->hardware.PrintLine("SetSDInserted called with %d", inserted);
    sdCardInserted = inserted;
    if (!inserted) {
      if (isRecording) {
        Jaffx::Firmware::instance->hardware.PrintLine("SD card removed during recording. Stopping.");
        StopRecording();
      }
    } else {
      Jaffx::Firmware::instance->hardware.PrintLine("SD card inserted.");
    }
  }

  bool sdStatus() const { return this->sdCardInserted; }
  bool recording() const { return this->isRecording; }

  bool FRESULT_Error_Print(FRESULT res) {
    if (res == FR_OK)
      return true;
    Jaffx::Firmware::instance->hardware.Print("SD Error: ");
    Jaffx::Firmware::instance->hardware.PrintLine("%d", res);
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
      Jaffx::Firmware::instance->hardware.PrintLine("SDMMC Init Failed");
      return false;
    }

    System::Delay(10);
    FatFSInterface::Config fsi_cfg;
    fsi_cfg.media = FatFSInterface::Config::MEDIA_SD;

    if (fsi_handler.Init(fsi_cfg) != FatFSInterface::Result::OK) {
      Jaffx::Firmware::instance->hardware.PrintLine(
          "FatFS Handler Init Failed");
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

    // PRE-ALLOCATE the file to avoid cluster allocation during recording
    // This makes subsequent syncs MUCH faster (only metadata, no FAT updates)
    Jaffx::Firmware::instance->hardware.PrintLine("Pre-allocating %lu MB (this may take a moment)...",
        PREALLOC_SIZE_BYTES / (1024 * 1024));

    uint32_t preallocStartMs = System::GetNow();

    // Expand the file by seeking to end and writing a byte
    res = f_lseek(&wav_file, PREALLOC_SIZE_BYTES);
    if (res != FR_OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("Pre-alloc seek failed: %d", res);
      f_close(&wav_file);
      return false;
    }

    // Write a single byte to force cluster allocation
    uint8_t dummy = 0;
    res = f_write(&wav_file, &dummy, 1, &bw);
    if (res != FR_OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("Pre-alloc write failed: %d", res);
      f_close(&wav_file);
      return false;
    }

    // Sync to commit all cluster allocations to FAT
    res = f_sync(&wav_file);
    if (res != FR_OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("Pre-alloc sync failed: %d", res);
      f_close(&wav_file);
      return false;
    }

    uint32_t preallocTimeMs = System::GetNow() - preallocStartMs;
    Jaffx::Firmware::instance->hardware.PrintLine("Pre-allocation complete in %lu ms", preallocTimeMs);

    // Seek back to just after the header to start writing audio data
    res = f_lseek(&wav_file, sizeof(header));
    if (res != FR_OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("Seek to start failed: %d", res);
      f_close(&wav_file);
      return false;
    }

    mAudioFifo.Reset();
    bufferOverflowed = false;
    recordingStopped = false;
    recordedSamples = 0;
    isRecording = true;
    lastSyncTime = System::GetNow();

    // Reset monitoring variables
    lastMonitorTime = System::GetNow();
    totalWriteCount = 0;
    slowWriteCount = 0;
    writeRetryCount = 0;
    maxWriteTimeUs = 0;
    maxSyncTimeUs = 0;
    syncErrorCount = 0;
    peakBufferFillPercent = 0;

    Jaffx::Firmware::instance->hardware.PrintLine("Recording to: %s", filename);
    Jaffx::Firmware::instance->hardware.PrintLine("Buffer: %lu floats (%lu KB, ~%lu sec), Chunk: %lu floats (%lu KB)",
        RING_BUFFER_SIZE, (RING_BUFFER_SIZE * sizeof(float)) / 1024,
        RING_BUFFER_SIZE / (48000 * 2),  // seconds of audio
        SD_WRITE_CHUNK_SAMPLES, (SD_WRITE_CHUNK_SAMPLES * sizeof(float)) / 1024);
    Jaffx::Firmware::instance->hardware.PrintLine("Sync interval: %lu sec (pre-allocated, should be fast)", SYNC_INTERVAL_MS / 1000);
    return true;
  }

  void StopRecording() {
    Jaffx::Firmware::instance->hardware.PrintLine("StopRecording called");

    // CRITICAL: Always set recordingStopped to prevent re-entry loops
    // Must be set BEFORE the isRecording check to avoid infinite loop
    if (recordingStopped) {
      return; // Already stopped, don't repeat
    }
    recordingStopped = true;

    if (!isRecording) {
      // Was already stopped externally, just mark as stopped
      return;
    }

    isRecording = false;
    ProcessBackgroundWrite(); // Flush remaining
    UpdateWavHeader();
    f_close(&wav_file);

    // Print final statistics
    float recordingSeconds = (float)recordedSamples / (48000.0f * 2.0f);
    Jaffx::Firmware::instance->hardware.PrintLine("Recording Stopped. Duration: " FLT_FMT3 "s, Samples: %u",
        FLT_VAR3(recordingSeconds), recordedSamples);
    Jaffx::Firmware::instance->hardware.PrintLine("Stats: Peak buf: %d%%, Writes: %lu, Retries: %lu, MaxWrite: %lu ms, MaxSync: %lu ms",
        peakBufferFillPercent, totalWriteCount, writeRetryCount, maxWriteTimeUs / 1000, maxSyncTimeUs / 1000);
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
      // CRITICAL: Never call PrintLine or any blocking I/O from audio callback!
      // Just set the flag - main loop will handle it
      bufferOverflowed = true;
    }
  }

  void ProcessBackgroundWrite() {

    // If recording was stopped externally, finalize it
    if (!isRecording && !recordingStopped && recordedSamples > 0) {
      Jaffx::Firmware::instance->hardware.PrintLine("Recording stopped externally, finalizing");
      StopRecording();
      return;
    }

    // Critical check to avoid hanging if card removed
    if (!sdCardInserted && isRecording) {
      Jaffx::Firmware::instance->hardware.PrintLine("SD card removed while recording - stopping");
      isRecording = false;
      recordingStopped = true; // CRITICAL: Must set this to prevent loop
      return;
    }

    if (!isRecording) {
      // Try to normalize indices when not recording
      mAudioFifo.TryNormalize();
      return;
    }

    // FIRST: Track buffer fill level BEFORE draining (so we see actual fill)
    uint8_t currentFill = mAudioFifo.FillPercent();
    if (currentFill > peakBufferFillPercent) {
      peakBufferFillPercent = currentFill;
    }

    // Check for buffer overflow (detected in audio callback)
    if (bufferOverflowed) {
      Jaffx::Firmware::instance->hardware.PrintLine("ERROR: Audio FIFO overflow!");
      Jaffx::Firmware::instance->hardware.PrintLine("  Peak fill: %d%%, Slow writes: %lu/%lu, Max write: %lu us",
          peakBufferFillPercent, slowWriteCount, totalWriteCount, maxWriteTimeUs);
      StopRecording();
      return;
    }

    // Warn if buffer is getting full (but don't stop)
    uint32_t now = System::GetNow();
    if (currentFill > 25 && (now - lastMonitorTime > MONITOR_INTERVAL_MS)) {
      Jaffx::Firmware::instance->hardware.PrintLine("WARNING: Buffer %d%% full! Slow writes: %lu/%lu",
          currentFill, slowWriteCount, totalWriteCount);
    }

    // Periodic monitoring output
    if (now - lastMonitorTime > MONITOR_INTERVAL_MS) {
      float recordingSeconds = (float)recordedSamples / (48000.0f * 2.0f);
      Jaffx::Firmware::instance->hardware.PrintLine("[" FLT_FMT3 "s] Buf:%d%% Peak:%d%% Writes:%lu Retry:%lu MaxW:%lums MaxSync:%lums",
          FLT_VAR3(recordingSeconds), currentFill, peakBufferFillPercent,
          totalWriteCount, writeRetryCount, maxWriteTimeUs / 1000, maxSyncTimeUs / 1000);
      lastMonitorTime = now;
    }

    float tempWriteBuffer[SD_WRITE_CHUNK_SAMPLES];

    // Write as many full chunks as are available.
    while (mAudioFifo.Available() >= SD_WRITE_CHUNK_SAMPLES) {
      const size_t samplesRead = mAudioFifo.Pop(tempWriteBuffer, SD_WRITE_CHUNK_SAMPLES);
      if (samplesRead == 0)
        break;

      // Time the SD write operation
      uint32_t writeStartUs = System::GetUs();

      UINT bw = 0;
      FRESULT res = FR_OK;
      const size_t expectedBytes = samplesRead * sizeof(float);
      size_t totalWritten = 0;

      // Retry logic for writes - up to 3 attempts
      for (int attempt = 0; attempt < 3; attempt++) {
        res = f_write(&wav_file, tempWriteBuffer + (totalWritten / sizeof(float)),
                      expectedBytes - totalWritten, &bw);

        totalWritten += bw;

        if (res == FR_OK && totalWritten >= expectedBytes) {
          break; // Success
        }

        if (attempt < 2) {
          // Retry after small delay
          System::DelayUs(1000); // 1ms delay before retry
          if (totalWritten < expectedBytes) {
            writeRetryCount++;
          }
        }
      }

      uint32_t writeTimeUs = System::GetUs() - writeStartUs;
      totalWriteCount++;

      // Track slow writes (>15ms is concerning)
      if (writeTimeUs > 15000) {
        slowWriteCount++;
      }
      if (writeTimeUs > maxWriteTimeUs) {
        maxWriteTimeUs = writeTimeUs;
      }

      if (res != FR_OK || totalWritten != expectedBytes) {
        Jaffx::Firmware::instance->hardware.PrintLine("SD write FAILED after retries: res=%d, wrote=%lu expected=%lu",
            res, totalWritten, expectedBytes);
        isRecording = false;
        recordingStopped = true;
        f_close(&wav_file);
        return;
      }

      recordedSamples += samplesRead;

      // Check for overflow after each write (in case sync took too long previously)
      if (bufferOverflowed) {
        return; // Will be handled at start of next call
      }
    }

    // Periodic sync - with pre-allocated file, this should be fast
    // (only metadata updates, no cluster allocation)
    if (sdSyncNeeded || (now - lastSyncTime > SYNC_INTERVAL_MS)) {

      // Small delay before sync to let SD card finish any pending operations
      System::Delay(5);

      uint32_t syncStartUs = System::GetUs();

      // Try sync with retries
      FRESULT syncRes = FR_OK;
      for (int syncAttempt = 0; syncAttempt < 3; syncAttempt++) {
        syncRes = f_sync(&wav_file);
        if (syncRes == FR_OK) {
          break;
        }
        // Wait before retry
        System::Delay(50);
        Jaffx::Firmware::instance->hardware.PrintLine("Sync retry %d after error %d", syncAttempt + 1, syncRes);
      }

      uint32_t syncTimeUs = System::GetUs() - syncStartUs;

      if (syncTimeUs > maxSyncTimeUs) {
        maxSyncTimeUs = syncTimeUs;
      }

      // Log if sync took more than 500ms (should be much faster with pre-alloc)
      if (syncTimeUs > 500000) {
        Jaffx::Firmware::instance->hardware.PrintLine("WARNING: Sync took %lu ms (expected <100ms with pre-alloc)",
            syncTimeUs / 1000);
      }

      if (syncRes != FR_OK) {
        Jaffx::Firmware::instance->hardware.PrintLine("Sync FAILED after retries: %d at pos %lu, samples %u - STOPPING",
            syncRes, (uint32_t)f_tell(&wav_file), recordedSamples);
        syncErrorCount++;
        StopRecording();
        return;
      } else {
        lastSyncTime = now;
        sdSyncNeeded = false;
      }
    }

    // Try to normalize indices if buffer is empty (prevents index growth)
    if (mAudioFifo.Available() == 0) {
      mAudioFifo.TryNormalize();
    }
  }

  void UpdateWavHeader() {

    if (!sdCardInserted) {
      return;
    }

    unsigned int dataSize = recordedSamples * sizeof(float);
    unsigned int fileSize = dataSize + sizeof(WavHeader) - 8;

    Jaffx::Firmware::instance->hardware.PrintLine("Updating header: dataSize=%u, fileSize=%u", dataSize, fileSize);

    // Seek to file size field (offset 4) and write
    FRESULT res = f_lseek(&wav_file, 4);
    if (res != FR_OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("Header seek error: %d", res);
      return;
    }

    UINT bw;
    res = f_write(&wav_file, &fileSize, 4, &bw);
    if (res != FR_OK || bw != 4) {
      Jaffx::Firmware::instance->hardware.PrintLine("Header write error (fileSize): %d, bw=%u", res, bw);
    }

    // Seek to data size field (offset 40) and write
    res = f_lseek(&wav_file, 40);
    if (res != FR_OK) {
      Jaffx::Firmware::instance->hardware.PrintLine("Header seek error (data): %d", res);
      return;
    }

    res = f_write(&wav_file, &dataSize, 4, &bw);
    if (res != FR_OK || bw != 4) {
      Jaffx::Firmware::instance->hardware.PrintLine("Header write error (dataSize): %d, bw=%u", res, bw);
    }

    // Truncate file to actual size (removes wasted pre-allocated space)
    FSIZE_t actualFileSize = sizeof(WavHeader) + dataSize;
    res = f_lseek(&wav_file, actualFileSize);
    if (res == FR_OK) {
      res = f_truncate(&wav_file);
      if (res != FR_OK) {
        Jaffx::Firmware::instance->hardware.PrintLine("Truncate error: %d", res);
      } else {
        Jaffx::Firmware::instance->hardware.PrintLine("File truncated to %lu bytes", (uint32_t)actualFileSize);
      }
    }

    // Only sync if we haven't had sync errors (avoid hanging)
    if (syncErrorCount == 0) {
      res = f_sync(&wav_file);
      if (res != FR_OK) {
        Jaffx::Firmware::instance->hardware.PrintLine("Final sync error: %d", res);
      }
    } else {
      Jaffx::Firmware::instance->hardware.PrintLine("Skipping final sync due to previous sync errors");
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
    for (auto &led : mLeds) { led.DeInit(); }
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
    StartPeriodicSDSync();  // Actually start the timer (was missing!)

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
    // If not recording, return                                  
    if (!mWavWriter.recording()) {
      return;
    }

    // copy audio data
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
    hardware.PrintLine("Rising Edge Detected"); 
  }

  inline void on_PB12_fully_risen() {
    hardware.PrintLine("SD Card Fully Removed");
    // CRITICAL: Notify writer immediately
    mWavWriter.SetSDInserted(false);

    switch (currentState) {
      case SlipRecorderState::RECORDING: {
        mWavWriter.StopRecording();
        currentState = SlipRecorderState::DEEPSLEEP;
      } 
      break;

      case SlipRecorderState::DEEPSLEEP: {} 
      break;

      default:
      break;
    }
  }

  void on_PB12_falling() { 
    hardware.PrintLine("Falling Edge Detected"); 
  }

  inline void on_PB12_fully_fallen() {
    hardware.PrintLine("SD Card Fully Inserted");
    hardware.PrintLine("Resetting to Bootloader...");
    // Update inserted status, although ResetToBootloader will effectively kill
    // this anyway
    mWavWriter.SetSDInserted(true);

    System::ResetToBootloader(daisy::System::DAISY);
    switch (currentState) {

      case SlipRecorderState::RECORDING: {
        hardware.PrintLine("Bro I'm already recording wtf");
      } break;

      case SlipRecorderState::DEEPSLEEP: {
        mWavWriter.InitSDCard();
        if (mWavWriter.sdStatus()) {
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
  }

  void on_PA2_falling() { 
    hardware.PrintLine("USB Disconnected"); 
  }

  void on_PA2_fully_fallen() { 
    hardware.PrintLine("USB Fully Disconnected"); 
  }

  inline void on_PC0_short_press() {
    hardware.PrintLine("Power Button Short-Pressed");
    switch (currentState) {
      case SlipRecorderState::RECORDING: {
        mWavWriter.StopRecording();
        currentState = SlipRecorderState::DEEPSLEEP;
      } break;

      case SlipRecorderState::DEEPSLEEP: {
        currentState = SlipRecorderState::RECORDING;
      } break;

      default: {
      }
    }
  }

  void updateClipDetectorLEDs() {
    float dB = 20.0f * log10f(RmsReport + 1e-12f);

    if (dB >= 0.0f) {
      mLeds[0].Write(true);
      mLeds[1].Write(true);
      mLeds[2].Write(true);
    } 
    else if (dB >= -6.0f) {
      mLeds[0].Write(false);
      mLeds[1].Write(true);
      mLeds[2].Write(true);
    } 
    else if (dB >= -20.0f) {
      mLeds[0].Write(false);
      mLeds[1].Write(false);
      mLeds[2].Write(true);
    } 
    else {
      mLeds[0].Write(false);
      mLeds[1].Write(false);
      mLeds[2].Write(false);
    }

    // Write recording status to Daisy Seed LED
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
      } 
      break;

      case SlipRecorderState::DEEPSLEEP: {} 
      break;

      default: 
      break;
    }
  }
};

void wakeUp() {
  switch (currentState) {
    case SlipRecorderState::RECORDING: {} 
    break;

    case SlipRecorderState::DEEPSLEEP: {
      currentState = SlipRecorderState::RECORDING;
    } 
    break;

    default: 
    break;
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

volatile uint32_t powerButtonInitiallyPressedAtTimeUs = 0;
volatile bool powerButtonPressedDown = false;
// Power Button IRQ Handler
extern "C" void EXTI0_IRQHandler(void) {
  if (EXTI->PR1 & EXTI_PR1_PR0) {
    /* Clear pending flag */
    EXTI->PR1 |= EXTI_PR1_PR0;

    /* Determine edge by reading input */
    SlipRecorder &mInstance = SlipRecorder::Instance();
    if (GPIOC->IDR & GPIO_IDR_ID0) { // Rising edge detected
      powerButtonInitiallyPressedAtTimeUs = System::GetUs();
      powerButtonPressedDown = true;
    } else { // Falling edge detected
      if (powerButtonPressedDown) {
        powerButtonPressedDown = false;
        uint32_t timeNow = System::GetUs();
        uint32_t timeElapsedUs = timeNow - powerButtonInitiallyPressedAtTimeUs;
        if (timeElapsedUs >= 4000000) {
          System::ResetToBootloader(
              System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
        } else {
          mInstance.on_PC0_short_press();
        }
      }
    }
  }
}

// Used to software debounce interrupts (using hardware timers)
enum class InterruptState : unsigned char { // inherit from unsigned char to
                                            // save space
  FALLING = 0,
  RISING = 1,
  NONE = 2
};

volatile InterruptState USB_IRQ_State = InterruptState::NONE;

// USB
extern "C" void EXTI2_IRQHandler(void) {
  if (EXTI->PR1 & EXTI_PR1_PR2) {
    EXTI->PR1 |= EXTI_PR1_PR2;
    if (TIM13->CR1 & TIM_CR1_CEN)
      return;

    if (GPIOA->IDR & GPIO_IDR_ID2) {
      SlipRecorder::Instance().on_PA2_rising();
      USB_IRQ_State = InterruptState::RISING;
    } else {
      SlipRecorder::Instance().on_PA2_falling();
      USB_IRQ_State = InterruptState::FALLING;
    }
    StartUSBDebounceTimer();
  }
}

// For the USB connection debounce
extern "C" void TIM8_UP_TIM13_IRQHandler(void) {
  Jaffx::Firmware::instance->hardware.PrintLine("TIM13 IRQ Handler Triggered");
  if (TIM13->SR & TIM_SR_UIF) {
    TIM13->SR &= ~TIM_SR_UIF;

    if (USB_IRQ_State == InterruptState::NONE)
      return;

    bool currentState = (GPIOA->IDR & GPIO_IDR_ID2) != 0;

    if (USB_IRQ_State == InterruptState::RISING && currentState) {
      SlipRecorder::Instance().on_PA2_fully_risen();
    } else if (USB_IRQ_State == InterruptState::FALLING && !currentState) {
      SlipRecorder::Instance().on_PA2_fully_fallen();
    } else {
      Jaffx::Firmware::instance->hardware.PrintLine("USB: Not a valid bounce");
    }
    USB_IRQ_State = InterruptState::NONE;
    TIM13->CR1 &= ~TIM_CR1_CEN;
  }
}

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
      // CRITICAL: Do NOT call PrintLine from ISR - just set the flag
      SlipRecorder::Instance().mWavWriter.sdSyncNeeded = true;
    }
  }
}

int main() {
  SlipRecorder::Instance().start();
  // EXTIptr = mSlipRecorder::IRQHandler
  return 0;
}