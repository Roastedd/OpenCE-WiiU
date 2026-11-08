#!/usr/bin/env bash
# One scenario of the network overlap's comparison, played twice at the same time on the same host machine so that both see the same load: arm A with the overlap off
# (the serial loop, as a build without halo/net_overlap.txt plays), arm B with it on. The client is the Wii U digest's program of $DIGEST_TREE (build/game-digest/trees/NAME);
# the host an x86 Linux build ($X86_TREE) unless HOST_SPEC says otherwise.
#
#   tools/harness/netoverlap_pair.sh <scenario> <rep> <seconds> [extra environment for the client of both arms: "NAME=VALUE ..."]
#
# scenario: plain (Blood Gulch, slayer, bots), events (shots every 4 s and a kill every 17), ctf (Blood Gulch CTF with the events); stress and hostrole are events with the environment
# the caller sets (CLIENT_ENV_BASE, HOST_ENV, ARM_B_ENV, ARM_ROLE=host, HOST_SPEC, CLIENT_SPEC, NET_OVERLAP). The folders are
# build/netov/<scenario>-A<rep> and -B<rep>; tools/harness/netoverlap_report.py reads them. ARM_B_ENV: more environment for the client of arm B only (HALO_NET_OVERLAP=1 is always in it);
# ARM_A_ENV the same for arm A.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
scenario="$1"; rep="$2"; seconds="$3"; extra="${4:-}"
R="$(cd "$root/../.." && pwd)"
export MAPS_PPC="${MAPS_PPC:-$R/data-be/maps}" MAPS_X86="${MAPS_X86:-$R/data/maps}" X86_TREE="${X86_TREE:-$R/scratch/opence-xplay}"
export HALO_TEST_INPUT="${HALO_TEST_INPUT:-bot:7}" HALO_RANDOM_SEED="${HALO_RANDOM_SEED:-11}" HALO_NETWORK_TEST_START="${HALO_NETWORK_TEST_START:-15}"
digest="$root/build/game-digest/trees/${DIGEST_TREE:-net5}/build/game-digest"
host_spec="${HOST_SPEC:-x86}"
client_spec="${CLIENT_SPEC:-digest:$digest}"
case "$scenario" in
	plain) ;;
	events|stress|hostrole) export HALO_NETWORK_TEST_SHOOT=4 HALO_NETWORK_TEST_KILL=17;;
	ctf) export HALO_NETWORK_TEST_SHOOT=4 HALO_NETWORK_TEST_KILL=17 HALO_HOST_TEST="host:bloodgulch:ctf";;
	*) echo "unknown scenario $scenario" >&2; exit 2;;
esac
export HOST_ENV="${HOST_ENV:-} HALO_NET_CLIENT_TIMEOUT_SECONDS=300"
base_client="${CLIENT_ENV_BASE:-} $extra"
for arm in A B; do
	out="$root/build/netov/$scenario-$arm$rep"
	rm -rf "$out"; mkdir -p "$out"
	if [ $arm = B ]; then arm_env="${ARM_B_ENV:-} HALO_NET_OVERLAP=${NET_OVERLAP:-1}"; else arm_env="${ARM_A_ENV:-}"; fi
	# (ARM_ROLE=host: the arm's environment is the host's, for the digest program as the host; else the client's)
	if [ "${ARM_ROLE:-client}" = host ]; then
		( HOST_ENV="$HOST_ENV $arm_env" CLIENT_ENV="$base_client" "$root/tools/harness/netoverlap_two_machines.sh" "$host_spec" "$client_spec" "$seconds" "$out" > "$out.run.txt" 2>&1 ) &
	else
		( CLIENT_ENV="$base_client $arm_env" "$root/tools/harness/netoverlap_two_machines.sh" "$host_spec" "$client_spec" "$seconds" "$out" > "$out.run.txt" 2>&1 ) &
	fi
	# (SEQUENTIAL=1: one arm at a time, for a scenario that is too much for the emulated client when two play at once: the Colima machine has 6 CPUs)
	[ "${SEQUENTIAL:-0}" = 1 ] && wait
done
wait
echo "$scenario rep $rep done"
