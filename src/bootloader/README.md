# Bootloader

This program is designed to act as a "bootloader" that will allow for the flashing of larger programs to `SRAM`. 

Daisy's flash memory (the default location for programs) can only hold binaries up to 128KB in size, which can be limiting for those who want to develop larger, more complex programs. 

This is where the bootloader is helpful. When flashed with `bootloader.cpp` using `make program-boot` instead of `make program-dfu` (as implemented in `./run.sh`), the Daisy will enter "bootloader" mode for a brief (2.5s) window at the beginning of every power cycle. This mode will be indicated by sinusoidal flashing of the USR light, and is extendable by pressing the BOOT button.

While the Daisy is in bootloader mode, you can flash a larger program with `./run.sh` (or `SHIFT+CMD+B` in VSCode), just make sure that:
1. The line `APP_TYPE = BOOT_SRAM` is added to the `Makefile`
2. The compiled program is less than 480KB

If you wish to flash a binary larger than 480KB, see [Electrosmith's tutorial](https://electro-smith.github.io/libDaisy/md_doc_2md_2__a7___getting-_started-_daisy-_bootloader.html) on flashing programs to QSPI (note that QSPI has significantly worse performance than SRAM).