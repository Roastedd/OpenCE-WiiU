#!/bin/sh
# Double-click this file on a Mac. If macOS says it "cannot be opened because it is from an unidentified developer",
# Control-click (or right-click) the file, choose Open, then Open again; macOS asks only once.
cd "$(dirname "$0")" || exit 1
if ! python3 -c 'import sys; raise SystemExit(sys.version_info < (3, 10))' >/dev/null 2>&1; then
    echo ""
    echo "This needs Python 3.10 or newer, and this Mac does not have it yet."
    echo "1. Download the macOS installer from https://www.python.org/downloads/"
    echo "2. Open it and click through (the defaults are fine)."
    echo "3. Double-click this file again."
    echo ""
    printf "Press Return to close."
    read -r answer
    exit 1
fi
python3 setup_wiiu.py "$@"
result=$?
if [ "$result" -ne 0 ]; then
    printf '\nSomething stopped the setup (see the message above). Press Return to close.'
    read -r answer
fi
exit "$result"
