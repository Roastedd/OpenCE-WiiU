#!/usr/bin/env python3
"""Reject a Wii U link in which a weak reference has no definition, or the game's objects use newlib's wide strings.

The builds mark every MSVC inline function `#pragma weak` (tools/linux_msvc_semantics.py). A reference to such a name
that no object defines stays a weak undefined symbol, which the linker silently resolves to address 0: this lists them
(tools/linux_link_check.py's check, on the Wii U's objects).

The game's wchar_t is 16 bits (UTF-16 strings in tag data and saved games), and newlib's wide-character functions
(wcslen, mbstowcs, swprintf, ...) assume 32. The port defines its own (port/linux/src/msvc_wide.c); this rejects an
object that calls one that no object defines, which would link to newlib's and misread the strings.

    wiiu_link_check.py <response file listing the objects> [anything after is ignored]
"""

import re
import shlex
import sys

from linux_link_check import RUNTIME_WEAK_REFERENCES, symbol_table

WIDE = re.compile(r"^(wcs|wmem|[a-z_]*wprintf|[a-z_]*wscanf|towlower|towupper|isw|btowc|wctob|mbrtowc|mbtowc|mbstowcs|"
                  r"mbsrtowcs|mbsnrtowcs|wcrtomb|wctomb|wctype|wctrans|fgetw|fputw|getwc|putwc|ungetwc)")


def main() -> int:
    with open(sys.argv[1], "r", encoding="utf-8") as response:
        objects = [path for line in response for path in shlex.split(line)]
    symbols = symbol_table(objects)
    defined_types = set("TDBRCVW")
    weak = sorted(name for name, types in symbols.items()
                  if types & {"w", "v"} and not types & defined_types and name not in RUNTIME_WEAK_REFERENCES)
    wide = sorted(name for name, types in symbols.items() if types == {"U"} and WIDE.match(name))
    if weak:
        print(f"{len(weak)} weak reference(s) that no object defines (they would silently resolve to address 0):")
        for name in weak:
            print(f"  {name}")
    if wide:
        print("the objects call newlib's wide character functions, which assume a 32-bit wchar_t: " + ", ".join(wide))
    return 1 if weak or wide else 0


if __name__ == "__main__":
    sys.exit(main())
