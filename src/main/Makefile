# Project Name
TARGET = main

# Set App Type
APP_TYPE = BOOT_SRAM

# Set Boot Bin to 10ms version (default is 2000ms)
BOOT_BIN = $(SYSTEM_FILES_DIR)/dsy_bootloader_v6_2-intdfu-10ms.bin

# Sources
CPP_SOURCES = main.cpp

# Library Locations
LIBDAISY_DIR = ../../libDaisy

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

