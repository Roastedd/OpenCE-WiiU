# The code generation flags the port's code is built with on the Wii U, whatever compiles it. They are the
# flags the Linux ports have (tools/linux_build.py LINUX_ABI_FLAGS) minus what only x86 has, so the same
# source means the same on every machine of a network game:
#
#   -fshort-wchar                       16-bit wchar_t: UTF-16 strings in tag data and saved games
#   -fsigned-char                       the game keeps NONE (-1) in char indices; PowerPC's char is unsigned
#   -ffp-contract=off                   no fused multiply-adds: every machine rounds twice, as x86 does
#   -fno-strict-aliasing -fwrapv        the source relies on what MSVC allows
#   -fno-delete-null-pointer-checks
#   -fcommon                            tentative definitions shared between units
#
# (-malign-double and -freg-struct-return are x86's; PowerPC EABI already aligns doubles to 8 bytes, which
# the probe's abi stage and the layout checks verify.)
HALO_WIIU_CODEGEN := -mcpu=750 -mhard-float -fshort-wchar -fsigned-char -ffp-contract=off -fno-strict-aliasing -fwrapv \
	-fno-delete-null-pointer-checks -fcommon
