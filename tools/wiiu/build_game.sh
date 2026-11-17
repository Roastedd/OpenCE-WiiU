#!/usr/bin/env bash
# The game for the Wii U (configure.py --linux-target=wiiu, ninja linux_wiiu) in the opence-wiiu container:
# compiles everything that compiles, checks every object, links the RPX with stubs for what is not written, and writes the
# sizes. The results are in build/linux-wiiu/ (halo.elf, halo.rpx, halo.sizes.txt, wiiu_stubs.txt).
#   tools/wiiu/build_game.sh [ninja arguments]       (default target: linux_wiiu)
#   RELEASE=1 tools/wiiu/build_game.sh                the release build (configure.py --release, HALO_RELEASE: a failed assertion is
#                                                     noted in debug.txt and the game goes on, as the retail game did, except in the
#                                                     saved games' sources, where it still stops; the HUD's stack sentinel and its
#                                                     paired return-address reads are left out; the build says "release" to the hosts
#                                                     it joins). It is also built as one module (LTO=1) unless LTO=0 says otherwise.
#   LTO=1 tools/wiiu/build_game.sh                    the game's sources optimised together as one module (configure.py --wiiu-lto)
#   LEAN=1 tools/wiiu/build_game.sh                   the default flavour (its assertions still stop the game) with only the HUD's stack
#                                                     sentinel left out (HALO_NO_HUD_SENTINEL=1): the measured saving of RELEASE=1 and none of
#                                                     its change of what a failed assertion does
# HALO_BUILD_NUMBER, HALO_UPDATE_REPOSITORY and HALO_UPDATE_PUBLIC_KEY in the environment go to the build (the updater's configuration, which
# is off without the last two: port/wiiu/README.md, "Updates").
# configure.py writes build.ninja in the OpenCE folder, where the big-endian loop (tools/ppc-be) has its own: this puts that
# one back when it is done.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
opence="${OPENCE:-$root}"
release=""
[ "${RELEASE:-0}" = 1 ] && release="--release"
lto="${LTO:-0}"
[ "${RELEASE:-0}" = 1 ] && lto="${LTO:-1}"
[ "$lto" = 1 ] && release="$release --wiiu-lto"
[ "${LEAN:-0}" = 1 ] && export HALO_NO_HUD_SENTINEL=1
docker run --rm -v "$opence:/src" -w /src -e RELEASE_FLAG="$release" \
	-e HALO_BUILD_NUMBER -e HALO_UPDATE_REPOSITORY -e HALO_UPDATE_PUBLIC_KEY -e HALO_NO_HUD_SENTINEL opence-wiiu bash -ec '
	trap "python3 configure.py --linux-target=ppc --lto=off --pgo=off > /dev/null" EXIT
	python3 configure.py --linux-target=wiiu $RELEASE_FLAG > /dev/null
	ninja "${@:-linux_wiiu}"
' bash "$@"
