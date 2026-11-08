#!/usr/bin/env bash
# A host and a joining client of a system link game in one opence-ppc-be container, each its own build (ppc, the big-endian one, or
# x86), maps, data folder, home and loopback address, under its emulator: tools/ppc-be/two_machines.sh of the Wii U port's
# repository, made to run from this checkout (both builds in it: build/linux-ppc/halo, build/linux/halo).
#
#   tools/harness/xplay_two_machines.sh <host arch> <client arch> <seconds> <output folder>
#
# MAPS_PPC and MAPS_X86: the folders of the converted maps (the big-endian build plays those) and of the Xbox disc's. HALO_HOST_TEST is
# the host's debug.network_test (host:bloodgulch by default; host:bloodgulch:ctf, host:levels\a10\a10:coop); HALO_TEST_INPUT,
# HALO_RANDOM_SEED, HALO_FIXED_STEP, HALO_NETWORK_TEST_START, _KILL, _SCORE, _SHOOT, _VEHICLE, _PICKUP and _PICKUP_WEAPON are passed to
# both. Each machine's log (its standard output with the network test's lines) and debug.txt go to <output folder>/{host,client}.*;
# tools/ppc-be/compare_network_tests.py pairs two such logs. Needs the retail maps (a local check, not a public CI one).
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
host_arch="$1"
client_arch="$2"
seconds="$3"
output="$(mkdir -p "$4" && cd "$4" && pwd)"
maps_ppc="${MAPS_PPC:?the converted maps folder (data-be/maps)}"
maps_x86="${MAPS_X86:?the Xbox maps folder (data/maps)}"

environment=()
for name in HALO_HOST_TEST HALO_TEST_INPUT HALO_RANDOM_SEED HALO_FIXED_STEP HALO_NETWORK_TEST_START HALO_NETWORK_TEST_KILL \
	HALO_NETWORK_TEST_SCORE HALO_NETWORK_TEST_SHOOT HALO_NETWORK_TEST_VEHICLE HALO_NETWORK_TEST_PICKUP \
	HALO_NETWORK_TEST_PICKUP_WEAPON; do
	[ -n "${!name:-}" ] && environment+=(-e "$name=${!name}")
done

docker run --rm --ulimit core=0 \
	-v "$root:/ppc:ro" -v "$root:/x86:ro" \
	-v "$maps_ppc:/maps-ppc:ro" -v "$maps_x86:/maps-x86:ro" -v "$output:/out" \
	${environment[@]+"${environment[@]}"} opence-ppc-be bash -c '
		machine() {
			# (role, arch, address, the other machine'"'"'s address)
			local role=$1 arch=$2 address=$3 other=$4 binary emulator libraries
			if [ "$arch" = x86 ]; then
				binary=/x86/build/linux/halo emulator=qemu-i386 libraries=/usr/i686-linux-gnu
			else
				binary=/ppc/build/linux-ppc/halo emulator=qemu-ppc libraries=/usr/powerpc-linux-gnu
			fi
			mkdir -p /run/$role/data /run/$role/home
			ln -s /maps-$arch /run/$role/data/maps
			local test=join
			[ $role = host ] && test="${HALO_HOST_TEST:-host:bloodgulch}"
			(cd /run/$role && HOME=/run/$role/home QEMU_LD_PREFIX=$libraries HALO_DATA_ROOT=/run/$role/data \
				HALO_NETWORK_TEST="$test" HALO_NET_ADDRESS=$address HALO_NET_BROADCAST=$other \
				HALO_NULL_RENDERER=1 HALO_NO_AUDIO=1 HALO_HIDDEN_WINDOW=1 \
				SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
				timeout -s KILL '"$seconds"' $emulator $binary > /out/$role.log 2>&1;
				cp /run/$role/data/debug.txt /out/$role.debug.txt 2>/dev/null || true) &
		}
		machine host '"$host_arch"' 127.0.0.200 127.0.0.201
		sleep 5
		machine client '"$client_arch"' 127.0.0.201 127.0.0.200
		wait
	'
echo "host ($host_arch) and client ($client_arch) logs in $output"
