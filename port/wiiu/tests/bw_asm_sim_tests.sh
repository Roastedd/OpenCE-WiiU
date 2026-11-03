#!/usr/bin/env bash
# The Broadway kernels' real machine code, run through the instruction simulator (gekko_sim.h) on 32-bit big-endian PowerPC under
# qemu-ppc, through the cases of the scalar models' test (test_bw_asm_sim.c): the check that the assembly is what the models say, which
# QEMU cannot do by running it (no paired singles). The objects are assembled by devkitPPC as the Wii U build does.
#   port/wiiu/tests/bw_asm_sim_tests.sh [multiplier]
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
multiplier="${1:-1}"
docker run --rm -v "$root:/src:ro" -w /src opence-wiiu bash -ec '
	mkdir -p /tmp/bw/asm
	for source in port/wiiu/src/bw/*.S; do
		powerpc-eabi-gcc -c -g -mcpu=750 -meabi -mhard-float -Wa,-mgekko $source -o /tmp/bw/asm/$(basename $source .S).o
		python3 tools/wiiu_check_objects.py --quiet --bw-kernel-object /tmp/bw/asm/$(basename $source .S).o /tmp/bw/asm/$(basename $source .S).o
	done
	python3 -I tools/bw_object_image.py /tmp/bw/asm/*.o > /tmp/bw/bw_image.h
	abi="-fshort-wchar -fsigned-char -ffp-contract=off -fno-strict-aliasing -fwrapv"
	flags="-std=gnu11 -O2 -g -Wall -Wextra -Werror -DHALO_BW_KERNELS -iquote port/linux/include -iquote port/wiiu/src -iquote port/wiiu/tests -iquote /tmp/bw"
	clang --target=powerpc-linux-gnu -fuse-ld=lld -no-pie $abi $flags port/wiiu/tests/test_bw_asm_sim.c -o /tmp/bw/asm_sim
	time qemu-ppc -L /usr/powerpc-linux-gnu /tmp/bw/asm_sim '"$multiplier"'
'
