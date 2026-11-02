#!/usr/bin/env bash
# The Broadway kernels' scalar models against the C they replace, on the three machines the port has to agree across (the
# container's own CPU, 32-bit x86 under qemu-i386, 32-bit big-endian PowerPC under qemu-ppc), each with the game's floating point
# flags: millions of random and edge inputs, every output bit compared (port/wiiu/tests/test_bw_model.c).
# Then the game's own sources against the reference copies (tools/test_bw_reference_sync.py).
#   port/wiiu/tests/bw_tests.sh [multiplier]      (the runs under qemu are about 30 times slower: the multiplier scales the case counts)
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
multiplier="${1:-1}"
python3 -I "$root/tools/test_bw_reference_sync.py"
docker run --rm -v "$root:/src:ro" -w /src opence-wiiu bash -ec '
	abi="-fshort-wchar -fsigned-char -ffp-contract=off -fno-strict-aliasing -fwrapv"
	flags="-std=gnu11 -O2 -g -Wall -Wextra -Werror -DHALO_BW_KERNELS -DHALO_BW_MODEL -iquote port/linux/include -iquote port/wiiu/src"
	src=port/wiiu/tests/test_bw_model.c
	mkdir -p /tmp/bw
	clang $abi $flags -fsanitize=undefined -fsanitize-trap=undefined $src -o /tmp/bw/native
	clang-i686 --target=i686-linux-gnu -m32 -march=x86-64 -malign-double -fno-pic -no-pie $abi $flags $src -o /tmp/bw/x86
	clang --target=powerpc-linux-gnu -fuse-ld=lld -no-pie $abi $flags $src -o /tmp/bw/ppc
	echo "== native (ARM64, undefined behaviour trapping)"; /tmp/bw/native '"$multiplier"'
	echo "== 32-bit x86 (SSE), qemu-i386";  qemu-i386 -L /usr/i686-linux-gnu /tmp/bw/x86 '"$multiplier"'
	echo "== 32-bit big-endian PowerPC, qemu-ppc"; qemu-ppc -L /usr/powerpc-linux-gnu /tmp/bw/ppc '"$multiplier"'
'
