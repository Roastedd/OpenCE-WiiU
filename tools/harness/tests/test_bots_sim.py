"""The headless bots run (tools/harness/bots_sim.py): the parser of the game's bots-sim block and the fingerprint two runs
are compared by, on canned log text; and a short game played for real when a runner can play here (build/linux/halo with
assets/maps, or tools/ppc-be's build and image with data-be/maps): the bots play, the block is whole, the counts are the
game's, and a second run of the same input is identical."""

import shutil
import sys
import tempfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import bots_sim  # noqa: E402

PLAYER_LINE = ("halo-linux: network test: tick {tick} player 0: ({x} -80.0 0.1) h1.00/1.00 g0/0 w a1/0 f1 l1/0 as1/1 st4 thr0.75 "
               "501:0 s0 k0 d0 f0 t0 m0 player 1: ({x} -81.0 0.1) h1.00/1.00 g0/0 w a1/0 f1 l1/0 as1/1 st4 thr0.75 "
               "501:0 s1 k2 d1 f0 t1 m113\n")


def block(bots=2, ticks=900, ms="18.295", think="60/1023/6576", end=True):
    text = f"halo-linux: bots-sim: begin (bots.sim_ticks) map bloodgulch ticks {ticks} bots {bots} ms_per_tick {ms}\n"
    for index in range(1, bots + 1):
        text += (f"halo-linux: bots-sim: bot {index} normal frags {index} deaths 1 stuck_s {index - 1} giveups 0 searches "
                 f"{10 * index} failed {index - 1} short 2 rays 143 think_us {think}\n")
    total = bots * (bots + 1) // 2
    text += (f"halo-linux: bots-sim: total frags {total} deaths {bots} stuck_s {bots * (bots - 1) // 2} searches "
             f"{10 * total} failed {bots * (bots - 1) // 2} short {2 * bots} rays {143 * bots}\n")
    text += "halo-linux: bots-sim: tick_us mean 305 p99 4095 max 10036 ticks 900 (the bots' think time)\n"
    if end:
        text += "halo-linux: bots-sim: end\n"
    return text


def log(x="30.5", **arguments):
    return PLAYER_LINE.format(tick=30, x=x) + PLAYER_LINE.format(tick=60, x=x) + block(**arguments)


def test_the_block_is_read():
    parsed = bots_sim.parse(log())
    assert parsed["begin"] == {"why": "bots.sim_ticks", "map": "bloodgulch", "ticks": 900, "bots": 2, "ms": 18.295}
    assert parsed["bots"][2] == {"skill": "normal", "frags": 2, "deaths": 1, "stuck": 1, "giveups": 0, "searches": 20,
                                 "failed": 1, "short": 2, "rays": 143, "mean": 60, "p99": 1023, "max": 6576}
    assert parsed["total"]["frags"] == 3 and parsed["total"]["failed"] == 1
    assert parsed["tick_us"] == {"mean": 305, "p99": 4095, "max": 10036, "ticks": 900}
    # the game's own per-second lines: all the players, a bot's machine at 113
    assert parsed["log_players"][1] == {"kills": 2, "deaths": 1, "machine": 113}


def test_the_summary_has_the_baseline_numbers():
    summary = bots_sim.summary(bots_sim.parse(log()), wall_seconds=19.04)
    assert summary["frags"] == 3 and summary["deaths"] == 2 and summary["stuck_s"] == 1
    assert summary["path_failures"] == 1 and summary["path_short"] == 4
    assert summary["bot_think_us"] == {"mean": 305, "p99": 4095, "max": 10036}
    assert summary["ms_per_tick"] == 18.295 and summary["wall_seconds"] == 19.0
    # one stuck second of 2 bots over 30 seconds
    assert summary["stuck_share"] == round(1 / 60.0, 4)


def test_a_block_that_is_not_whole_is_refused():
    with pytest.raises(ValueError, match="no whole bots-sim block"):
        bots_sim.parse(log(end=False))
    with pytest.raises(ValueError, match="no whole bots-sim block"):
        bots_sim.parse("halo-linux: nothing here\n")
    # a bot line lost
    text = log().replace("bots-sim: bot 2 normal", "bots-sim: lost 2 normal")
    with pytest.raises(ValueError, match="1 bot lines for 2 bots"):
        bots_sim.parse(text)


def test_the_last_block_is_the_run():
    # a game that ended before the run's end said a block of its own
    text = block(bots=1, ticks=100) + log(bots=2)
    assert bots_sim.parse(text)["begin"]["bots"] == 2


def test_two_runs_match_whatever_the_times():
    first = log(think="60/1023/6576", ms="18.295")
    second = log(think="99/2047/9000", ms="21.5").replace("mean 305 p99 4095 max 10036", "mean 77 p99 255 max 500")
    assert bots_sim.fingerprint(first) == bots_sim.fingerprint(second)
    assert bots_sim.differences(first, second) == []


def test_a_dead_players_camera_is_not_a_part_of_the_game():
    # the camera of a dead player follows the wall clock
    first = log().replace("m0 player 1", "m0 | camera (39.0 -71.0 0.2) respawn 67 player 1")
    second = log().replace("m0 player 1", "m0 | camera (39.0 -70.9 0.2) respawn 67 player 1")
    assert bots_sim.differences(first, second) == []
    assert bots_sim.differences(first, first.replace("respawn 67", "respawn 66"))


def test_a_build_without_the_block_is_compared_by_the_lines_both_have():
    older = PLAYER_LINE.format(tick=30, x="30.5") + PLAYER_LINE.format(tick=60, x="30.5")
    assert bots_sim.differences(older, log(), prefix=True) == []
    # (but not without the prefix: the block is in one and not the other)
    assert bots_sim.differences(older, log())
    # an older run that stops sooner is the shorter prefix; a different position in it is still a difference
    assert bots_sim.differences(older, log(x="31.0"), prefix=True)


def test_two_runs_differ_by_a_position_or_a_count():
    first = log(x="30.5")
    moved = log(x="30.6")
    found = bots_sim.differences(first, moved)
    assert found and "30.5" in found[0][1] and "30.6" in found[0][2]
    assert bots_sim.fingerprint(first) != bots_sim.fingerprint(moved)
    counted = first.replace("bots-sim: total frags 3", "bots-sim: total frags 4")
    assert bots_sim.differences(first, counted)


# ---------- a game played for real

RUNNER = bots_sim.default_runner()
needs_game = pytest.mark.skipif(RUNNER is None, reason="needs build/linux/halo with assets/maps, or tools/ppc-be's build and "
                                "image with data-be/maps")


@pytest.fixture
def work():
    """a folder for a run: under the game's own build folder (the container the ppc-be runner plays in mounts only /Users)"""
    folder = Path(tempfile.mkdtemp(prefix="bots-sim-", dir=bots_sim.REPOSITORY / "build"))
    yield folder
    shutil.rmtree(folder, ignore_errors=True)


@needs_game
def test_a_short_game_with_bots(work):
    first, text = bots_sim.run("bloodgulch", "slayer", 3, "normal", 450, 7, RUNNER, work / "a")
    assert first["bots"] == 3 and first["ticks"] == 450 and first["map"].endswith("bloodgulch")
    assert set(first["per_bot"]) == {"1", "2", "3"} and all(bot["skill"] == "normal" for bot in first["per_bot"].values())
    # the bots think (a clock read), cast sight tests and search for paths, and the counts agree with the game's own lines
    assert first["bot_think_us"]["max"] > 0 and first["sight_tests"] > 0 and first["path_searches"] > 0
    parsed = bots_sim.parse(text)
    in_game = {index: player for index, player in parsed["log_players"].items() if player["machine"] >= bots_sim.BOTS_FIRST_MACHINE}
    assert len(in_game) == 3
    # (the game's last per-second line is at most a second before the last tick)
    assert abs(sum(player["kills"] for player in in_game.values()) - first["frags"]) <= 3
    assert first["path_failures"] <= first["path_searches"] and first["stuck_s"] <= 3 * 450 // 30
    # the same input again: the same game (the times aside). The scripted human's first ticks are the one thing a machine
    # running many games at once can change (its poll against the game's first frames), so a run that differs is played a
    # third time and one of the two must agree with the first
    second, again = bots_sim.run("bloodgulch", "slayer", 3, "normal", 450, 7, RUNNER, work / "b")
    if bots_sim.differences(text, again):
        second, again = bots_sim.run("bloodgulch", "slayer", 3, "normal", 450, 7, RUNNER, work / "b2")
    assert bots_sim.differences(text, again) == [], "two runs of the same input played different games"
    assert first["fingerprint"] == second["fingerprint"]
    # another seed: another game
    third, other = bots_sim.run("bloodgulch", "slayer", 3, "normal", 450, 8, RUNNER, work / "c")
    assert third["fingerprint"] != first["fingerprint"]
