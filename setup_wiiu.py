#!/usr/bin/env python3
"""OpenCE-WiiU's local ISO-to-SD-card setup helper (Python 3.10 or later)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "tools/wiiu"))
from setup_assistant import main

if __name__ == "__main__":
    raise SystemExit(main())
