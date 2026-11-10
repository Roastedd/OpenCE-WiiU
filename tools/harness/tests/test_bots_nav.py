"""The bots' navigation graph (port/linux/game/bots_nav.c, compiled as it is): the pack's bytes (a record round trip, the pack's index and crc,
bad ones refused), a point's area, the search over the areas (the best way, a link a bot found bad avoided, the nearest the graph goes to a
goal it cannot reach, the jitter), a path's points and the places."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["crc", "round-trip", "pack", "bad-record", "find-area", "find-areas", "search", "unreachable", "jitter", "points", "places"]

# a fault in bots_nav.c, and the case that must catch it
NEGATIVE_CONTROLS = {
    "crc-no-final-xor": (("return ~crc & 0xFFFFFFFFUL;", "return crc & 0xFFFFFFFFUL;"), "crc"),
    "portal-not-relative": (("l->exit[0] += a->x;", "l->exit[0] += 0.0f;"), "round-trip"),
    "entry-relative-to-the-wrong-area": (("l->entry[0] += b->x;", "l->entry[0] += a->x;"), "round-trip"),
    "radius-scale": (("a->radius = (float)nav_u16(bytes + at + 16) / 16.0f;", "a->radius = (float)nav_u16(bytes + at + 16) / 8.0f;"),
                     "round-trip"),
    "pack-crc-skipped": (("if (bots_nav_crc32(0, bytes + BOTS_NAV_PACK_HEADER_BYTES, count - BOTS_NAV_PACK_HEADER_BYTES) != nav_u32(bytes + 20))",
                          "if (0)"), "pack"),
    "version-unchecked": (("if (nav_u16(bytes + 8) != BOTS_NAV_PACK_VERSION || nav_u16(bytes + 10) != BOTS_NAV_FORMAT)", "if (0)"), "pack"),
    "name-case-sensitive": (("entry_name[k] = (char)(entry_name[k] - 'A' + 'a');", "entry_name[k] = entry_name[k];"), "pack"),
    "link-to-unchecked": (("if (l->to >= areas || l->type", "if (l->type"), "bad-record"),
    "table-size-unchecked": (("if (count < need)", "if (need > 0x7FFFFFFFUL)"), "bad-record"),
    "far-floor-is-the-area": (("fabs(dz) > 1.2f + 0.15f * flat", "fabs(dz) > 100.0f"), "find-area"),
    "areas-not-sorted": (("for (slot = count; slot > 0 && scores[slot - 1] > score; slot--)", "for (slot = count; slot > 0 && 0; slot--)"), "find-areas"),
    "areas-over-the-limit": (("if (slot < maximum)\n\t\t{\n\t\t\tscores[slot] = score;", "{\n\t\t\tscores[slot] = score;"), "find-areas"),
    "bad-link-taken": (("if (bad[k] == link_index)", "if (0)"), "search"),
    "blocked-link-taken": (("if (s->link_blocked && s->link_blocked[link_index] >= 2)", "if (0)"), "search"),
    "one-failure-blocks": (("if (s->link_blocked && s->link_blocked[link_index] >= 2)", "if (s->link_blocked && s->link_blocked[link_index] >= 1)"), "search"),
    "blocked-links-not-kept": (("search->link_blocked = (unsigned char *)calloc((size_t)nav->link_count + 1, 1);\n\tif (!search->link_blocked ||", "search->link_blocked = NULL;\n\tif ("), "search"),
    "cost-ignored": (("cost = (float)l->cost_q / 16.0f;", "cost = 1.0f;"), "search"),
    "partial-claims-complete": (("path->complete = end == to;", "path->complete = 1;"), "unreachable"),
    "jitter-ignored": (("if (jitter_seed)\n					cost *=", "if (0)\n					cost *="), "jitter"),
    "points-skip-anchor": (("return path->valid ? 3L * path->link_count + 2 : 0;", "return path->valid ? 2L * path->link_count + 2 : 0;"),
                           "points"),
    "entry-kind-lost": (("point->kind = l->type;", "point->kind = _bots_nav_link_walk;"), "points"),
    "place-type-ignored": (("if (nav->places[index].type == type)", "if (1)"), "places"),
}


def generated(fault=None):
    code = read("port/linux/game/bots_nav.c")
    if fault:
        code = mutated(code, *fault)
    return (("bots_nav.h", read("port/linux/game/bots_nav.h")), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_nav", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_nav", generated(fault)), case)
    assert status == CHECK_FAILED, f"the navigation graph with a fault ({control}) passed '{case}': the test cannot see it"
