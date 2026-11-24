# Makefile thay the cho build.ps1 (Linux/macOS/MSYS2).  make  |  make test (test tren PC)
PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size
HOSTCC  ?= gcc

OUT  := build
NAME := rak811_am2301

SRCS := src/main.c src/hal.c src/am2301.c src/am2301_port.c src/syscalls.c \
        drivers/cmsis/device/system_stm32l1xx.c
ASMS := drivers/cmsis/device/startup_stm32l151xba.s
OBJS := $(addprefix $(OUT)/,$(notdir $(SRCS:.c=.o) $(ASMS:.s=.o)))

CPU     := -mcpu=cortex-m3 -mthumb
CFLAGS  := $(CPU) -std=gnu11 -Os -g3 -ffunction-sections -fdata-sections -Wall -Wextra -Wshadow \
           -Wdouble-promotion -Werror -DSTM32L151xBA -Isrc -Idrivers/cmsis/core -Idrivers/cmsis/device
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

test: | $(OUT)
	$(HOSTCC) -std=gnu11 -O1 -g -Wall -Wextra -Wshadow -Wconversion -Werror -DAM2301_HOST_TEST -Isrc \
	    test/test_am2301.c src/am2301.c -o $(OUT)/test_am2301
	$(OUT)/test_am2301

clean:
	rm -rf build

.PHONY: all test clean
