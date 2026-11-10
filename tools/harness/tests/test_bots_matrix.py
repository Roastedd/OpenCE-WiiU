"""tools/harness/bots_matrix.py: the modes' environment and the table of stuck shares added up over seeds (the games themselves are bots_sim.py's, played in
test_bots_sim.py)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bots_matrix as matrix  # noqa: E402


def result(stuck, bots=6, ticks=3600, frags=10, short=2, searches=100):
    return {"stuck_s": stuck, "bots": bots, "ticks": ticks, "stuck_share": round(stuck / (bots * ticks / 30.0), 4), "frags": frags, "path_short": short,
            "path_searches": searches}


def test_modes():
    assert matrix.mode_environment("legacy") == {"HALO_BOTS_NAV": "0"}
    nav = matrix.mode_environment("nav")
    assert nav["HALO_BOTS_NAV"] == "1" and nav["HALO_BOTS_NAV_SYNC"] == "1" and nav["HALO_BOTS_NAV_PACK"] == "/out/navs.pack"
    # (a named mode adds to the nav mode's environment and overrides it)
    both = matrix.mode_environment("t3=HALO_BOTS_NAV_TWEAKS=3,HALO_BOTS_NAV_SYNC=0")
    assert both["HALO_BOTS_NAV_TWEAKS"] == "3" and both["HALO_BOTS_NAV_SYNC"] == "0" and both["HALO_BOTS_NAV"] == "1"
    # (the table's copies are not shared)
    matrix.mode_environment("nav")["HALO_BOTS_NAV"] = "x"
    assert matrix.MODES["nav"]["HALO_BOTS_NAV"] == "1"
    with pytest.raises(ValueError):
        matrix.mode_environment("what")


def test_table_adds_the_seeds_up():
    jobs = [({"map": "bloodgulch", "mode": "legacy"}, result(17)), ({"map": "bloodgulch", "mode": "legacy"}, result(19)),
            ({"map": "bloodgulch", "mode": "nav"}, result(33)), ({"map": "bloodgulch", "mode": "nav"}, result(27)),
            ({"map": "chillout", "mode": "nav"}, result(10))]
    rows = matrix.table(jobs)
    assert [(r[0], r[1], r[2]) for r in rows] == [("bloodgulch", "legacy", 2), ("bloodgulch", "nav", 2), ("chillout", "nav", 1)]
    legacy, nav = rows[0], rows[1]
    # (36 stuck seconds of 2 x 720 bot-seconds; the least and the most of the seeds)
    assert legacy[3] == 36 and legacy[4] == 1440 and abs(legacy[5] - 0.025) < 1e-9
    assert nav[3] == 60 and abs(nav[5] - 60 / 1440) < 1e-9 and nav[6] < nav[7]
    text = matrix.format_table(rows)
    assert "bloodgulch" in text and "2.50%" in text and "4.17%" in text
