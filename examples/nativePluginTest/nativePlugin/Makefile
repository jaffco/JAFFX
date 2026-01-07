TARGET      = nativePluginTest
BUILD_DIR   = build
CPP_SOURCE  = nativePluginTest.cpp
LD_SCRIPT   = lpa.ld

CXX         = arm-none-eabi-g++
OBJCOPY     = arm-none-eabi-objcopy
OBJDUMP     = arm-none-eabi-objdump

ELF         = $(BUILD_DIR)/$(TARGET).elf
BIN         = $(BUILD_DIR)/$(TARGET).bin
DISASM      = $(BUILD_DIR)/$(TARGET).disasm
PLUGIN_HDR  = $(BUILD_DIR)/plugin_binary.h

CXXFLAGS = \
  -mcpu=cortex-m7 \
  -mthumb \
  -mfpu=fpv5-d16 \
  -mfloat-abi=hard \
  -Ofast \
  -ffunction-sections \
  -fdata-sections \
  -fno-exceptions \
  -fno-rtti \
  -fno-unwind-tables \
  -fno-asynchronous-unwind-tables \
  -Wall

LDFLAGS = \
  -nostdlib \
  -nostartfiles \
  -nodefaultlibs \
  -T $(LD_SCRIPT) \
  -Wl,--gc-sections

# -------------------------
# Default target
# -------------------------
all: $(PLUGIN_HDR)

# Ensure build directory exists
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build ELF
$(ELF): $(CPP_SOURCE) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

# Convert ELF to raw binary
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

# Generate C header from binary (default build only)
$(PLUGIN_HDR): $(BIN)
	@echo '__attribute__((section(".sram1_bss")))' > $@
	xxd -i $< >> $@

# -------------------------
# Disassembly target
# -------------------------
disasm: $(ELF)
	$(OBJDUMP) -S -C -d $< > $(DISASM)

# -------------------------
# Clean
# -------------------------
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean disasm

# Run: `arm-none-eabi-objdump -h build/nativePluginTest.elf` to see the .entry section
# TODO: Add `xxd -i ./build/nativePluginTest.bin > plugin_binary.h`