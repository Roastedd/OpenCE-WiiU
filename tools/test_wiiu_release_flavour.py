"""The release flavour's source-level rules (port/wiiu/README.md, "The release build"): what a --release build keeps and leaves out is written in the
sources, so these checks read them. The behaviour itself is proved by the game digest harness (GAME_DIGEST_RELEASE=1: the same frames as the default
build; a mutated save-path assertion halts, a mutated other one is noted and carried on), which needs Docker and the maps."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SAVED_GAMES = ["saved_game_files.c", "game_state.c", "game_state_xbox.c", "player_profile.c", "playlist_profile.c"]


class ReleaseFlavourTest(unittest.TestCase):
    def test_the_saved_games_sources_keep_their_assertions_fatal(self):
        for name in SAVED_GAMES:
            text = (ROOT / "source" / "saved games" / name).read_text(errors="replace")
            define = text.find("#define HALO_ASSERTS_STOP")
            self.assertGreaterEqual(define, 0, name)
            self.assertLess(define, text.find("#include"), f"{name}: before its first include, so that cseries.h sees it")

    def test_the_stop_form_notes_the_failure_and_stops(self):
        text = (ROOT / "source/cseries/cseries.h").read_text()
        stop = text[text.index("#ifdef HALO_ASSERTS_STOP"):text.index("#else", text.index("#ifdef HALO_ASSERTS_STOP"))]
        for macro in ("match_assert", "match_vassert"):
            line = [l for l in stop.splitlines() if l.startswith(f"#define {macro}(")][0]
            self.assertIn("release_assert_failed(", line)
            self.assertIn("system_exit(-1)", line)
            self.assertIn("display_assert_skipped = FALSE", line)

    def test_no_hud_function_fills_its_stack_sentinel_outside_the_macro(self):
        for name in ("hud_draw.c", "hud_weapon.c", "hud_unit.c", "hud_nav_points.c", "motion_sensor.c"):
            text = (ROOT / "source/interface" / name).read_text(errors="replace")
            for match in re.finditer(r"memset\(\s*stack_buffer,\s*0x62", text):
                before = text[max(0, match.start() - 40):match.start()]
                self.assertIn("#ifndef HALO_RELEASE", text[max(0, match.start() - 60):match.start()], f"{name}: {before!r}")

    def test_the_release_sentinel_macros_leave_nothing_to_run(self):
        text = (ROOT / "source/interface/hud_draw.h").read_text()
        release = text[text.index("#ifdef HALO_RELEASE"):text.index("#else", text.index("#ifdef HALO_RELEASE"))]
        self.assertIn("#define hud_stack_sentinel_fill(buffer) ((void)(buffer))", release)
        self.assertIn("#define check_stack_buffer(buffer) ((void)(buffer), (short)NONE)", release)
        self.assertIn("#define get_return_eip() 0L", release)
        self.assertIn("csmemset(buffer, 0x62, sizeof(buffer))", text[text.index("#else", text.index("#ifdef HALO_RELEASE")):])

    def test_debug_stays_defined_for_the_join_token(self):
        # (the network's join token is made #ifndef DEBUG from the build's compile time: a build without -DDEBUG could join no other build's game)
        self.assertIn("-DDEBUG", (ROOT / "tools/test_wiiu_build.py").read_text())
        self.assertIn("#ifndef DEBUG", (ROOT / "source/networking/network_server_manager.c").read_text())


if __name__ == "__main__":
    unittest.main()
