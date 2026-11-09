CROSS_COMPILE ?= riscv32-none-elf-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
SIZE := $(CROSS_COMPILE)size

# Hardware charge/discharge is the only measurement algorithm.
ifneq ($(origin MODE),undefined)
$(error MODE is no longer supported; omit it to build hardware charge/discharge)
endif
# Captures per block (1..131); report deviation over 128 blocks.
SAMPLES ?= 16
# Independent charge-through-R and forced-GPIO-discharge hold times.
CHARGE_LOOPS ?= 64
DISCHARGE_LOOPS ?= 0
# AWU: (AWU_WINDOW + 1) * 64 / 128 kHz; nominal sleep = 15 ms.
STANDBY ?= 1
AWU_WINDOW ?= 29
# Completed deviation results retained until a PC4 request.
BUFFER_CAPACITY ?= 32
BUILD := build/$(SAMPLES)/$(CHARGE_LOOPS)-$(DISCHARGE_LOOPS)/sleep-$(STANDBY)-$(AWU_WINDOW)/buffer-$(BUFFER_CAPACITY)
ARCH := -march=rv32ec_zicsr_zifencei -mabi=ilp32e
CFLAGS := $(ARCH) -Os -g3 -std=c11 -Wall -Wextra -Werror \
          -ffreestanding -fno-builtin -fno-common -fno-pic -fno-pie \
          -ffunction-sections -fdata-sections -msmall-data-limit=0
LDFLAGS := $(ARCH) -nostdlib -nostartfiles -static \
           -Wl,-T,link.ld,--gc-sections,--build-id=none,-Map,$(BUILD)/firmware.map

.PHONY: all clean delay-probe discharge-probe
all: $(BUILD)/firmware.bin $(BUILD)/firmware.hex $(BUILD)/firmware.lst

$(BUILD):
	mkdir -p $@

$(BUILD)/main.o: main.c registers.h statistics.h | $(BUILD)
	$(CC) $(CFLAGS) -DSAMPLES=$(SAMPLES) \
	    -DCHARGE_SETTLE_LOOPS=$(CHARGE_LOOPS) \
	    -DDISCHARGE_SETTLE_LOOPS=$(DISCHARGE_LOOPS) \
	    -DBLOCK_STANDBY=$(STANDBY) -DAWU_WINDOW=$(AWU_WINDOW) \
	    -DBUFFER_CAPACITY=$(BUFFER_CAPACITY) -c $< -o $@

$(BUILD)/startup.o: startup.S | $(BUILD)
	$(CC) $(ARCH) -g3 -c $< -o $@

$(BUILD)/firmware.elf: $(BUILD)/startup.o $(BUILD)/main.o link.ld
	$(CC) $(LDFLAGS) $(BUILD)/startup.o $(BUILD)/main.o -o $@
	$(SIZE) $@

$(BUILD)/firmware.bin: $(BUILD)/firmware.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/firmware.hex: $(BUILD)/firmware.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/firmware.lst: $(BUILD)/firmware.elf
	$(OBJDUMP) -d -S $< > $@

# Standalone SWIO experiment; does not replace the normal UART firmware.
PROBE_BUILD := build/poweropt/probe/$(DISCHARGE_LOOPS)
delay-probe: $(PROBE_BUILD)/probe.bin

$(PROBE_BUILD):
	mkdir -p $@

$(PROBE_BUILD)/probe.o: tests/delay_probe.c main.c registers.h statistics.h | $(PROBE_BUILD)
	$(CC) $(CFLAGS) -DSAMPLES=16 \
	    -DDISCHARGE_SETTLE_LOOPS=$(DISCHARGE_LOOPS) -c $< -o $@

$(PROBE_BUILD)/probe.elf: $(BUILD)/startup.o $(PROBE_BUILD)/probe.o link.ld
	$(CC) $(ARCH) -nostdlib -nostartfiles -static \
	    -Wl,-T,link.ld,--gc-sections,--build-id=none,-Map,$(PROBE_BUILD)/probe.map \
	    $(BUILD)/startup.o $(PROBE_BUILD)/probe.o -o $@
	$(SIZE) $@

$(PROBE_BUILD)/probe.bin: $(PROBE_BUILD)/probe.elf
	$(OBJCOPY) -O binary $< $@

DISCHARGE_PROBE_BUILD := build/poweropt/discharge-probe/$(CHARGE_LOOPS)-$(DISCHARGE_LOOPS)
discharge-probe: $(DISCHARGE_PROBE_BUILD)/probe.bin

$(DISCHARGE_PROBE_BUILD):
	mkdir -p $@

$(DISCHARGE_PROBE_BUILD)/probe.o: tests/discharge_probe.c main.c registers.h statistics.h | $(DISCHARGE_PROBE_BUILD)
	$(CC) $(CFLAGS) -DSAMPLES=16 \
	    -DCHARGE_SETTLE_LOOPS=$(CHARGE_LOOPS) \
	    -DDISCHARGE_SETTLE_LOOPS=$(DISCHARGE_LOOPS) -c $< -o $@

$(DISCHARGE_PROBE_BUILD)/probe.elf: $(BUILD)/startup.o $(DISCHARGE_PROBE_BUILD)/probe.o link.ld
	$(CC) $(ARCH) -nostdlib -nostartfiles -static \
	    -Wl,-T,link.ld,--gc-sections,--build-id=none,-Map,$(DISCHARGE_PROBE_BUILD)/probe.map \
	    $(BUILD)/startup.o $(DISCHARGE_PROBE_BUILD)/probe.o -o $@
	$(SIZE) $@

$(DISCHARGE_PROBE_BUILD)/probe.bin: $(DISCHARGE_PROBE_BUILD)/probe.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf build
