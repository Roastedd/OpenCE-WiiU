# The Wii U memory probe: where does the game's hot data run fastest (MEM2 heap, MEM1 eDRAM, the foreground
# bucket)? A small RPX and WUHB that measure latency and bandwidth per core and per region and write a table
# to the SD card. See README.md.
#
#   make                    clang, as the port builds    -> build/clang/opence-memprobe.{rpx,wuhb}
#   make COMPILER=gcc       devkitPPC's GCC              -> build/gcc/opence-memprobe.{rpx,wuhb}
#
# In the opence-wiiu container:  docker run --rm -v <OpenCE>:/src -w /src/port/wiiu/memprobe opence-wiiu make

ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO (the opence-wiiu container does)")
endif

TARGET     := opence-memprobe
COMPILER   ?= clang
BUILD      := build/$(COMPILER)
OPENCE     := $(abspath ../../..)

APP_NAME       := OpenCE Wii U memory probe
APP_SHORTNAME  := OpenCE memprobe
APP_AUTHOR     := OpenCE Wii U port

include $(DEVKITPRO)/wut/share/wut_rules
include $(OPENCE)/port/wiiu/abi.mk

ifeq ($(COMPILER),gcc)
CC         := $(PREFIX)gcc
EXTRA      := -meabi
else ifeq ($(COMPILER),clang)
CC         := wiiu-clang
EXTRA      := $(HALO_WIIU_CODEGEN_CLANG)
else
$(error COMPILER is gcc or clang)
endif
LINK       := $(PREFIX)gcc

BUILD_ID   ?= unknown
Q          := $(if $(V),,@)

SOURCES    := $(wildcard src/*.c)
OFILES     := $(SOURCES:%.c=$(BUILD)/%.o)

WARNINGS   := -Wall -Wextra -Werror -Wno-unused-parameter
CFLAGS     := -std=gnu11 -O2 -g -ffunction-sections -fdata-sections $(HALO_WIIU_CODEGEN) $(EXTRA) $(WARNINGS) \
	-DESPRESSO -D__WIIU__ -D__WUT__ -DPROBE_BUILD_ID='"$(BUILD_ID)"' -I$(WUT_ROOT)/include
LDFLAGS    := -g $(RPXSPECS) -mcpu=750 -meabi -mhard-float -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS       := -L$(WUT_ROOT)/lib -lwut

.PHONY: all clean inspect
all: $(BUILD)/$(TARGET).rpx $(BUILD)/$(TARGET).wuhb

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC   $<"
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OFILES)
	@echo "  LINK $(notdir $@)"
	$(Q)$(LINK) $(LDFLAGS) $(OFILES) $(LIBS) -o $@

-include $(OFILES:.o=.d)

$(OFILES) $(BUILD)/$(TARGET).wuhb: Makefile $(OPENCE)/port/wiiu/abi.mk

inspect: $(BUILD)/$(TARGET).rpx
	readrpl -S $< | head -40
	readrpl -i $< | head -60

clean:
	rm -rf build
