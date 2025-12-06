#ifndef SDCARD_HPP
#define SDCARD_HPP

#include "fatfs.h"
#include "diskio.h"

// // TODO: Move these to the dedicated DMA section exposed in daisy_core.h
// // Global SD resources (hardware-required placement in AXI SRAM for DMA)
// SdmmcHandler global_sdmmc_handler __attribute__((section(".sram1_bss")));
// FatFSInterface global_fsi_handler __attribute__((section(".sram1_bss")));
// FIL global_wav_file __attribute__((section(".sram1_bss")));



#endif // SDCARD_HPP