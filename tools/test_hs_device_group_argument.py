"""Test the actual script adapter with 32-bit argument slots on both byte orders."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "source/hs/hs.c").read_text(encoding="latin1")
start = source.index("#define HS_EVALUATE_REAL_FROM_UNSIGNED_SHORT(")
end = source.index("\n#define ", start + 1)
adapter = source[start:end]
start = source.index("union hs_evaluation_argument\n{")
end = source.index("\n};", start) + 3
argument = source[start:end]
fixture = r"""
#include <stdint.h>
#include <string.h>
typedef unsigned char boolean;
typedef unsigned char byte;
typedef unsigned short word;
typedef float real;
_Static_assert(sizeof(long)==4, "The script ABI uses 32-bit slots");
"""
fixture += argument + r"""
union hs_real_value { real real_value; long long_value; };
static long slot;
static int available=1;
static int calls, returns;
static unsigned short observed;
static long returned;
static long *hs_macro_function_evaluate(short f,long t,boolean init) {
 (void)f;(void)t;(void)init; return available ? &slot : 0;
}
static real device_group_get_value(unsigned short group) {
 calls++; observed=group; return group==7 ? 1.0f : 0.0f;
}
static void hs_return(long thread,long value) { (void)thread; returns++; returned=value; }
"""
main = r"""
HS_EVALUATE_REAL_FROM_UNSIGNED_SHORT(device_group_get_value_evaluate,device_group_get_value)
int main(void) {
 const unsigned short groups[]={0,1,7,26,32768,65535};
 for (unsigned i=0;i<sizeof(groups)/sizeof(groups[0]);i++) {
  for (unsigned high=0;high<2;high++) {
   slot=(long)((high ? 0xabcd0000u : 0) | groups[i]);
   device_group_get_value_evaluate(0,0,0);
   if(observed!=groups[i]) return 1;
   union hs_real_value result;
   result.long_value=returned;
   if(result.real_value!=(groups[i]==7 ? 1.0f : 0.0f)) return 2;
  }
 }
 available=0; calls=returns=0;
 device_group_get_value_evaluate(0,0,1);
 return calls || returns;
}
"""
parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--runner", required=True)
parser.add_argument("--big-endian", action="store_true")
args = parser.parse_args()
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    def run(name, macro):
        code, executable = folder / (name + ".c"), folder / name
        code.write_text(fixture + macro + main)
        command = shlex.split(args.cc) + ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-strict-aliasing"]
        if args.big_endian:
            command += ["-D__BIG_ENDIAN__"]
        subprocess.run(command + [str(code), "-o", str(executable)], check=True)
        return subprocess.run(shlex.split(args.runner) + [str(executable)]).returncode
    assert run("fixed", adapter) == 0
    if args.big_endian:
        old = adapter.replace(
            "union hs_evaluation_argument const *arguments = (union hs_evaluation_argument const *)",
            "unsigned short *arguments = (unsigned short *)"
        ).replace("arguments[0].unsigned_short_value", "arguments[0]")
        assert run("old", old) == 1, "Old adapter must read the wrong device group"
print("Device-group argument and return checks passed")
