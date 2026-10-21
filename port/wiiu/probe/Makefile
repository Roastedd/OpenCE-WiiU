# The Wii U probe (plan phase 0): an RPX and a WUHB that measure what the console allows.
#
#   make                    devkitPPC's GCC            -> build/gcc/opence-probe.{rpx,wuhb}
#   make COMPILER=clang     clang, as the port builds  -> build/clang/opence-probe.{rpx,wuhb}
#   make analyze            clang's static analyzer over the probe's own sources
#   make inspect            the RPX's sections and imports (readrpl)
#   make dist               builds both: dist/wiiu/ is what to copy to the SD card
#   make clean              the chosen compiler's build; distclean: all of them
#
# In the opence-wiiu container: tools/wiiu/build_probe.sh in the repository around OpenCE. V=1 shows the commands.
# Warnings are errors, for both compilers: the probe is small enough to be clean and what it
# reports must not hide behind noise.

ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO (the opence-wiiu container does)")
endif

TARGET     := opence-probe
COMPILER   ?= gcc
BUILD      := build/$(COMPILER)
OPENCE     := $(abspath ../../..)
MUSL       := $(OPENCE)/port/third_party/musl-math

APP_NAME       := OpenCE Wii U probe ($(COMPILER))
APP_SHORTNAME  := OpenCE probe $(COMPILER)
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

# the commit this was built from (tools/wiiu/build_probe.sh passes it: the container has no git)
BUILD_ID   ?= unknown
Q          := $(if $(V),,@)

SOURCES    := $(wildcard src/*.c) $(wildcard src/wiiu/*.c)
MUSL_SOURCES := $(wildcard $(MUSL)/src/*.c)
# units shared with the port (the shell's), built here with the probe's flags
SHARED     := $(OPENCE)/port/wiiu/src/wiiu_align_fixup.c
OFILES     := $(SOURCES:%.c=$(BUILD)/%.o) $(patsubst $(OPENCE)/port/wiiu/src/%.c,$(BUILD)/shared/%.o,$(SHARED)) $(patsubst $(MUSL)/src/%.c,$(BUILD)/musl/%.o,$(MUSL_SOURCES))

WARNINGS   := -Wall -Wextra -Werror -Wno-unused-parameter
CFLAGS     := -std=gnu11 -O2 -g -ffunction-sections -fdata-sections $(HALO_WIIU_CODEGEN) $(EXTRA) $(WARNINGS) \
	-DESPRESSO -D__WIIU__ -D__WUT__ -DPROBE_BUILD_ID='"$(BUILD_ID)"' -DPROBE_FLAVOUR='"$(COMPILER)"' -I$(WUT_ROOT)/include
# the maths is the game's own (vendored musl, port/third_party/musl-math): built as the game builds it
MUSL_FLAGS := -std=gnu11 -O2 -g -ffunction-sections -fdata-sections $(HALO_WIIU_CODEGEN) $(EXTRA) -w \
	-DESPRESSO -D__WIIU__ -D__WUT__ -I$(MUSL)/include -include $(MUSL)/include/libm.h -I$(WUT_ROOT)/include
LDFLAGS    := -g $(RPXSPECS) -mcpu=750 -meabi -mhard-float -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS       := -L$(WUT_ROOT)/lib -lwut -lm

.PHONY: all clean distclean analyze inspect dist
all: $(BUILD)/$(TARGET).rpx $(BUILD)/$(TARGET).wuhb

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC   $<"
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/shared/%.o: $(OPENCE)/port/wiiu/src/%.c
	@mkdir -p $(dir $@)
	@echo "  CC   $(notdir $<) (shared)"
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/musl/%.o: $(MUSL)/src/%.c
	@mkdir -p $(dir $@)
	@echo "  CC   $(notdir $<) (musl)"
	$(Q)$(CC) $(MUSL_FLAGS) -MMD -MP -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OFILES)
	@echo "  LINK $(notdir $@)"
	$(Q)$(LINK) $(LDFLAGS) $(OFILES) $(LIBS) -o $@

-include $(OFILES:.o=.d)

# a change of flags or of the application's names is a change of the result
$(OFILES) $(BUILD)/$(TARGET).wuhb: Makefile $(OPENCE)/port/wiiu/abi.mk

# clang's static analyzer: the analyzer's findings are warnings, and any is a failure
analyze:
	@mkdir -p build
	@rm -f build/analyze.log
	@for f in $(SOURCES) $(SHARED); do \
		wiiu-clang --analyze -Xanalyzer -analyzer-output=text $(filter-out -meabi,$(CFLAGS)) $$f -o /dev/null 2>> build/analyze.log || exit 1; \
	done
	@if grep -q 'warning:' build/analyze.log; then cat build/analyze.log; echo "analyzer findings"; exit 1; fi
	@echo "analyzer: $(words $(SOURCES) $(SHARED)) files, no findings"

inspect: $(BUILD)/$(TARGET).rpx
	readrpl -S $< | head -40
	readrpl -i $< | head -40

# both builds: the clang one is what the port is built with, the GCC one is for comparing the two
dist:
	$(MAKE) COMPILER=gcc all
	$(MAKE) COMPILER=clang all
	rm -rf dist
	mkdir -p dist/wiiu/apps/opence-probe
	for c in clang gcc; do \
		cp build/$$c/$(TARGET).wuhb dist/wiiu/apps/opence-probe/$(TARGET)-$$c.wuhb; \
		cp build/$$c/$(TARGET).rpx dist/wiiu/apps/opence-probe/$(TARGET)-$$c.rpx; \
	done
	@echo "dist/wiiu: copy this folder to the SD card's root"

clean:
	rm -rf $(BUILD) dist

distclean:
	rm -rf build dist
