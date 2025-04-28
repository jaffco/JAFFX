# Project Name
TARGET = gimmelTests

# Sources
CPP_SOURCES = gimmelTests.cpp

# Set App Type
APP_TYPE = BOOT_SRAM

# Library Locations
LIBDAISY_DIR = ../../libDaisy

C_SOURCES = \
$(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/FastMathFunctions/arm_sin_f32.c \
$(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/FastMathFunctions/arm_cos_f32.c \
$(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/ControllerFunctions/arm_sin_cos_f32.c \
$(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/CommonTables/arm_common_tables.c \

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

