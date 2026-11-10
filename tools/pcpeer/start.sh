#!/bin/sh
# Starts a PC peer detached from this shell: tools/pcpeer/start.sh <data dir> <name> <console port> [extra pcpeer arguments]
# The log is <data dir>/../<name>.log and the process id is written to <data dir>/../<name>.pid. A new data dir is a new identity.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DIR=$1; NAME=$2; PORT=$3; shift 3
PROGRAM=${PCPEER:-$ROOT/build/pcpeer/pcpeer}
mkdir -p "$DIR"
BASE=$(cd "$DIR/.." && pwd)
perl -MPOSIX -e 'POSIX::setsid(); open(STDIN, "<", "/dev/null"); open(STDOUT, ">>", $ARGV[0]); open(STDERR, ">&STDOUT"); exec @ARGV[1..$#ARGV]' \
	"$BASE/$NAME.log" "$PROGRAM" --dir "$DIR" --name "$NAME" --console "$PORT" "$@" &
sleep 1
pgrep -f "pcpeer --dir $DIR " | head -1 > "$BASE/$NAME.pid"
cat "$BASE/$NAME.pid"
