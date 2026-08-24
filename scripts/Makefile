# Makefile for EDP Georeferencing Coprocessor (STM32H753XI Cortex-M7)
# Run from project root: make -f scripts/Makefile

TARGET = edp_m7_firmware
BUILD_DIR = build
SRC_DIR = src

# Toolchain definitions
CC = arm-none-eabi-gcc
LD = arm-none-eabi-ld
OBJCOPY = arm-none-eabi-objcopy
OBJDUMP = arm-none-eabi-objdump
SIZE = arm-none-eabi-size

# Architecture specific flags for Cortex-M7 with double-precision FPU
MCU_FLAGS = -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16
RENODE ?= "D:/Desktop/Vault/03 Projects/juff/portfolio/edp-embed-final/renode_win/renode_1.16.1-dotnet_portable/renode.exe"

# Compilation flags
CFLAGS = $(MCU_FLAGS) -Wall -Wextra -g3 -O0 -ffunction-sections -fdata-sections
CFLAGS += -I$(SRC_DIR) -DSTM32H753xx -DSIMULATION_BUILD

# Linker flags
LDFLAGS = $(MCU_FLAGS) -T $(SRC_DIR)/stm32h753.ld --specs=nosys.specs --specs=nano.specs -Wl,--gc-sections,-Map=$(BUILD_DIR)/$(TARGET).map -nostdlib -lc -lnosys -lm -lgcc

# Object files
OBJS = $(BUILD_DIR)/main.o \
       $(BUILD_DIR)/startup.o \
       $(BUILD_DIR)/syscalls.o \
       $(BUILD_DIR)/mavlink_parser.o \
       $(BUILD_DIR)/hw_timer.o \
       $(BUILD_DIR)/ring_buffer.o \
       $(BUILD_DIR)/ekf.o \
       $(BUILD_DIR)/geo_transform.o \
       $(BUILD_DIR)/output_stream.o

.PHONY: all clean simulate size disasm

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin size

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/startup.o: $(SRC_DIR)/startup.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mavlink_parser.o: $(SRC_DIR)/mavlink_parser.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hw_timer.o: $(SRC_DIR)/hw_timer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ring_buffer.o: $(SRC_DIR)/ring_buffer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ekf.o: $(SRC_DIR)/ekf.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/geo_transform.o: $(SRC_DIR)/geo_transform.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/output_stream.o: $(SRC_DIR)/output_stream.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscalls.o: $(SRC_DIR)/syscalls.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(SRC_DIR)/stm32h753.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(BUILD_DIR)/$(TARGET).elf
	$(SIZE) $<

disasm: $(BUILD_DIR)/$(TARGET).elf
	$(OBJDUMP) -d $< > $(BUILD_DIR)/$(TARGET).lst

simulate: all
	@cp build/edp_m7_firmware.elf D:/Desktop/edp_firmware.elf 2>/dev/null
	@cd sim && $(RENODE) --console --plain --disable-gui -e "include @simulate.rescript; emulation RunFor '0.5'; quit" || true
	@echo "--- simulation completed, output logs: ---"
	@cat sim/uart_output.log 2>/dev/null || echo "(no uart output produced)"

clean:
	rm -rf $(BUILD_DIR) uart_output.log renode_trace.log
