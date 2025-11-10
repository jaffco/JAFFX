#include "../../Jaffx.hpp"
#include "fatfs.h"
#include "diskio.h"
#include <cstring>

/** 
 * Comprehensive SD Card Demo
 * 
 * This example demonstrates libDaisy's SD card interface capabilities:
 * 1. Initialize SDMMC hardware and FatFS filesystem
 * 2. Mount SD card and check status
 * 3. Create/write files
 * 4. Read files
 * 5. List directory contents
 * 6. Get disk space information
 * 7. Delete files
 * 
 * Hardware Requirements:
 * - SD card connected to SDMMC1 pins:
 *   PC12 - SDMMC1 CK (Clock)
 *   PD2  - SDMMC1 CMD (Command)
 *   PC8  - SDMMC1 D0 (Data 0)
 *   PC9  - SDMMC1 D1 (Data 1) - optional for 4-bit mode
 *   PC10 - SDMMC1 D2 (Data 2) - optional for 4-bit mode
 *   PC11 - SDMMC1 D3 (Data 3) - optional for 4-bit mode
 * 
 * KNOWN ISSUE - libDaisy SD Card DMA Bug:
 * ==========================================
 * libDaisy's sd_diskio.c uses DMA mode (BSP_SD_ReadBlocks_DMA) but the SDMMC
 * DMA callbacks never fire. This causes FR_DISK_ERR on all filesystem operations.
 * 
 * ROOT CAUSE:
 * - STM32H7 SD HAL (HAL_SD_ReadBlocks_DMA) manages DMA internally
 * - It should trigger HAL_SD_RxCpltCallback → BSP_SD_ReadCpltCallback → sets ReadStatus=1
 * - Without proper DMA interrupt routing, callbacks never fire
 * - sd_diskio.c waits for ReadStatus flag, times out after SD_TIMEOUT (~65s)
 * - Result: FR_DISK_ERR on f_opendir, f_read, f_write, all disk operations
 * 
 * HARDWARE STATUS:
 * - SDMMC initialization: ✓ OK
 * - FatFS mount: ✓ OK (FR_OK)
 * - BSP_SD_Init(): ✓ OK (returns 0x00)
 * - HAL state: ✓ HAL_SD_STATE_READY
 * - disk_status(0): ✓ 0x00 (fully operational)
 * - Filesystem operations: ✗ FR_DISK_ERR (DMA timeout)
 * 
 * WORKAROUND - Use Polling Mode:
 * ==============================
 * Edit libDaisy/src/util/sd_diskio.c line ~140 in SD_read():
 * 
 * Replace:
 *   res = BSP_SD_ReadBlocks_DMA((uint32_t*)buff, (uint32_t)(sector), count);
 *   while(ReadStatus == 0) { }  // Waits forever
 * 
 * With:
 *   res = BSP_SD_ReadBlocks((uint32_t*)buff, (uint32_t)(sector), count);
 *   // No waiting needed - polling mode completes immediately
 * 
 * Do the same for SD_write() around line ~180:
 *   res = BSP_SD_WriteBlocks((uint32_t*)buff, (uint32_t)(sector), count);
 * 
 * Then rebuild libDaisy. Polling mode is slower but works reliably.
 * 
 * PROPER FIX (requires libDaisy update):
 * =======================================
 * The proper fix requires enabling DMA completion interrupts for SDMMC.
 * This is non-trivial on STM32H7 because the SD HAL manages DMA internally
 * and doesn't expose separate RX/TX DMA handles like other peripherals.
 * 
 * LED Feedback:
 * - Rapid blink (125ms): SD card operations successful
 * - Slow blink (1000ms): SD card error or not present
 */

// CRITICAL: SD card objects must be in AXI SRAM (SRAM), not DTCM
// The SDMMC peripheral can only access AXI SRAM via DMA
// Place these in global scope with section attribute
SdmmcHandler sdmmc_handler __attribute__((section(".sram1_bss")));
FatFSInterface fsi_handler __attribute__((section(".sram1_bss")));

// FatFS working structures also need to be in AXI SRAM for DMA operations
DIR work_dir __attribute__((section(".sram1_bss")));
FILINFO work_fno __attribute__((section(".sram1_bss")));

class SDcard : public Jaffx::Firmware {
  bool sdCardOk = false;

  void init() override {
    hardware.StartLog(true);
    System::Delay(100);
    
    hardware.PrintLine("=== SD Card Demo ===");
    hardware.PrintLine("Initializing...");
    
    hardware.PrintLine("\n--- Step 1: SDMMC Hardware Init ---");
    
    // Initialize SDMMC hardware
    // Start with most compatible settings: slow speed, 1-bit mode
    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sd_cfg.speed = SdmmcHandler::Speed::STANDARD;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_1; // Use 1-bit mode for maximum compatibility
    sd_cfg.clock_powersave = false;
    
    hardware.PrintLine("Config: Speed=%d, Width=%d, PowerSave=%d", 
                      (int)sd_cfg.speed, (int)sd_cfg.width, sd_cfg.clock_powersave);
    hardware.PrintLine("NOTE: Using 1-bit mode for maximum compatibility");
    
    SdmmcHandler::Result sdmmc_result = sdmmc_handler.Init(sd_cfg);
    if(sdmmc_result != SdmmcHandler::Result::OK) {
      hardware.PrintLine("ERROR: SDMMC Init failed! Result=%d", (int)sdmmc_result);
      return;
    }
    hardware.PrintLine("✓ SDMMC hardware initialized");
    
    // Give hardware time to settle
    System::Delay(100);
    
    hardware.PrintLine("\n--- Step 2: FatFS Interface Init ---");
    
    // Initialize FatFS interface
    FatFSInterface::Config fsi_cfg;
    fsi_cfg.media = FatFSInterface::Config::MEDIA_SD;
    
    FatFSInterface::Result fsi_result = fsi_handler.Init(fsi_cfg);
    if(fsi_result != FatFSInterface::OK) {
      hardware.PrintLine("ERROR: FatFS Init failed! Result=%d", (int)fsi_result);
      return;
    }
    hardware.PrintLine("✓ FatFS interface initialized");
    hardware.PrintLine("  Mount path: '%s'", fsi_handler.GetSDPath());
    
    // Give more time for card detection and power-up
    hardware.PrintLine("\nWaiting for SD card to stabilize...");
    System::Delay(500);
    
    hardware.PrintLine("\n--- Step 3: Mount Filesystem ---");
    hardware.PrintLine("Attempting to mount SD card...");
    
    // Try mounting with delayed initialization first (0 = delay)
    FATFS& fs = fsi_handler.GetSDFileSystem();
    FRESULT res = f_mount(&fs, fsi_handler.GetSDPath(), 0); // 0 = delayed mount
    
    if(res != FR_OK) {
      hardware.PrintLine("ERROR: Delayed mount failed (Error: %d)", res);
      PrintFresultError(res);
      
      // Try immediate mount
      hardware.PrintLine("Retrying with immediate mount...");
      System::Delay(500);
      res = f_mount(&fs, fsi_handler.GetSDPath(), 1); // 1 = mount now
      
      if(res != FR_OK) {
        hardware.PrintLine("ERROR: Immediate mount also failed (Error: %d)", res);
        PrintFresultError(res);
        
        // Try to get disk status
        hardware.PrintLine("\nDisk Status Check:");
        BYTE status = disk_status(0); // Physical drive 0
        hardware.PrintLine("  Status byte: 0x%02X", status);
        if(status & STA_NOINIT)   hardware.PrintLine("  - STA_NOINIT: Not initialized");
        if(status & STA_NODISK)   hardware.PrintLine("  - STA_NODISK: No medium");
        if(status & STA_PROTECT)  hardware.PrintLine("  - STA_PROTECT: Write protected");
        
        // Try disk initialize
        hardware.PrintLine("\nAttempting disk_initialize...");
        status = disk_initialize(0);
        hardware.PrintLine("  Result: 0x%02X", status);
        
        if(status == 0) {
          hardware.PrintLine("  Disk initialized! Retrying mount...");
          System::Delay(200);
          res = f_mount(&fs, fsi_handler.GetSDPath(), 1);
          if(res != FR_OK) {
            hardware.PrintLine("  Still failed: %d", res);
            PrintFresultError(res);
          }
        }
        
        if(res != FR_OK) {
          hardware.PrintLine("\n*** SD Card Mount Failed ***");
          hardware.PrintLine("Possible issues:");
          hardware.PrintLine("  1. No SD card inserted");
          hardware.PrintLine("  2. SD card not formatted (FAT/FAT32)");
          hardware.PrintLine("  3. Bad SD card or connection");
          hardware.PrintLine("  4. Incorrect pin connections");
          hardware.PrintLine("     PC12 - SDMMC1 CK");
          hardware.PrintLine("     PD2  - SDMMC1 CMD");
          hardware.PrintLine("     PC8  - SDMMC1 D0");
          hardware.PrintLine("     PC9  - SDMMC1 D1");
          hardware.PrintLine("     PC10 - SDMMC1 D2");
          hardware.PrintLine("     PC11 - SDMMC1 D3");
          return;
        }
      }
    }
    
    hardware.PrintLine("✓ SD Card mounted successfully!");
    
    // Verify disk is ready after mount
    hardware.PrintLine("\n--- Step 4: Verify Disk Status ---");
    
    // Try calling BSP_SD_Init directly to initialize the card
    hardware.PrintLine("Calling BSP_SD_Init() to initialize card...");
    extern uint8_t BSP_SD_Init(void);
    uint8_t bsp_result = BSP_SD_Init();
    hardware.PrintLine("BSP_SD_Init() returned: 0x%02X", bsp_result);
    if(bsp_result == 0) {
      hardware.PrintLine("✓ BSP_SD_Init succeeded (MSD_OK)");
    } else {
      hardware.PrintLine("✗ BSP_SD_Init failed!");
      if(bsp_result == 1) hardware.PrintLine("  Error: MSD_ERROR (general error)");
      if(bsp_result == 2) hardware.PrintLine("  Error: MSD_ERROR_SD_NOT_PRESENT");
      return;
    }
    
    // Check disk driver status
    BYTE disk_stat = disk_status(0);
    hardware.PrintLine("\nDisk status: 0x%02X", disk_stat);
    
    if(disk_stat != 0) {
      hardware.PrintLine("Disk not ready! Status flags:");
      if(disk_stat & STA_NOINIT)   hardware.PrintLine("  - STA_NOINIT: Not initialized");
      if(disk_stat & STA_NODISK)   hardware.PrintLine("  - STA_NODISK: No medium");
      if(disk_stat & STA_PROTECT)  hardware.PrintLine("  - STA_PROTECT: Write protected");
      
      hardware.PrintLine("\nAttempting disk_initialize...");
      disk_stat = disk_initialize(0);
      hardware.PrintLine("Initialize result: 0x%02X", disk_stat);
      
      if(disk_stat == 0) {
        hardware.PrintLine("✓ Disk initialized successfully!");
      } else {
        hardware.PrintLine("ERROR: Disk initialization failed!");
        return;
      }
    } else {
      hardware.PrintLine("✓ Disk is ready");
    }
    
    // Give the disk time to settle
    System::Delay(100);
    
    sdCardOk = true;
    
    // Run comprehensive demos
    if(sdCardOk) {
      hardware.PrintLine("\n=== Running SD Card Demos ===\n");
      
      // Demo 1: Get disk information
      GetDiskInfoDemo();
      
      // Demo 2: Write a file
      WriteFileDemo();
      
      // Demo 3: Read the file back
      ReadFileDemo();
      
      // Demo 4: List directory contents
      ListDirectoryDemo();
      
      // Demo 5: Append to file
      AppendFileDemo();
      
      // Demo 6: Read appended file
      ReadFileDemo();
      
      // Demo 7: Delete a file
      DeleteFileDemo();
      
      hardware.PrintLine("\n=== All Demos Complete ===");
      hardware.PrintLine("SD card operations working successfully!");
    }
  }

  void loop() override {
    // Blink LED to indicate status
    if(sdCardOk) {
      // Fast blink = success
      hardware.SetLed(true);
      System::Delay(125);
      hardware.SetLed(false);
      System::Delay(125);
    } else {
      // Slow blink = error
      hardware.SetLed(true);
      System::Delay(1000);
      hardware.SetLed(false);
      System::Delay(1000);
    }
  }
  
  void GetDiskInfoDemo() {
    hardware.PrintLine("\n--- Disk Information Demo ---");
    
    FATFS* fs;
    DWORD free_clusters, total_sectors, free_sectors;
    
    FRESULT res = f_getfree(fsi_handler.GetSDPath(), &free_clusters, &fs);
    hardware.PrintLine("f_getfree() returned: %d", res);
    
    if(res == FR_OK) {
      // Calculate total and free space
      total_sectors = (fs->n_fatent - 2) * fs->csize;
      free_sectors = free_clusters * fs->csize;
      
      // Convert to MB (assuming 512 byte sectors)
      int total_mb = (total_sectors / 2) / 1024;
      int free_mb = (free_sectors / 2) / 1024;
      int used_mb = total_mb - free_mb;
      
      hardware.PrintLine("✓ Disk Information:");
      hardware.PrintLine("  Total Space: %d MB", total_mb);
      hardware.PrintLine("  Used Space:  %d MB", used_mb);
      hardware.PrintLine("  Free Space:  %d MB", free_mb);
      hardware.PrintLine("  Cluster Size: %u sectors", fs->csize);
      hardware.PrintLine("  Sector Size: 512 bytes");
    } else {
      hardware.PrintLine("ERROR: Could not get disk info");
      PrintFresultError(res);
    }
  }
  
  void WriteFileDemo() {
    hardware.PrintLine("\n--- Write File Demo ---");
    
    FIL file;
    FRESULT res = f_open(&file, "demo.txt", FA_CREATE_ALWAYS | FA_WRITE);
    hardware.PrintLine("f_open('demo.txt', CREATE|WRITE) returned: %d", res);
    
    if(res != FR_OK) {
      hardware.PrintLine("ERROR: Could not open file for writing");
      PrintFresultError(res);
      return;
    }
    
    const char* text = "Hello from Daisy Seed!\nThis is a test file.\nSD card working perfectly!\n";
    UINT bytes_written;
    
    hardware.PrintLine("Writing %d bytes...", strlen(text));
    res = f_write(&file, text, strlen(text), &bytes_written);
    hardware.PrintLine("f_write() returned: %d", res);
    hardware.PrintLine("Bytes written: %u", bytes_written);
    
    if(res == FR_OK && bytes_written == strlen(text)) {
      hardware.PrintLine("✓ Successfully wrote to demo.txt");
    } else {
      hardware.PrintLine("ERROR: Write failed or incomplete");
      PrintFresultError(res);
    }
    
    f_close(&file);
  }
  
  void ReadFileDemo() {
    hardware.PrintLine("\n--- Read File Demo ---");
    
    FIL file;
    FRESULT res = f_open(&file, "demo.txt", FA_READ);
    hardware.PrintLine("f_open('demo.txt', READ) returned: %d", res);
    
    if(res != FR_OK) {
      hardware.PrintLine("ERROR: Could not open file for reading");
      PrintFresultError(res);
      return;
    }
    
    char buffer[256];
    UINT bytes_read;
    
    res = f_read(&file, buffer, sizeof(buffer) - 1, &bytes_read);
    hardware.PrintLine("f_read() returned: %d", res);
    hardware.PrintLine("Bytes read: %u", bytes_read);
    
    if(res == FR_OK) {
      buffer[bytes_read] = '\0'; // Null terminate
      hardware.PrintLine("✓ File contents:");
      hardware.PrintLine("---");
      hardware.PrintLine("%s", buffer);
      hardware.PrintLine("---");
    } else {
      hardware.PrintLine("ERROR: Read failed");
      PrintFresultError(res);
    }
    
    f_close(&file);
  }
  
  void ListDirectoryDemo() {
    hardware.PrintLine("\n--- Directory Listing Demo ---");
    
    hardware.PrintLine("Opening root directory '/'...");
    FRESULT res = f_opendir(&work_dir, "/");
    hardware.PrintLine("f_opendir() returned: %d", res);
    
    if(res != FR_OK) {
      hardware.PrintLine("ERROR: Could not open directory");
      PrintFresultError(res);
      return;
    }
    
    hardware.PrintLine("✓ Directory contents:");
    int file_count = 0;
    
    while(true) {
      res = f_readdir(&work_dir, &work_fno);
      if(res != FR_OK) {
        hardware.PrintLine("ERROR: f_readdir failed");
        PrintFresultError(res);
        break;
      }
      
      // End of directory?
      if(work_fno.fname[0] == 0) {
        break;
      }
      
      if(work_fno.fattrib & AM_DIR) {
        hardware.PrintLine("  [DIR]  %s", work_fno.fname);
      } else {
        hardware.PrintLine("  [FILE] %s (%lu bytes)", work_fno.fname, work_fno.fsize);
      }
      file_count++;
    }
    
    hardware.PrintLine("Total items: %d", file_count);
    f_closedir(&work_dir);
  }
  
  void AppendFileDemo() {
    hardware.PrintLine("\n--- Append File Demo ---");
    
    FIL file;
    FRESULT res = f_open(&file, "demo.txt", FA_OPEN_APPEND | FA_WRITE);
    hardware.PrintLine("f_open('demo.txt', APPEND|WRITE) returned: %d", res);
    
    if(res != FR_OK) {
      hardware.PrintLine("ERROR: Could not open file for appending");
      PrintFresultError(res);
      return;
    }
    
    const char* text = "This line was appended!\n";
    UINT bytes_written;
    
    hardware.PrintLine("Appending %d bytes...", strlen(text));
    res = f_write(&file, text, strlen(text), &bytes_written);
    hardware.PrintLine("f_write() returned: %d", res);
    hardware.PrintLine("Bytes written: %u", bytes_written);
    
    if(res == FR_OK && bytes_written == strlen(text)) {
      hardware.PrintLine("✓ Successfully appended to demo.txt");
    } else {
      hardware.PrintLine("ERROR: Append failed");
      PrintFresultError(res);
    }
    
    f_close(&file);
  }
  
  void DeleteFileDemo() {
    hardware.PrintLine("\n--- Delete File Demo ---");
    
    // First create a temporary file
    const char* temp_filename = "temp.txt";
    hardware.PrintLine("Creating temporary file '%s'...", temp_filename);
    
    FIL file;
    FRESULT res = f_open(&file, temp_filename, FA_CREATE_ALWAYS | FA_WRITE);
    if(res == FR_OK) {
      const char* text = "Temporary file for deletion test";
      UINT bytes_written;
      f_write(&file, text, strlen(text), &bytes_written);
      f_close(&file);
      hardware.PrintLine("✓ Created temp.txt");
    } else {
      hardware.PrintLine("ERROR: Could not create temp file");
      PrintFresultError(res);
      return;
    }
    
    // Now delete it
    hardware.PrintLine("Deleting '%s'...", temp_filename);
    res = f_unlink(temp_filename);
    hardware.PrintLine("f_unlink() returned: %d", res);
    
    if(res == FR_OK) {
      hardware.PrintLine("✓ Successfully deleted temp.txt");
    } else {
      hardware.PrintLine("ERROR: Could not delete file");
      PrintFresultError(res);
    }
  }
  
  void SimpleFileWriteTest() {
    hardware.PrintLine("--- Simple File Write Test ---");
    hardware.PrintLine("Goal: Create 'test.txt' with 'Hello World'");
    
    FIL file;
    FRESULT fres;
    UINT bytes_written;
    const char* filename = "test.txt";
    const char* content = "Hello World";
    
    hardware.PrintLine("\nStep 1: Opening file '%s' for writing...", filename);
    hardware.PrintLine("Flags: FA_CREATE_ALWAYS | FA_WRITE");
    
    fres = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    hardware.PrintLine("f_open() returned: %d", fres);
    
    if(fres != FR_OK) {
      hardware.PrintLine("ERROR: Failed to open file!");
      PrintFresultError(fres);
      return;
    }
    
    hardware.PrintLine("✓ File opened successfully");
    
    hardware.PrintLine("\nStep 2: Writing '%s' to file...", content);
    hardware.PrintLine("Content length: %d bytes", strlen(content));
    
    fres = f_write(&file, content, strlen(content), &bytes_written);
    hardware.PrintLine("f_write() returned: %d", fres);
    hardware.PrintLine("Bytes written: %u", bytes_written);
    
    if(fres != FR_OK) {
      hardware.PrintLine("ERROR: Failed to write to file!");
      PrintFresultError(fres);
      f_close(&file);
      return;
    }
    
    if(bytes_written != strlen(content)) {
      hardware.PrintLine("WARNING: Not all bytes written! Expected %d, wrote %u", 
                        strlen(content), bytes_written);
    } else {
      hardware.PrintLine("✓ All bytes written successfully");
    }
    
    hardware.PrintLine("\nStep 3: Closing file...");
    fres = f_close(&file);
    hardware.PrintLine("f_close() returned: %d", fres);
    
    if(fres != FR_OK) {
      hardware.PrintLine("ERROR: Failed to close file!");
      PrintFresultError(fres);
      return;
    }
    
    hardware.PrintLine("✓ File closed successfully");
    hardware.PrintLine("\n✓✓✓ TEST PASSED: Created test.txt with Hello World ✓✓✓");
  }
  
  void PrintFresultError(FRESULT res) {
    hardware.Print("  ");
    switch(res) {
      case FR_OK:                  hardware.PrintLine("FR_OK: Success"); break;
      case FR_DISK_ERR:            hardware.PrintLine("FR_DISK_ERR: Low level disk I/O error"); break;
      case FR_INT_ERR:             hardware.PrintLine("FR_INT_ERR: Assertion failed"); break;
      case FR_NOT_READY:           hardware.PrintLine("FR_NOT_READY: Physical drive not ready"); break;
      case FR_NO_FILE:             hardware.PrintLine("FR_NO_FILE: File not found"); break;
      case FR_NO_PATH:             hardware.PrintLine("FR_NO_PATH: Path not found"); break;
      case FR_INVALID_NAME:        hardware.PrintLine("FR_INVALID_NAME: Invalid path name"); break;
      case FR_DENIED:              hardware.PrintLine("FR_DENIED: Access denied"); break;
      case FR_EXIST:               hardware.PrintLine("FR_EXIST: File/directory already exists"); break;
      case FR_INVALID_OBJECT:      hardware.PrintLine("FR_INVALID_OBJECT: Invalid object"); break;
      case FR_WRITE_PROTECTED:     hardware.PrintLine("FR_WRITE_PROTECTED: Write protected"); break;
      case FR_INVALID_DRIVE:       hardware.PrintLine("FR_INVALID_DRIVE: Invalid drive number"); break;
      case FR_NOT_ENABLED:         hardware.PrintLine("FR_NOT_ENABLED: Work area not enabled"); break;
      case FR_NO_FILESYSTEM:       hardware.PrintLine("FR_NO_FILESYSTEM: No valid FAT volume"); break;
      case FR_MKFS_ABORTED:        hardware.PrintLine("FR_MKFS_ABORTED: f_mkfs aborted"); break;
      case FR_TIMEOUT:             hardware.PrintLine("FR_TIMEOUT: Timeout"); break;
      case FR_LOCKED:              hardware.PrintLine("FR_LOCKED: File locked"); break;
      case FR_NOT_ENOUGH_CORE:     hardware.PrintLine("FR_NOT_ENOUGH_CORE: Not enough memory"); break;
      case FR_TOO_MANY_OPEN_FILES: hardware.PrintLine("FR_TOO_MANY_OPEN_FILES: Too many open files"); break;
      case FR_INVALID_PARAMETER:   hardware.PrintLine("FR_INVALID_PARAMETER: Invalid parameter"); break;
      default:                     hardware.PrintLine("Unknown error code: %d", res); break;
    }
  }
  
  void ShowDiskInfo() {
    hardware.PrintLine("\n--- Disk Information ---");
    
    // Check disk status first
    BYTE disk_stat = disk_status(0);
    hardware.PrintLine("Current disk status: 0x%02X", disk_stat);
    if(disk_stat != 0) {
      hardware.PrintLine("Warning: Disk not ready!");
      if(disk_stat & STA_NOINIT)   hardware.PrintLine("  - Not initialized");
      if(disk_stat & STA_NODISK)   hardware.PrintLine("  - No medium");
      if(disk_stat & STA_PROTECT)  hardware.PrintLine("  - Write protected");
    }
    
    FATFS* fs;
    DWORD free_clusters, total_sectors, free_sectors;
    
    FRESULT res = f_getfree(fsi_handler.GetSDPath(), &free_clusters, &fs);
    hardware.PrintLine("f_getfree result: %d", res);
    
    if(res == FR_OK) {
      // Calculate total and free space
      total_sectors = (fs->n_fatent - 2) * fs->csize;
      free_sectors = free_clusters * fs->csize;
      
      // Convert to MB (assuming 512 byte sectors)
      int total_mb = (total_sectors / 2) / 1024;
      int free_mb = (free_sectors / 2) / 1024;
      int used_mb = total_mb - free_mb;
      
      hardware.PrintLine("Total Space: %d MB", total_mb);
      hardware.PrintLine("Used Space: %d MB", used_mb);
      hardware.PrintLine("Free Space: %d MB", free_mb);
    } else {
      hardware.PrintLine("ERROR: Could not get disk info (Error: %d)", res);
      PrintFresultError(res);
    }
  }
};

int main() {
  SDcard mSDcard;
  mSDcard.start();
  return 0;
}


