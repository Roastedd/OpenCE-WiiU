"""The bots' think-time counters (port/linux/game/bots_stats.c, compiled as it is): mean, longest and percentiles of the
microseconds a bot takes a tick, and the sum of two sets."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["empty", "mean-and-longest", "negative", "percentile", "huge", "add"]

# a fault in bots_stats.c, and the case that must catch it
NEGATIVE_CONTROLS = {
    "mean-truncated": (("(stats->total_microseconds + (unsigned long long)stats->samples / 2)",
                        "(stats->total_microseconds)"), "mean-and-longest"),
    "negative-time-wraps": (("\tif (microseconds < 0)\n\t\tmicroseconds = 0;\n", ""), "negative"),
    "longest-not-kept": (("\tif (microseconds > stats->maximum_microseconds)\n\t\tstats->maximum_microseconds = microseconds;\n", ""),
                         "mean-and-longest"),
    "percentile-past-the-longest": (("return top < stats->maximum_microseconds ? top : stats->maximum_microseconds;",
                                     "return top;"), "percentile"),
    "percentile-rounds-down": (("needed = (stats->samples * per_cent + 99) / 100;", "needed = stats->samples * per_cent / 100;"),
                               "percentile"),
    "sum-forgets-the-longest": (("\tif (other->maximum_microseconds > total->maximum_microseconds)\n"
                                 "\t\ttotal->maximum_microseconds = other->maximum_microseconds;\n", ""), "add"),
    "last-bucket-missing": (("bucket < BOTS_THINK_BUCKETS - 1)", "bucket < BOTS_THINK_BUCKETS + 8)"), "huge"),
}


def generated(fault=None):
    code = read("port/linux/game/bots_stats.c")
    if fault:
        code = mutated(code, *fault)
    return (("bots_stats.h", read("port/linux/game/bots_stats.h")), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_stats", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_stats", generated(fault)), case)
    assert status == CHECK_FAILED, f"the counters with a fault ({control}) passed '{case}': the test cannot see it"
