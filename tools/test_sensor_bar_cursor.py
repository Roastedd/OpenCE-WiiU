"""Check IR coordinates and the production menu cursor's tracking-loss behavior."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'port/wiiu/src/wiiu_input.c').read_text()
a=source.index('int wiiu_input_remote_cursor(')
b=source.index('/* The controller on a KPAD channel',a)
fixture=r"""
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include "wiiu_wimote.h"
typedef uint64_t OSTime;
static OSTime now;
#define OSMillisecondsToTicks(value) (value)
#define WIIU_PAD_CHANNELS 4
static struct {int present;} last_pro[4];
static struct {int remote,source;OSTime pointer_seen;float pointer_x,pointer_y;} remotes[4];
static struct {float x,y,moved;} remote_cursor;
static int pad_players;
static OSTime OSGetSystemTime(void) {return now;}
static int wiiu_pad_players_port(int *p,int ch) {(void)p;return ch;}
"""
main=r"""
int main(void) {
 float x=-1,y=-1;int moved;
 assert(!wimote_pointer_cursor(0,0,0,&x,&y) && x==-1);
 assert(wimote_pointer_cursor(1,0,0,&x,&y) && x==0.5f && y==0.5f);
 assert(wimote_pointer_cursor(1,-1,1,&x,&y) && x==0 && y==1);
 assert(wimote_pointer_cursor(1,2,-2,&x,&y) && x==1 && y==0);
 assert(!wimote_pointer_cursor(1,NAN,0,&x,&y));
 assert(!wimote_pointer_cursor(1,0,INFINITY,&x,&y));
 last_pro[0].present=remotes[0].remote=1;
 remotes[0].source=WIIMOTE_SOURCE_POINTER;
 remotes[0].pointer_seen=100;
 remotes[0].pointer_x=0.2f;remotes[0].pointer_y=0.8f;
 now=110;assert(wiiu_input_remote_cursor(&x,&y,&moved) && moved && x==0.2f && y==0.8f);
 now=240;assert(wiiu_input_remote_cursor(&x,&y,&moved) && !moved);
 now=251;assert(!wiiu_input_remote_cursor(&x,&y,&moved));
 remotes[0].source=WIIMOTE_SOURCE_MOTION;remote_cursor.moved=0.01f;
 assert(wiiu_input_remote_cursor(&x,&y,&moved) && moved);
 last_pro[0].present=0;
 assert(!wiiu_input_remote_cursor(&x,&y,&moved));
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text(fixture+source[a:b]+main)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I',str(root/'port/wiiu/src'),str(p/'test.c'),
                 str(root/'port/wiiu/src/wiiu_wimote.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Sensor-bar cursor mapping and tracking-loss checks passed')
