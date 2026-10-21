# The Wii U GX2 probe (plan phase 0, GPU half): an RPX and a WUHB that answer the renderer's questions on a console.
#
#   make                    devkitPPC's GCC            -> build/gcc/opence-probe-gx2.{rpx,wuhb}
#   make COMPILER=clang     clang, as the port builds  -> build/clang/opence-probe-gx2.{rpx,wuhb}
#   make analyze            clang's static analyzer over the probe's own sources
#   make inspect            the RPX's sections and imports (readrpl)
#   make NOCOMPILER=1       the same without CafeGLSL linked in (build/<compiler>-nocompiler): if the full program does not start
#                           on a console, this one does, and answers everything but the compiler's questions
#   make dist               builds the three: dist/wiiu/ is what to copy to the SD card
#   make clean              the chosen compiler's build; distclean: all of them
#
# It links CafeGLSL (Exzap/CafeGLSL v1.0.0-rc1, a static library) from CAFEGLSL, a folder holding include/ and lib/
# (tools/wiiu/fetch_cafeglsl.sh downloads and checks it; tools/wiiu/build_probe_gx2.sh mounts it at /cafeglsl).
# In the opence-wiiu container: tools/wiiu/build_probe_gx2.sh in the repository around OpenCE. V=1 shows the commands.
# Warnings are errors, for both compilers.

ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO (the opence-wiiu container does)")
endif

NOCOMPILER ?= 0
CAFEGLSL   ?= /cafeglsl
ifneq ($(NOCOMPILER),1)
ifeq ($(wildcard $(CAFEGLSL)/lib/libcafeglsl.a),)
$(error "No CafeGLSL at $(CAFEGLSL): run tools/wiiu/fetch_cafeglsl.sh, or set CAFEGLSL to the folder with include/ and lib/")
endif
endif

TARGET     := opence-probe-gx2
COMPILER   ?= gcc
FLAVOUR    := $(COMPILER)$(if $(filter 1,$(NOCOMPILER)),-nocompiler)
BUILD      := build/$(FLAVOUR)
OPENCE     := $(abspath ../../..)
PROBE      := $(OPENCE)/port/wiiu/probe

APP_NAME       := OpenCE Wii U GX2 probe ($(FLAVOUR))
APP_SHORTNAME  := OpenCE GX2 probe $(FLAVOUR)
APP_AUTHOR     := OpenCE Wii U port

include $(DEVKITPRO)/wut/share/wut_rules
include $(OPENCE)/port/wiiu/abi.mk

ifeq ($(COMPILER),gcc)
CC         := $(PREFIX)gcc
EXTRA      := -meabi
else ifeq ($(COMPILER),clang)
CC         := wiiu-clang
EXTRA      :=
else
$(error COMPILER is gcc or clang)
endif
LINK       := $(PREFIX)gcc

# the commit this was built from (tools/wiiu/build_probe_gx2.sh passes it: the container has no git)
BUILD_ID   ?= unknown
Q          := $(if $(V),,@)

# the report's text buffer is the first probe's (shared, not copied)
ifeq ($(NOCOMPILER),1)
SOURCES    := $(wildcard src/*.c) $(filter-out src/wiiu/stage_glsl.c,$(wildcard src/wiiu/*.c)) src/wiiu/nocompiler/stage_glsl_none.c
else
SOURCES    := $(wildcard src/*.c) $(wildcard src/wiiu/*.c)
endif
SHARED     := $(PROBE)/src/report.c $(OPENCE)/port/wiiu/src/wiiu_align_fixup.c
OFILES     := $(SOURCES:%.c=$(BUILD)/%.o) $(BUILD)/probe/report.o $(BUILD)/shared/wiiu_align_fixup.o

WARNINGS   := -Wall -Wextra -Werror -Wno-unused-parameter
INCLUDES   := -I$(PROBE)/src -I$(CAFEGLSL)/include -I$(WUT_ROOT)/include
CFLAGS     := -std=gnu11 -O2 -g -ffunction-sections -fdata-sections $(HALO_WIIU_CODEGEN) $(EXTRA) $(WARNINGS) \
	-DESPRESSO -D__WIIU__ -D__WUT__ -DPROBE_BUILD_ID='"$(BUILD_ID)"' -DPROBE_FLAVOUR='"$(FLAVOUR)"' $(INCLUDES)
LDFLAGS    := -g $(RPXSPECS) -mcpu=750 -meabi -mhard-float -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map
ifeq ($(NOCOMPILER),1)
LIBS       := -L$(WUT_ROOT)/lib -lwut -lm
else
LIBS       := -L$(CAFEGLSL)/lib -L$(WUT_ROOT)/lib -lcafeglsl -lwut -lm -lstdc++
endif

.PHONY: all clean distclean analyze inspect dist
all: $(BUILD)/$(TARGET).rpx $(BUILD)/$(TARGET).wuhb

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC   $<"
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/probe/report.o: $(PROBE)/src/report.c
	@mkdir -p $(dir $@)
	@echo "  CC   $< (shared with the first probe)"
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/shared/wiiu_align_fixup.o: $(OPENCE)/port/wiiu/src/wiiu_align_fixup.c
	@mkdir -p $(dir $@)
	@echo "  CC   $< (shared with the shell)"
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

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

# the clang one is what the port is built with, the GCC one is for comparing the two, the one without the compiler is for a
# console where the full program does not start
dist:
	$(MAKE) COMPILER=gcc all
	$(MAKE) COMPILER=clang all
	$(MAKE) COMPILER=clang NOCOMPILER=1 all
	rm -rf dist
	mkdir -p dist/wiiu/apps/opence-probe-gx2
	for c in clang gcc clang-nocompiler; do \
		cp build/$$c/$(TARGET).wuhb dist/wiiu/apps/opence-probe-gx2/$(TARGET)-$$c.wuhb; \
		cp build/$$c/$(TARGET).rpx dist/wiiu/apps/opence-probe-gx2/$(TARGET)-$$c.rpx; \
	done
	@echo "dist/wiiu: copy this folder to the SD card's root"

clean:
	rm -rf $(BUILD) dist

distclean:
	rm -rf build dist
