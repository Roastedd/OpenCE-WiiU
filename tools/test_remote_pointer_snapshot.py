"""Check coherent IR coordinates and explicit tracking-loss ownership."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'port/wiiu/src/wiiu_input.c').read_text();a=s.index('int wiiu_input_remote_pointer(');b=s.index('int wiiu_input_remote_cursor(',a)
fixture=r"""
#include <assert.h>
#include <stdlib.h>
#include "wiiu_wimote.h"
#define WIIU_PAD_CHANNELS 4
#define OSTicksToMilliseconds(x) (x)
static unsigned now;
static unsigned OSGetSystemTime(void){return now;}
static struct {int present;} last_pro[4];
static struct {int remote,source;unsigned pointer_seen;} remotes[4];
static struct {int look;} wimote_settings;
static unsigned remote_pointer_xy[4],remote_pointer_valid[4],remote_pointer_ms[4];
static int prompt_owner[4],pad_players,ports[4]={0,1,2,3};
static int wiiu_pad_players_port(int *p,int ch){(void)p;return ports[ch];}
"""
main=r"""
int main(void){
 float x,y;int valid;
 last_pro[0].present=remotes[0].remote=1;remotes[0].source=WIIMOTE_SOURCE_POINTER;
 remote_pointer_xy[0]=65535;remote_pointer_ms[0]=900;remote_pointer_valid[0]=1;now=950;
 assert(wiiu_input_remote_pointer(&x,&y,&valid) && valid && x==-1 && y==1);
 remote_pointer_valid[0]=0;assert(wiiu_input_remote_pointer(&x,&y,&valid) && !valid);
 remote_pointer_valid[0]=1;now=1001;assert(wiiu_input_remote_pointer(&x,&y,&valid) && !valid);
 remotes[0].source=WIIMOTE_SOURCE_NONE;remotes[0].pointer_seen=900;wimote_settings.look=WIIMOTE_LOOK_AUTO;
 assert(wiiu_input_remote_pointer(&x,&y,&valid) && !valid);
 wimote_settings.look=WIIMOTE_LOOK_OFF;assert(!wiiu_input_remote_pointer(&x,&y,&valid));
 remotes[0].source=WIIMOTE_SOURCE_POINTER;last_pro[0].present=0;
 assert(!wiiu_input_remote_pointer(&x,&y,&valid));
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text(fixture+s[a:b]+main)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I',str(root/'port/wiiu/src'),
                 str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('IR snapshot, stale sample and tracking-loss ownership checks passed')
