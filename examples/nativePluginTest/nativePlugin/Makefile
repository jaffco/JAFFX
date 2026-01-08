TARGET      = nativePlugin
BUILD_DIR   = build
CPP_SOURCE  = nativePlugin.cpp
LD_SCRIPT   = lpa.ld

CXX         = arm-none-eabi-g++
OBJCOPY     = arm-none-eabi-objcopy
OBJDUMP     = arm-none-eabi-objdump

ELF         = $(BUILD_DIR)/$(TARGET).elf
BIN         = $(BUILD_DIR)/$(TARGET).bin
DISASM      = $(BUILD_DIR)/$(TARGET).disasm
PLUGIN_HDR  = $(BUILD_DIR)/$(TARGET).h

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
  # Adding proper alignment and section for executable code in SRAM
  # const ensures data is in initialized .data section, not .bss
  # aligned(16) ensures proper alignment for both code and data
	@echo '__attribute__((section(".sram1_data"), aligned(16)))' > $@
	@echo 'const unsigned char build_nativePlugin_bin[] = {' >> $@
	@xxd -i < $< | grep -v 'unsigned char' | grep -v '};' >> $@
	@echo '};' >> $@
	@echo 'const unsigned int build_nativePlugin_bin_len = '`wc -c < $< | tr -d ' '`';' >> $@

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