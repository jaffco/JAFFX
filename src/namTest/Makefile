# Project Name
TARGET = namTest

CPP_STANDARD = -std=gnu++14
OPT=-Ofast

# Set App Type
APP_TYPE = BOOT_SRAM

# Set Boot Bin to 10ms version (default is 2000ms)
BOOT_BIN = $(SYSTEM_FILES_DIR)/dsy_bootloader_v6_2-intdfu-10ms.bin

# Sources
CPP_SOURCES = namTest.cpp

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

C_INCLUDES += -I../../RTNeural
C_INCLUDES += -I../../RTNeural/modules/Eigen
C_INCLUDES += -I../../RTNeural/modules/rt-nam
CPPFLAGS += -DRTNEURAL_DEFAULT_ALIGNMENT=8 -DRTNEURAL_NO_DEBUG=1 -DRTNEURAL_USE_EIGEN=1

