#!/bin/sh
# Builds the PC peer (tools/pcpeer/pcpeer.c): the friends layer and the signalling MQTT code of internet play, from the tree's own sources, for this machine.
# Usage: tools/pcpeer/build.sh [output folder]   (default: build/pcpeer under the repository root); prints the program's path.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$ROOT/build/pcpeer}
CC=${CC:-clang}
mkdir -p "$OUT/obj" "$OUT/src" "$OUT/game"
# the sources are linked in (not copied): p2p_signal.c finds the stand-in platform.h beside itself, as the game's finds its own
for f in p2p_signal.c p2p_internal.h p2p.h p2p_crypto.c posix.h port_config.h posix_net.c; do ln -sf "$ROOT/port/linux/src/$f" "$OUT/src/$f"; done
cp "$ROOT/tools/pcpeer/shim/platform.h" "$OUT/src/platform.h"
for f in "$ROOT"/port/linux/game/social*.c "$ROOT"/port/linux/game/social*.h "$ROOT"/port/linux/game/text_safe.c "$ROOT"/port/linux/game/text_safe.h; do ln -sf "$f" "$OUT/game/"; done
FLAGS="-std=gnu99 -O2 -g -fsigned-char -w -I$OUT/src -I$OUT/game -idirafter $ROOT/port/linux/include -I$ROOT/port/third_party/monocypher"
OBJECTS=
for f in "$OUT"/src/p2p_signal.c "$OUT"/src/p2p_crypto.c "$OUT"/game/social*.c "$OUT"/game/text_safe.c \
         "$ROOT"/port/third_party/monocypher/monocypher.c "$ROOT"/port/third_party/monocypher/monocypher-ed25519.c "$ROOT"/tools/pcpeer/pcpeer.c; do
	o="$OUT/obj/$(basename "$f").o"
	$CC $FLAGS -c "$f" -o "$o"
	OBJECTS="$OBJECTS $o"
done
$CC $FLAGS -include "$ROOT/tools/pcpeer/compat.h" -c "$OUT/src/posix_net.c" -o "$OUT/obj/posix_net.o"
$CC -o "$OUT/pcpeer" $OBJECTS "$OUT/obj/posix_net.o" -lpthread -lm
echo "$OUT/pcpeer"
