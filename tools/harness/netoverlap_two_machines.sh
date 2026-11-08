#!/usr/bin/env bash
# A host and a joining client of a system link game in one opence-ppc-be container, on the loopback, each its own program under its emulator:
# xplay_two_machines.sh's, with a third kind of machine, the Wii U game digest's program (the real game with the Wii U's Direct3D layer, drawing,
# and the tick overlap, port/wiiu/tests/game_digest), so that a joined client (or a host) can run its ticks beside its drawing and be held to the
# other machine's game by tools/net_compare.py.
#
#   tools/harness/netoverlap_two_machines.sh <host spec> <client spec> <seconds> <output folder>
#
# A spec is x86 (build/linux/halo of $X86_TREE, qemu-i386, null renderer), ppc (build/linux-ppc/halo of $PPC_TREE, qemu-ppc, null renderer)
# or digest:<folder> (the folder of a game digest program, build/game-digest/trees/NAME/build/game-digest; qemu-ppc -cpu 750, drawing).
#
# HOST_ENV and CLIENT_ENV: "NAME=VALUE NAME=VALUE" for that machine only (HALO_NET_OVERLAP=1, HALO_TICK_OVERLAP_JITTER=3, ...). HALO_HOST_TEST is the
# host's debug.network_test (host:bloodgulch by default); HALO_TEST_INPUT, HALO_RANDOM_SEED, HALO_NETWORK_TEST_START, _KILL, _SCORE, _SHOOT,
# _VEHICLE, _PICKUP and _PICKUP_WEAPON go to both. MAPS_PPC and MAPS_X86 are the folders of the converted maps and of the Xbox disc's. DIGEST_TICKS
# is the tick a digest machine ends its run at (the glue's GAME_DIGEST_TICKS; default 100000: the seconds' timeout ends it). NETEM_*: not used
# here (the loopback is not shaped); the harness's jitter is the game's own debug.network_latency / _loss (HOST_ENV/CLIENT_ENV HALO_NETWORK_LATENCY...).
# Each machine's log (its standard error with the game's lines and the network test's) and debug.txt go to <output folder>/{host,client}.*, and a
# digest machine's frames.txt and summary.txt to <output folder>/<role>-digest/. Needs the retail maps (a local check, not a public CI one).
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
host_spec="$1"
client_spec="$2"
seconds="$3"
output="$(mkdir -p "$4" && cd "$4" && pwd)"
maps_ppc="${MAPS_PPC:?the converted maps folder (data-be/maps)}"
maps_x86="${MAPS_X86:?the Xbox maps folder (data/maps)}"
x86_tree="${X86_TREE:-$root}"
ppc_tree="${PPC_TREE:-$root}"
digest_ticks="${DIGEST_TICKS:-100000}"

environment=()
for name in HALO_HOST_TEST HALO_TEST_INPUT HALO_RANDOM_SEED HALO_NETWORK_TEST_START HALO_NETWORK_TEST_KILL \
	HALO_NETWORK_TEST_SCORE HALO_NETWORK_TEST_SHOOT HALO_NETWORK_TEST_VEHICLE HALO_NETWORK_TEST_PICKUP \
	HALO_NETWORK_TEST_PICKUP_WEAPON HOST_ENV CLIENT_ENV; do
	[ -n "${!name:-}" ] && environment+=(-e "$name=${!name}")
done
mounts=(-v "$x86_tree:/x86:ro" -v "$ppc_tree:/ppc:ro" -v "$maps_ppc:/maps-ppc:ro" -v "$maps_x86:/maps-x86:ro" -v "$output:/out")
host_digest="" client_digest=""
case "$host_spec" in digest:*) host_digest=/digest-host; mounts+=(-v "$(cd "${host_spec#digest:}" && pwd):/digest-host:ro");; esac
case "$client_spec" in digest:*) client_digest=/digest-client; mounts+=(-v "$(cd "${client_spec#digest:}" && pwd):/digest-client:ro");; esac

docker run --rm --ulimit core=0 "${mounts[@]}" ${environment[@]+"${environment[@]}"} \
	-e "HOST_SPEC=$host_spec" -e "CLIENT_SPEC=$client_spec" -e "HOST_DIGEST=$host_digest" -e "CLIENT_DIGEST=$client_digest" \
	-e "DIGEST_TICKS=$digest_ticks" opence-ppc-be bash -c '
		machine() {
			# (role, spec, address, the other machine'"'"'s address, its digest mount)
			local role=$1 spec=$2 address=$3 other=$4 digest=$5 binary emulator libraries extra="" arch kind
			mkdir -p /run/$role/data /run/$role/home
			case $spec in
				x86) kind=linux arch=x86 binary=/x86/build/linux/halo emulator="qemu-i386" libraries=/usr/i686-linux-gnu;;
				ppc) kind=linux arch=ppc binary=/ppc/build/linux-ppc/halo emulator="qemu-ppc" libraries=/usr/powerpc-linux-gnu;;
				digest:*) kind=digest arch=ppc binary=$digest/halo emulator="qemu-ppc -cpu 750" libraries=/usr/powerpc-linux-gnu;;
			esac
			ln -s /maps-$arch /run/$role/data/maps
			local test=join
			[ $role = host ] && test="${HALO_HOST_TEST:-host:bloodgulch}"
			local role_env=$CLIENT_ENV
			[ $role = host ] && role_env=$HOST_ENV
			local env_args=()
			for pair in $role_env; do env_args+=("$pair"); done
			if [ $kind = digest ]; then
				mkdir -p /out/$role-digest
				touch /run/$role/data/empty-brokers.txt
				env_args+=(HALO_NET_BROKERS_FILE=/run/$role/data/empty-brokers.txt HALO_NETWORK_TEST_PUBLIC=0 GAME_DIGEST_DIR=/out/$role-digest
					GAME_DIGEST_TICKS=$DIGEST_TICKS GAME_DIGEST_REAL_CLOCK=1 GAME_DIGEST_TRACE=0 GAME_DIGEST_DETAIL= GAME_DIGEST_RANDOM=0 HALO_DATA_ROOT=/run/$role/data
					HALO_SAVE_ROOT=/run/$role/data)
				# (the digest machine draws: the null renderer is the Linux builds only)
				extra="--config /run/$role/data/config.toml"
			else
				env_args+=(HALO_NULL_RENDERER=1 HALO_DATA_ROOT=/run/$role/data)
			fi
			(cd /run/$role && env HOME=/run/$role/home QEMU_LD_PREFIX=$libraries HALO_NETWORK_TEST="$test" HALO_NET_ADDRESS=$address \
				HALO_NET_BROADCAST=$other HALO_NO_AUDIO=1 HALO_HIDDEN_WINDOW=1 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
				"${env_args[@]}" \
				timeout -s KILL '"$seconds"' $emulator $binary $extra > /out/$role.log 2>&1;
				cp /run/$role/data/debug.txt /out/$role.debug.txt 2>/dev/null || true) &
		}
		machine host "$HOST_SPEC" 127.0.0.200 127.0.0.201 "$HOST_DIGEST"
		sleep 5
		machine client "$CLIENT_SPEC" 127.0.0.201 127.0.0.200 "$CLIENT_DIGEST"
		wait
	'
echo "host ($host_spec) and client ($client_spec) logs in $output"
