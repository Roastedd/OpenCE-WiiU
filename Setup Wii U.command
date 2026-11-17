#!/bin/sh
cd "$(dirname "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
    echo "Install Python 3.10 or later from python.org, then run this again."
    read -r answer
    exit 1
fi
python3 setup_wiiu.py "$@"
result=$?
if [ "$result" -ne 0 ]; then
    printf '\nPress Return to close.'
    read -r answer
fi
exit "$result"
