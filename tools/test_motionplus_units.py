"""Check the production KPAD conversion with realistic rotations-per-second input."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "port/wiiu/src/wiiu_input.c").read_text()
start = source.index("const float rate[3] = { status->mplus.acc.x")
conversion = source[start:source.index(";", start) + 1]
cursor_start = source.index("cursor_turn = turn;")
cursor_end = source.index("/* held still for a second", cursor_start)
cursor_code = source[cursor_start:cursor_end]
fixture = r"""
#include <math.h>
#include <assert.h>
#include "wiiu_wimote.h"
struct vector { float x,y,z; };
struct sample { struct { struct vector acc; } mplus; };
int main(void) {
 struct sample input = { .mplus.acc = { -0.25f, 0, -0.25f } };
 struct sample *status = &input;
 CONVERSION
 const float gravity[3] = {0,0,1};
 struct wimote_motion motion;
 float turn,tilt;
 wimote_motion_reset(&motion);
 wimote_motion_step(&motion,WIIU_GYRO_SPACE_TURN,0.1f,gravity,rate,0,&turn,&tilt);
 /* 90 degrees/s for 0.1 second must produce 9 degrees. */
 assert(fabsf(turn-0.025f)<0.00001f && fabsf(tilt+0.025f)<0.00001f);
 for(int i=0;i<30;i++) assert(!wimote_motion_still(&motion,0.1f,rate));

 /* Gameplay edge turning must not slide a stationary menu cursor. */
 {
  struct { struct wimote_motion motion; } remotes[1];
  struct { int edge; float edge_box, edge_range, edge_speed; } wimote_settings={1,0.05f,0.1f,1.0f};
  struct { float turn,pitch; } remote_mouse={0,0};
  struct { float x,y,moved; } remote_cursor={0.5f,0.5f,0};
  unsigned channel=0;
  int owns=1, remote_free_aim_selected=0;
  float seconds=0.1f, cursor_turn, cursor_tilt;
  wimote_motion_reset(&remotes[0].motion);
  remotes[0].motion.edge_x=0.1f;
  turn=tilt=0;
  CURSOR
  assert(remote_mouse.turn>0);
  assert(remote_cursor.x==0.5f && remote_cursor.y==0.5f && remote_cursor.moved==0);
 }
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    for label, statement in (("fixed", conversion), ("old", conversion.replace(" * -360.0f", ""))):
        (path / "test.c").write_text(fixture.replace("CONVERSION", statement).replace("CURSOR", cursor_code))
        subprocess.run(["cc", "-std=c99", "-O2", "-I", str(root / "port/wiiu/src"),
                        str(path / "test.c"), str(root / "port/wiiu/src/wiiu_wimote.c"),
                        "-lm", "-o", str(path / label)], check=True)
        result = subprocess.run([str(path / label)], stderr=subprocess.DEVNULL)
        assert (result.returncode == 0) == (label == "fixed")
print("MotionPlus unit conversion passed; old conversion failed as expected")
