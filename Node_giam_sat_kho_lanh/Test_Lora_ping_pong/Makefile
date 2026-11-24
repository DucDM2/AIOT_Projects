# Makefile thay the cho build.ps1 (Linux/macOS/MSYS2).  make VARIANT=H  |  make VARIANT=L
VARIANT ?= H
PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

OUT  := build/$(VARIANT)
NAME := rak811_lora_pingpong_$(VARIANT)

SRCS := src/main.c src/hal.c src/sx1276.c src/syscalls.c drivers/cmsis/device/system_stm32l1xx.c
ASMS := drivers/cmsis/device/startup_stm32l151xba.s
OBJS := $(addprefix $(OUT)/,$(notdir $(SRCS:.c=.o) $(ASMS:.s=.o)))

CPU     := -mcpu=cortex-m3 -mthumb
CFLAGS  := $(CPU) -std=gnu11 -Os -g3 -ffunction-sections -fdata-sections -Wall -Wextra \
           -DSTM32L151xBA -DRAK811_VARIANT_$(VARIANT) -Isrc -Idrivers/cmsis/core -Idrivers/cmsis/device
LDFLAGS := $(CPU) -T linker/STM32L151CBUx.ld --specs=nano.specs --specs=nosys.specs \
           -Wl,-Map=$(OUT)/$(NAME).map,--cref -Wl,--gc-sections -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments

vpath %.c src drivers/cmsis/device
vpath %.s drivers/cmsis/device

all: $(OUT)/$(NAME).hex $(OUT)/$(NAME).bin

$(OUT):
	mkdir -p $@

$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/%.o: %.s | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/$(NAME).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@
	$(SIZE) $@

$(OUT)/$(NAME).hex: $(OUT)/$(NAME).elf
	$(OBJCOPY) -O ihex $< $@

$(OUT)/$(NAME).bin: $(OUT)/$(NAME).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf build

.PHONY: all clean
