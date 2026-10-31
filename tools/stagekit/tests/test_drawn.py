"""scenarios/drawn.c: the obstacle's wall, its model a box of one part in the map's model data."""

import re

PLAY = dict(seconds=30)
FACE = 1.0 - 0.25


def test_its_model_is_converted(play):
    """the part's eight vertices compressed and given buffers (custom_edition_geometry.c)"""
    assert re.search(r"custom edition: 1 model parts converted \(8 vertices compressed", play("drawn").debug)


def test_never_in_or_past_the_wall(play):
    assert max(x for x, y, _ in play("drawn").places() if abs(y) < 10.0) <= FACE + 1e-3
