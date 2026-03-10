# Mini RTOS kernel for Cortex-M3 (STM32F103), bare-metal arm-none-eabi.
CC      := $(shell command -v arm-none-eabi-gcc 2>/dev/null || echo $(HOME)/opt/arm/usr/bin/arm-none-eabi-gcc)
OBJCOPY := $(patsubst %gcc,%objcopy,$(CC))
SIZE    := $(patsubst %gcc,%size,$(CC))

MCU     := -mcpu=cortex-m3 -mthumb
CFLAGS  := $(MCU) -std=c11 -Os -g -Wall -Wextra -ffreestanding \
           -ffunction-sections -fdata-sections -Iinclude
LDFLAGS := $(MCU) -nostdlib -Wl,--gc-sections -Tlinker/stm32f103c8.ld \
           -Wl,-Map=build/rtos.map
LIBGCC  := $(shell $(CC) $(MCU) -print-libgcc-file-name)

SRC     := $(wildcard src/*.c) startup/startup_stm32f103.c
OBJ     := $(SRC:%.c=build/%.o)
TARGET  := build/rtos

# Host unit tests: the portable kernel logic (kernel/mutex/queue/timer) built
# with the native compiler + a stub port, exercising the scheduler and the
# sync primitives' state machine. No hardware needed.
HOSTCC   ?= cc
PORTABLE := src/kernel.c src/mutex.c src/queue.c src/timer.c

.PHONY: all test clean
all: $(TARGET).elf $(TARGET).bin

test:
	@mkdir -p build
	$(HOSTCC) -std=c11 -O2 -g -Wall -Wextra -Iinclude -Itest \
	  $(PORTABLE) test/host_port.c test/host_test.c -o build/host_test
	@./build/host_test

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $^ $(LIBGCC) -o $@
	@$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build
