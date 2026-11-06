"""port/linux/src/http_date.h: an HTTP Date header's time in Unix seconds, against Python's calendar over a range of dates, and refusing what is not one."""
import calendar
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = r'''
#include <stdio.h>
#include "http_date.h"
int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
		printf("%.0f\n", http_date_parse(argv[i]));
	return 0;
}
'''


def parse(texts):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "t.c"
        source.write_text(PROGRAM)
        binary = Path(directory) / "t"
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "port/linux/src"), str(source), "-o", str(binary)], check=True)
        out = subprocess.run([str(binary), *texts], check=True, capture_output=True, text=True).stdout.split()
    return [int(value) for value in out]


class HttpDateTests(unittest.TestCase):
    def test_dates_match_the_calendar(self):
        names = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
        months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
        cases = [(2026, 11, 6, 19, 9, 11), (2000, 1, 1, 0, 0, 0), (2024, 2, 29, 23, 59, 59), (2026, 3, 1, 0, 0, 0), (2100, 12, 31, 12, 0, 0),
                 (2028, 2, 29, 6, 30, 0), (2026, 2, 28, 6, 30, 0)]
        texts = []
        for year, month, day, hour, minute, second in cases:
            weekday = calendar.weekday(year, month, day)
            texts.append(f"{names[weekday]}, {day:02d} {months[month - 1]} {year} {hour:02d}:{minute:02d}:{second:02d} GMT")
        expected = [calendar.timegm(case + (0, 0, 0)) for case in cases]
        self.assertEqual(parse(texts), expected)

    def test_what_is_not_a_date_is_0(self):
        self.assertEqual(parse(["", "yesterday", "Fri, 32 Nov 2026 19:09:11 GMT", "Fri, 06 Xyz 2026 19:09:11 GMT", "Fri, 06 Nov 1999 19:09:11 GMT",
                                "Fri, 06 Nov 2026 25:09:11 GMT", "06 Nov 2026 19:09:11"]), [0] * 7)

    def test_the_masters_own_header(self):
        # (an answer of master.opencommunityedition.com, 2026-11-06 19:09:11 UTC, the time the Mac's `date -u` said)
        self.assertEqual(parse(["Fri, 06 Nov 2026 19:09:11 GMT"]), [1793992151])


if __name__ == "__main__":
    unittest.main()
