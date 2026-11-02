#!/usr/bin/env bash
# One headless b30 co-op host run of the big-endian Linux build under qemu-ppc, to compare the game's ticks with the Broadway kernels'
# scalar models in the kernels' places (HALO_BW_MODEL=1 at configure time, then HALO_BW_KERNELS=1 or 0 for the run) against without:
# the bot timed by ticks (tickbot:7), seed 12345, fixed step, null renderer, no audio (the same run as the ticks study's run_probe.sh).
# The per-tick network-test log is compared between runs with the tools repository's tools/ppc-be/compare_network_tests.py.
#
#   build:  docker run --rm -e HALO_BW_MODEL=1 -v "$PWD:/src" -w /src opence-ppc-be bash -c \
#             'python3 configure.py --linux-target=ppc --lto=off --pgo=off >/dev/null && ninja linux_ppc'
#   run:    BW_MAPS=<a folder with the big-endian maps> port/wiiu/tests/bw_tick_run.sh NAME SECONDS [VAR=VALUE ...]
#
# The run's log is build/bw-runs/NAME.log, its data folder build/bw-runs/NAME-data. BW_BUILD names another build folder (a base build).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
name="$1"; seconds="$2"; shift 2
maps="${BW_MAPS:?BW_MAPS names the folder of big-endian maps (data-be/maps in the tools repository)}"
build="${BW_BUILD:-$root/build/linux-ppc}"
runs="$root/build/bw-runs"
data="$runs/$name-data"
mkdir -p "$runs"
rm -rf "$data"; mkdir -p "$data"; : > "$data/empty-brokers.txt"
extra=()
for setting in "$@"; do extra+=(-e "$setting"); done
docker run --rm --network none --ulimit core=0 \
	-v "$build:/out:ro" -v "$data:/data" -v "$maps:/data/maps:ro" -w /data \
	-e HALO_DATA_ROOT=/data -e HALO_SAVE_ROOT=/data \
	-e 'HALO_NETWORK_TEST=host:levels\b30\b30:coop' -e HALO_NETWORK_TEST_START=2 -e HALO_NETWORK_TEST_LOG=1 \
	-e HALO_TEST_INPUT=tickbot:7 -e HALO_RANDOM_SEED=12345 -e HALO_FIXED_STEP=1 \
	-e HALO_NET_ADDRESS=127.0.0.1 -e HALO_NET_BROADCAST=127.0.0.1 -e HALO_NET_BROKERS_FILE=/data/empty-brokers.txt \
	-e HALO_NETWORK_TEST_PUBLIC=0 -e HALO_NULL_RENDERER=1 -e HALO_NO_AUDIO=1 -e HALO_HIDDEN_WINDOW=1 \
	-e HALO_EXIT_AFTER="$seconds" -e SDL_VIDEO_DRIVER=offscreen -e SDL_AUDIO_DRIVER=dummy \
	${extra[@]+"${extra[@]}"} opence-ppc-be timeout -s KILL $((seconds + 300)) qemu-ppc -L /usr/powerpc-linux-gnu /out/halo --config /data/config.toml \
	> "$runs/$name.log" 2>&1
echo "$name: $(grep -c 'network test: tick' "$runs/$name.log") tick lines, last: $(grep 'network test: tick' "$runs/$name.log" | tail -1 | cut -c1-60)"
