"""Exercise deliberate shake activation, pulse duration, rearming and noise."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
code=r"""
#include <assert.h>
#include "wiiu_wimote.h"
static const float rest[3]={0,0,1},shake[3]={0,0,2.5f},turn[3]={0.7f,0,0.7141428f};
int main(void){
 struct wimote_shake s={0};
 for(int i=0;i<20;i++)assert(!wimote_shake_melee(&s,0.02f,rest,1));
 for(int i=0;i<10;i++)assert(!wimote_shake_melee(&s,0.02f,turn,1));
 assert(wimote_shake_melee(&s,0.02f,shake,1));
 assert(wimote_shake_melee(&s,0.02f,rest,1));
 for(int i=0;i<40;i++)wimote_shake_melee(&s,0.02f,shake,1);
 assert(!s.ready && s.pulse==0);
 for(int i=0;i<15;i++)assert(!wimote_shake_melee(&s,0.02f,rest,1));
 assert(wimote_shake_melee(&s,0.02f,shake,1));
 assert(!wimote_shake_melee(&s,0.02f,shake,0));
 assert(!wimote_shake_melee(&s,0.02f,shake,1));
 assert(!wimote_shake_melee(&s,2.0f,shake,1));
 for(int i=0;i<20;i++)assert(!wimote_shake_melee(&s,0.02f,rest,1));
 assert(wimote_shake_melee(&s,0.02f,shake,1));
 struct wimote_shake n={0};
 for(int i=0;i<20;i++)assert(!wimote_nunchuk_melee(&n,0.02f,rest,1,2.0f));
 assert(wimote_nunchuk_melee(&n,0.02f,shake,1,2.0f));
 for(int i=0;i<5;i++)wimote_nunchuk_melee(&n,0.02f,rest,1,2.0f);
 assert(!wimote_nunchuk_melee(&n,0.02f,shake,1,2.0f));
 for(int i=0;i<40;i++)wimote_nunchuk_melee(&n,0.02f,rest,1,3.0f);
 assert(!wimote_nunchuk_melee(&n,0.02f,shake,1,3.0f));
 assert(!wimote_nunchuk_melee(&n,0.02f,shake,0,2.0f));
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I',str(root/'port/wiiu/src'),str(p/'test.c'),
                 str(root/'port/wiiu/src/wiiu_wimote.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Shake-to-melee activation, cooldown and noise checks passed')
