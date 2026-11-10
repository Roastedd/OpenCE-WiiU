"""The pictures of the social screens (tools/screens_preview.py): the TV's and the GamePad's, at their sizes, with something in them; and the GamePad's social view's text kept in its places with the
real glyphs (social_view_render.c exits 2 on an overflow)."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))


@pytest.mark.skipif(shutil.which("cc") is None, reason="needs a C compiler")
def test_pictures_are_made(tmp_path):
    PIL = pytest.importorskip("PIL")
    from PIL import Image
    import screens_preview
    paths = screens_preview.render_all(tmp_path, {"players", "friends", "lobby", "social-friends", "social-bots", "social-toast-confirm"})
    assert len(paths) == 2 * 3 + 3
    for path in paths:
        image = Image.open(path)
        expected = screens_preview.TV if path.name.endswith("-tv.png") else screens_preview.GAMEPAD
        assert image.size == expected, path.name
        colors = image.convert("RGB").getcolors(maxcolors=1 << 20)
        assert colors is None or len(colors) > 30, f"{path.name} is nearly blank"


def test_longest_names_are_drawn_whole_in_the_rows():
    pytest.importorskip("PIL")
    import screens_preview
    data = screens_preview.dump("players", 5, 0)
    names = [row["cells"][0]["text"] for row in data["rows"]]
    assert "WWWWWWWWWWW" in names and "%@%@%@%@%@%" in names, "the made-up players have the widest names"
    friends = screens_preview.dump("friends", 0)
    assert "WWWWWWWWWWW" in [row["cells"][1]["text"] for row in friends["rows"]]


def test_a_tab_or_button_with_no_words_is_refused():
    """the Friends screen's four tabs showed nothing on the console: the preview now refuses a text widget with a bitmap (a tab, a button) that is drawn empty"""
    pytest.importorskip("PIL")
    import screens_preview
    menus = screens_preview.Menus()
    screen = f"{screens_preview.port_settings.SOCIAL}/friends_screen"
    data = screens_preview.dump("friends", 0)
    state = screens_preview.screen_state_friends(data)
    screens_preview.render_screen(menus, screens_preview.GAMEPAD, screen, state)  # (every tab and button has words)
    assert all(label.strip() for label in data["tabs"]) and len(data["tabs"]) == 4
    for tab in range(4):
        broken = dict(state, texts={key: value for key, value in state["texts"].items() if key != ("", f"friends_tab_{tab}")})
        with pytest.raises(screens_preview.SilentButtons) as caught:
            screens_preview.render_screen(menus, screens_preview.GAMEPAD, screen, broken)
        assert caught.value.names == [f"friends_tab_{tab}"]
    # a button whose words are blank
    texts = dict(state["texts"])
    texts[("", "friends_tab_1")] = "  "
    with pytest.raises(screens_preview.SilentButtons):
        screens_preview.render_screen(menus, screens_preview.GAMEPAD, screen, dict(state, texts=texts))
