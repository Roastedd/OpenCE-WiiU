"""The actual vector traversal's tail-return contract and fault controls."""
import subprocess
import sys
from pathlib import Path


def test_collision_tail_contract():
    script = Path(__file__).resolve().parents[2] / 'test_collision_tail.py'
    subprocess.run([sys.executable, str(script), '--fault-controls'], check=True)
