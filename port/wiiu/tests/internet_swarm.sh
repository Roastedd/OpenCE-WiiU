#!/usr/bin/env bash
# Internet play with many machines on one computer: copies of the game's Linux builds under qemu, each in a container of
# its own on Docker's bridge (its own address and ports, as a machine has), in the tools repository's opence-ppc-be image
# (tools/ppc-be/Dockerfile there). Every copy runs headless, its frames held to 30 a second, and logs its net: lines
# (debug.network_statistics, where its build has them).
#
#   internet_swarm.sh host <output> <host build> <joiners> <joiner build> <seconds> [host in process] [joiner in process]
#       a host and its joiners, on this computer alone: they find each other through a local MQTT broker
#       (mqtt_test_broker.py, beside this script), by the host's invite (debug.network_test's host logs it whole), with
#       no STUN (their tunnels meet on the bridge's addresses), and system link's broadcasts sent to a documentation
#       address that reaches nobody, so that the joiners reach the host through the tunnel alone
#   internet_swarm.sh join <output> <invite> <joiners> <joiner build> <seconds> [joiner in process]
#       joiners of another machine's game (a console's) by its invite, through the public brokers (brokers.txt) and STUN,
#       as players on the internet join; behind the Docker machine's NAT
#
# A build is "net-ppc" (OPENCE_NET's build/linux-ppc, big-endian, with DATA_BE's maps), "net-x86" (OPENCE_NET's
# build/linux) or "upstream-x86" (OPENCE_UPSTREAM's build/linux: an official OpenCE build). In process: 1 sets
# network.tunnel_in_process. HOST_ENV and JOINER_ENV add docker options ("-e HALO_NETWORK_CORRUPT=30") to the host's or
# each joiner's container. Defaults suit the tools repository: ROOT (its folder, the current one), OPENCE_NET
# ($ROOT/scratch/opence-net), OPENCE_UPSTREAM ($ROOT/scratch/opence-upstream-x86), DATA ($ROOT/data/maps), DATA_BE
# ($ROOT/data-be/maps). Logs: <output>/<name>.log and <name>.debug.txt; the host's last net: lines in summary.txt.
#
# The host has the cores first (--cpu-shares): many copies under qemu outrun a computer's cores, and a host that falls
# behind its clients reads its sockets too seldom, which tests the computer, not the network.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
mode="$1"
out="$(mkdir -p "$2" && cd "$2" && pwd)"
root="${ROOT:-$PWD}"
opence_net="${OPENCE_NET:-$root/scratch/opence-net}"
opence_upstream="${OPENCE_UPSTREAM:-$root/scratch/opence-upstream-x86}"
data="${DATA:-$root/data/maps}"
data_be="${DATA_BE:-$root/data-be/maps}"
run="swarm$$"
note() { echo "$(date +%H:%M:%S) $*" | tee -a "$out/summary.txt"; }

cleanup() {
	docker ps -q --filter "name=^${run}-" | xargs -r docker kill > /dev/null 2>&1
}
trap cleanup EXIT

# (name, build, seconds, in process, network test, bot seed, local: 1 for the local broker and no STUN, arguments)
machine() {
	local name=$1 build=$2 time=$3 in_process=$4 test=$5 seed=$6 local_network=$7 arguments=${8:-}
	local mount binary emulator libraries maps shares=512 extra=${JOINER_ENV:-} network=()
	case "$build" in
	net-ppc) mount="$opence_net" binary=/game/build/linux-ppc/halo emulator=qemu-ppc libraries=/usr/powerpc-linux-gnu
		maps="$data_be" ;;
	net-x86) mount="$opence_net" binary=/game/build/linux/halo emulator=qemu-i386 libraries=/usr/i686-linux-gnu maps="$data" ;;
	upstream-x86) mount="$opence_upstream" binary=/game/build/linux/halo emulator=qemu-i386 libraries=/usr/i686-linux-gnu
		maps="$data" ;;
	*) echo "unknown build $build"; exit 2 ;;
	esac
	[ "$name" = host ] && shares=8192 extra=${HOST_ENV:-}
	if [ "$local_network" = 1 ]; then
		network=(-e HALO_NET_STUN= -e HALO_NET_BROKERS_FILE=/out/brokers.txt -e HALO_NET_BROADCAST=192.0.2.255)
	else
		network=(-e HALO_NET_BROKERS_FILE=/game/port/assets/network/brokers.txt)
	fi
	# shellcheck disable=SC2086
	docker run -d --rm --name "$run-$name" --cpu-shares "$shares" --ulimit core=0 -v "$mount:/game:ro" -v "$maps:/maps:ro" \
		-v "$out:/out" "${network[@]}" -e HALO_NETWORK_TEST="$test" -e HALO_TEST_INPUT="bot:$seed" \
		-e HALO_NET_TUNNEL_IN_PROCESS="$in_process" -e HALO_NETWORK_STATISTICS=1 -e HALO_NET_ALLOW_UPNP=0 \
		-e HALO_NO_VSYNC=1 -e HALO_MAX_FPS=30 -e HALO_INTERPOLATION=0 \
		-e HALO_NULL_RENDERER=1 -e HALO_NO_AUDIO=1 -e HALO_HIDDEN_WINDOW=1 -e HALO_EXIT_AFTER="$time" \
		-e SDL_VIDEO_DRIVER=offscreen -e SDL_AUDIO_DRIVER=dummy -e QEMU_LD_PREFIX="$libraries" $extra \
		opence-ppc-be bash -c "mkdir -p /run/m/data /run/m/home && ln -s /maps /run/m/data/maps && cd /run/m &&
			HOME=/run/m/home HALO_CONFIG_FILE=/run/m/config.toml HALO_DATA_ROOT=/run/m/data \
			timeout -s KILL $((time + 120)) $emulator $binary $arguments > /out/$name.log 2>&1;
			echo \"exit status \$?\" >> /out/$name.log; cp /run/m/data/debug.txt /out/$name.debug.txt 2>/dev/null" > /dev/null
}

joiners() {
	local count=$1 build=$2 time=$3 in_process=$4 local_network=$5 invite=$6 index
	for index in $(seq 1 "$count"); do
		machine "$(printf 'j%02d' "$index")" "$build" "$time" "$in_process" join $((index + 1)) "$local_network" "$invite"
		sleep 3
	done
}

case "$mode" in
host)
	host_build="$3" count="$4" joiner_build="$5" seconds="$6" host_in_process="${7:-1}" joiner_in_process="${8:-0}"
	docker run -d --rm --name "$run-broker" -v "$here:/tests:ro" -v "$out:/out" opence-ppc-be \
		python3 /tests/mqtt_test_broker.py 1883 /out/broker.log > /dev/null
	sleep 2
	broker_ip="$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$run-broker")"
	echo "$broker_ip:1883" > "$out/brokers.txt"
	note "broker at $broker_ip:1883; host $host_build (in process $host_in_process), $count joiners $joiner_build" \
		"(in process $joiner_in_process), $seconds s"
	machine host "$host_build" "$seconds" "$host_in_process" host:bloodgulch 1 1
	invite=""
	for attempt in $(seq 1 120); do
		invite="$(grep -o 'network test: invite halo://join/[0-9a-f]*' "$out/host.log" 2>/dev/null | head -1 |
			sed 's/network test: invite //')"
		[ -n "$invite" ] && break
		sleep 2
	done
	[ -n "$invite" ] || { note "no invite from the host"; exit 1; }
	note "invite ${invite:0:24}...; the joiners start"
	joiners "$count" "$joiner_build" $((seconds - 40)) "$joiner_in_process" 1 "$invite"
	while docker ps -q --filter "name=^$run-host$" | grep -q .; do sleep 5; done
	note "host done; its last net: lines:"
	grep 'net: ' "$out/host.log" | tail -4 | tee -a "$out/summary.txt"
	;;
join)
	invite="$3" count="$4" joiner_build="$5" seconds="$6" joiner_in_process="${7:-0}"
	note "$count joiners $joiner_build (in process $joiner_in_process) of ${invite:0:24}..., $seconds s"
	joiners "$count" "$joiner_build" "$seconds" "$joiner_in_process" 0 "$invite"
	while docker ps -q --filter "name=^$run-j" | grep -q .; do sleep 5; done
	note "joiners done"
	;;
*)
	sed -n '2,25p' "$0"
	exit 2
	;;
esac
sleep 10
