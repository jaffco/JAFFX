# CLAUDE.md - JAFFX Repository Analysis

## Repository Overview

JAFFX is a free, open-source framework for programming the JAFFX reprogrammable audio effects pedal and other projects using the [Daisy Seed](https://electro-smith.com/) microcontroller platform. The repository centers around a single header file (`Jaffx.hpp`) that wraps the libDaisy hardware abstraction library and provides additional utilities.

### Core Structure

```
JAFFX/
├── Jaffx.hpp              # Main header - wraps libDaisy, provides base Firmware class
├── include/
│   └── SDRAM.hpp          # Custom SDRAM memory allocator (64MB external RAM)
├── common.mk              # Shared Makefile configuration
├── examples/              # Example projects demonstrating various features
├── Gimmel/                # Submodule: DSP effects library
├── libDaisy/              # Submodule: Hardware abstraction for Daisy Seed
├── RTNeural/              # Submodule: Real-time neural network inference
├── projectGen.py          # Project generator script
├── run.sh                 # Build and flash script
└── init.sh                # Repository initialization script
```

### Submodules

1. **libDaisy** (v7.1.0-based, jaffco fork)
   - Hardware abstraction library for Daisy Seed (STM32H750)
   - Provides: Audio I/O, SD card via FatFS, GPIO, I2C, SPI, UART, USB
   - Location: `libDaisy/` (branch: v7.1.0-30-g577ebd5)

2. **Gimmel** (jaffco/Gimmel, branch: SlipRecorder)
   - Lightweight C++ audio effects library
   - Provides: CircularBuffer, Biquad filters, Delay, Reverb, Compressor, etc.
   - Key file: `Gimmel/include/utility.hpp` - Contains CircularBuffer used by SlipRecorder
   - Location: `Gimmel/`

3. **RTNeural** (joeljaffesd/RTNeural, branch: dev)
   - Real-time neural network inference library
   - Uses Eigen for matrix operations
   - Location: `RTNeural/`

### Build System

- Uses ARM GCC toolchain for STM32H750
- Build configuration in `common.mk`:
  - C++ Standard: `gnu++14`
  - Optimization: `-Ofast`
  - Boot binary: 10ms version (`dsy_bootloader_v6_3-intdfu-10ms.bin`)
  - App type: `BOOT_SRAM`

### Key Configuration Values

- **Sample Rate**: 48kHz (hardcoded in `Jaffx.hpp:34`)
- **Buffer Size**: 256 samples (hardcoded in `Jaffx.hpp:35`)
- **SDRAM Size**: 64MB at address `0xC0000000`
- **SD Block Size**: 512 bytes (from HAL)

---

## examples/slipRecorder - Detailed Analysis

SlipRecorder is an audio recorder that writes stereo float32 WAV files to an SD card. It uses interrupt-driven detection for SD card insertion/removal and USB connection, with hardware debouncing via timers.

### Architecture

```
Audio Callback (IRQ)           Main Loop
       │                           │
       ▼                           ▼
  WriteAudioBlock()         ProcessBackgroundWrite()
       │                           │
       ▼                           ▼
  CircularFifo.Push()        CircularFifo.Pop()
       │                           │
       └──────── SDRAM ────────────┘
                  │
                  ▼
            f_write() → SD Card
```

### Key Components

#### 1. Memory Layout
- **DMA Audio Buffer**: `float dmaAudioBuffer[2][256]` in `.sram1_bss` (non-cached for DMA)
- **Ring Buffer**: 200,000 floats (~800KB) allocated in SDRAM via custom allocator
- **SD/FatFS Handlers**: Placed in `.sram1_bss` for DMA compatibility

#### 2. CircularFifo Class (`slipRecorder.cpp:35-126`)
Extends `giml::CircularBuffer<T>` with atomic head/tail pointers for lock-free SPSC queue:
- **Producer**: Audio callback calls `Push()` with interleaved stereo samples
- **Consumer**: Main loop calls `Pop()` to get data for SD writes
- **Capacity**: 200,000 floats = ~2 seconds of 48kHz stereo at production rate

#### 3. SDCardWavWriter Class (`slipRecorder.cpp:133-425`)
Handles WAV file creation and streaming writes:
- **Chunk Size**: 2048 floats (8192 bytes) per SD write
- **Format**: 32-bit float, 48kHz, stereo
- **Sync Interval**: Every 5 seconds (via `lastSyncTime` check)

#### 4. Interrupt System (`Interrupts.hpp`)
| Timer/EXTI | Purpose | Notes |
|------------|---------|-------|
| EXTI12 (PB12) | SD card detect | 50ms debounce via TIM14 |
| EXTI2 (PA2) | USB detect | 50ms debounce via TIM13 |
| EXTI0 (PC0) | Power button | Short press toggles state, 4s hold → bootloader |
| TIM16 | Recording LED pulse | 1.5s period |
| TIM17 | SD sync timer | **Initialized but never started** |

### Data Flow Timing Analysis

At 48kHz stereo:
- **Production rate**: 48,000 × 2 = 96,000 floats/second = 384 KB/s
- **Ring buffer capacity**: 200,000 floats = 800 KB
- **Time to fill buffer**: ~2.08 seconds if writes stall
- **SD write chunk**: 2048 floats = 8192 bytes
- **Required write frequency**: 96,000 / 2048 = ~47 writes/second
- **Max latency per write**: ~21ms before backlog accumulates

---

## Potential Causes of ~30 Second Recording Failure

### Ranked by Likelihood (High to Low)

#### 1. **SD Card Write Latency Accumulation** (HIGH)
**Evidence**: Recording works for ~30 seconds then fails, likely with "Buffer overflow detected" message.

**Root Cause**: SD cards have non-deterministic write latency. While average writes may complete in <21ms, periodic "housekeeping" operations (wear leveling, garbage collection) can cause multi-hundred-millisecond stalls.

**Analysis**:
- Ring buffer = 2.08 seconds capacity
- If SD stalls for >2s, buffer overflows
- Stalls may accumulate: 10ms extra latency × 1000 writes = 10 seconds lost capacity
- After ~30 seconds, accumulated stalls exceed buffer headroom

**Fix Suggestions**:
- Increase `RING_BUFFER_SIZE` from 200,000 to 500,000+ floats
- Use faster SD card (Class 10 / UHS-I minimum)
- Reduce `SD_WRITE_CHUNK_SAMPLES` to 4096 for better batching (16KB aligned to SD page size)

#### 2. **PrintLine Call in Audio Callback Context** (HIGH)
**Location**: `slipRecorder.cpp:350`
```cpp
void WriteAudioBlock(...) {
    // ...
    if (written != interleavedCount) {
        bufferOverflowed = true;
        Jaffx::Firmware::instance->hardware.PrintLine("Buffer overflow detected...");  // DANGEROUS
    }
}
```

**Problem**: `WriteAudioBlock()` is called from the audio callback (`CustomAudioBlockCallback`). Calling `PrintLine()` (which uses USB serial and blocking I/O) from an interrupt context can:
- Block the audio callback
- Cause timing violations
- Lead to cascading buffer overflows
- Create a deadlock if USB buffer is full

**Fix**: Remove the PrintLine or set a flag and print from main loop only.

#### 3. **Head/Tail Index Accumulation** (MEDIUM)
**Location**: `slipRecorder.cpp:123-125`
```cpp
std::atomic<size_t> head_{0};
std::atomic<size_t> tail_{0};
```

**Problem**: These indices increment indefinitely and never wrap:
```cpp
head_.store(current_head + count, ...);  // Line 91
tail_.store(current_tail + count, ...);  // Line 119
```

At 96k floats/second, after 30 seconds: head ≈ 2.88 million

While the modulo operation (`current_head % this->size()`) should handle this, potential issues:
- Integer overflow after ~22,000 seconds (size_t is 32-bit on ARM)
- Subtle bugs in `Available()` calculation when head wraps but tail doesn't
- Cache line bouncing between head/tail atomics

**Fix**: Periodically normalize head/tail when consumer catches up, or use power-of-2 buffer sizes with masking.

#### 4. **Sync Timer Never Started** (MEDIUM)
**Location**: `Interrupts.hpp:178-203`

**Problem**: `TIM17_Init()` configures the timer but doesn't start it. `StartPeriodicSDSync()` would start it but is never called.

```cpp
inline void EnableSDSyncTimer(void) {
    TIM17_Init();  // Configures timer but doesn't start it
    // Missing: StartPeriodicSDSync();
}
```

**Impact**: The `sdSyncNeeded` flag is never set by the timer ISR. However, the code has a fallback time-based sync check (`System::GetNow() - lastSyncTime > 5000`), so this may not be the primary cause.

**Fix**: Call `StartPeriodicSDSync()` after `EnableSDSyncTimer()` in `init()`.

#### 5. **f_sync() Blocking During Header Updates** (MEDIUM)
**Location**: `slipRecorder.cpp:406-424`

**Problem**: `UpdateWavHeader()` performs multiple operations:
1. Seeks to position 4
2. Writes 4 bytes
3. Seeks to position 40
4. Writes 4 bytes
5. Seeks back to original position
6. Calls `f_sync()`

This is called every 5 seconds and during stop. Each operation can block, and if called while the ring buffer is filling, can cause overflow.

**Fix**: Only update header when recording stops, or use a separate background task.

#### 6. **SDRAM Allocator Fragmentation** (LOW)
**Location**: `include/SDRAM.hpp`

**Problem**: Custom malloc/free implementation may fragment over time. If `giml::malloc` is called (used by CircularBuffer base class), memory could be exhausted.

**Evidence**: Single 800KB allocation at startup should be fine, but if any dynamic allocation happens during recording, fragmentation could be an issue.

#### 7. **USB Serial Buffer Blocking** (LOW)
**Problem**: If USB serial output buffer fills up (no host reading), PrintLine calls block indefinitely.

**Evidence**: Multiple debug print statements throughout the code could accumulate.

---

## Code Quality Issues

### Critical
1. **PrintLine in interrupt context** - Never call blocking I/O from callbacks
2. **Timer initialized but not started** - TIM17 setup is incomplete

### Warnings
1. **Hardcoded paths in analyze_audio.py** - `wav_file = 'rec_00026.wav'`
2. **Unused `sdSyncNeeded` flag** - Set by timer ISR but never read
3. **Magic numbers** - Buffer sizes, timing values should be constants with explanations

### Suggestions
1. Add `volatile` to `isRecording`, `recordedSamples` where accessed from both contexts
2. Use `__DSB()` after writing to atomics that affect DMA
3. Consider double-buffering the SD write buffer instead of ring buffer

---

## Building and Flashing

```bash
# Initialize repository (first time only)
./init.sh

# Build and flash a project
./run.sh examples/slipRecorder/slipRecorder.cpp

# Or from VSCode: Shift+Cmd+B with source file open
```

### Serial Monitoring
- USB CDC serial at 115200 baud
- VSCode: Use Microsoft Serial Monitor extension
- macOS: `screen /dev/tty.usbmodem* 115200`

---

## Debug Strategy for SlipRecorder

1. **Add ring buffer fill level monitoring**:
   ```cpp
   // In loop(), print buffer usage periodically
   if (System::GetNow() % 1000 == 0) {
       hardware.PrintLine("Buffer: %d / %d", mAudioFifo.Available(), RING_BUFFER_SIZE);
   }
   ```

2. **Track SD write timing**:
   ```cpp
   uint32_t writeStart = System::GetUs();
   f_write(&wav_file, ...);
   uint32_t writeTime = System::GetUs() - writeStart;
   if (writeTime > 15000) { // >15ms
       hardware.PrintLine("Slow SD write: %lu us", writeTime);
   }
   ```

3. **Remove/guard debug prints in callbacks**:
   ```cpp
   // In WriteAudioBlock - remove PrintLine or use flag
   if (written != interleavedCount) {
       bufferOverflowed = true;
       // DO NOT PrintLine here!
   }
   ```

4. **Test with larger ring buffer**:
   ```cpp
   static constexpr size_t RING_BUFFER_SIZE = 500000;  // 2MB, ~5 seconds
   ```

---

## Hardware Pin Assignments (SlipRecorder)

| Function | Pin | Notes |
|----------|-----|-------|
| LED 0 (clip) | D21 | Level meter |
| LED 1 (clip) | D20 | Level meter |
| LED 2 (clip) | D19 | Level meter |
| Power LED | D22 | On when running |
| Recording LED | PA4 | Pulsing via TIM16 |
| SD Card Detect | PB12 | Directly wired |
| USB Detect | PA2 | VBUS sense |
| Power Button | PC0 | Short/long press |
| SDMMC1 CLK | PC12 | Fixed by libDaisy |
| SDMMC1 CMD | PD2 | Fixed by libDaisy |
| SDMMC1 D0-D3 | PC8-11 | Fixed by libDaisy |
