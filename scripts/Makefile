# Makefile for v0_m7_baremetal (STM32H753XI Cortex-M7)

TARGET = edp_m7_firmware
BUILD_DIR = build

# Toolchain definitions
CC = arm-none-eabi-gcc
LD = arm-none-eabi-ld
OBJCOPY = arm-none-eabi-objcopy
OBJDUMP = arm-none-eabi-objdump
SIZE = arm-none-eabi-size

# Architecture specific flags for Cortex-M7 with double-precision FPU
MCU_FLAGS = -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16
RENODE ?= /mnt/d/Desktop/Vault/03\ Projects/uff/portfolio/edp-embed-final/renode_win/renode_1.16.1-dotnet_portable/renode.exe

# Compilation flags
CFLAGS = $(MCU_FLAGS) -Wall -Wextra -g3 -O0 -ffunction-sections -fdata-sections
CFLAGS += -Isrc -DSTM32H753xx

# Linker flags
LDFLAGS = $(MCU_FLAGS) -T stm32h753.ld --specs=nosys.specs -Wl,--gc-sections,-Map=$(BUILD_DIR)/$(TARGET).map -nostdlib

# Source files
SRCS = src/main.c src/startup.c
OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean simulate

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin size

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) stm32h753.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(BUILD_DIR)/$(TARGET).elf
	$(SIZE) $<

simulate: all
	$(RENODE) --console --plain --disable-gui -e "include @simulate.rescript; emulation RunFor '0.1'; quit" || true
	@echo "--- simulation completed, output logs: ---"
	@cat uart_output.log 2>/dev/null || echo "(no uart output produced)"

clean:
	rm -rf $(BUILD_DIR) uart_output.log renode_trace.log
