# Get the directory where this common.mk file is located
CONFIG_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Library Locations (allow environment override)
LIBDAISY_DIR ?= $(CONFIG_DIR)libDaisy
RTNEURAL_DIR ?= $(CONFIG_DIR)RTNeural
GIMMEL_DIR ?= $(CONFIG_DIR)Gimmel

# Normalize paths for cross-platform compatibility
normalize_path = $(subst \,/,$(1))
LIBDAISY_DIR := $(call normalize_path,$(LIBDAISY_DIR))
RTNEURAL_DIR := $(call normalize_path,$(RTNEURAL_DIR))
GIMMEL_DIR := $(call normalize_path,$(GIMMEL_DIR))

# Verify critical directories exist
$(if $(wildcard $(LIBDAISY_DIR)),,$(error libDaisy directory not found at $(LIBDAISY_DIR)))
$(if $(wildcard $(RTNEURAL_DIR)),,$(error RTNeural directory not found at $(RTNEURAL_DIR)))

# Define subdirectories
SYSTEM_FILES_DIR := $(LIBDAISY_DIR)/core

# Set Boot Bin to 10ms version (default is 2000ms)
BOOT_BIN = $(SYSTEM_FILES_DIR)/dsy_bootloader_v6_3-intdfu-10ms.bin

# Set app type
APP_TYPE = BOOT_SRAM

# Set the C++ standard
CPP_STANDARD = -std=gnu++14

# Set optimization level
OPT=-Ofast

# Core location, and generic makefile.
include $(SYSTEM_FILES_DIR)/Makefile

# Includes and flags for RTNeural
C_INCLUDES += -I$(RTNEURAL_DIR)
C_INCLUDES += -I$(RTNEURAL_DIR)/modules/Eigen
C_INCLUDES += -I$(RTNEURAL_DIR)/modules/rt-nam

# Gimmel includes
C_INCLUDES += -I$(GIMMEL_DIR)/include

# RTNeural compiler flags
CPPFLAGS += -DRTNEURAL_DEFAULT_ALIGNMENT=8 -DRTNEURAL_NO_DEBUG=1 -DRTNEURAL_USE_EIGEN=1

# Debug information (can be disabled by setting VERBOSE=0)
ifneq ($(VERBOSE),0)
$(info CONFIG_DIR: $(CONFIG_DIR))
$(info LIBDAISY_DIR: $(LIBDAISY_DIR))
$(info RTNEURAL_DIR: $(RTNEURAL_DIR))
$(info GIMMEL_DIR: $(GIMMEL_DIR))
endif