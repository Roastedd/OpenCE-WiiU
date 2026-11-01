"""Actual Wii U setters: last-sent values, direct prewrites and sparse marks."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from harness import function

PRELUDE = r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t DWORD;
typedef int BOOL;
typedef unsigned D3DRENDERSTATETYPE, D3DTEXTURESTAGESTATETYPE;
#define WINAPI
#define D3DFASTCALL
#define TRUE 1
#define FALSE 0
#define D3DRS_MAX 144
#define D3DRS_TEXTUREFACTOR 128
#define D3DTSS_MAXSTAGES 4
#define D3DTSS_MAX 32
#define D3DTSS_TEXCOORDINDEX 28
#define D3DTSS_BORDERCOLOR 29
#define D3DTSS_COLORKEYCOLOR 30
static DWORD D3D__RenderState[D3DRS_MAX], render_states_told[D3DRS_MAX];
static DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX];
static DWORD texture_states_told[D3DTSS_MAXSTAGES][D3DTSS_MAX];
static uint32_t render_states_dirty[(D3DRS_MAX+31)/32];
static uint32_t texture_states_dirty[D3DTSS_MAXSTAGES];
static BOOL states_told_valid, render_states_all, simple_states_made;
#define SIMPLE_METHOD_BASE 0x4000u
#define SIMPLE_METHOD_SLOTS 8
#define D3DRS_SIMPLE_MAX 60
static unsigned long simple_states[SIMPLE_METHOD_SLOTS];
static DWORD D3DSIMPLERENDERSTATEENCODE[D3DRS_MAX];
static void simple_states_make(void) {simple_states[0]=3;D3DSIMPLERENDERSTATEENCODE[2]=SIMPLE_METHOD_BASE;simple_states_made=1;}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed: %s line %d\n",#x,__LINE__);return 99;}}while(0)
'''
DRIVER = r'''
static void clean(void) {
 memset(render_states_dirty,0,sizeof(render_states_dirty));
 memset(texture_states_dirty,0,sizeof(texture_states_dirty));
 states_told_valid=1;
}
int main(void) {
 unsigned i,stage,type;uint32_t seed=0x750;
 void (*render)(D3DRENDERSTATETYPE,DWORD)=D3DDevice_SetRenderState_Deferred;
 void (*texture)(DWORD,D3DTEXTURESTAGESTATETYPE,DWORD)=D3DDevice_SetTextureState_Deferred;
 for(i=0;i<20000;i++) {
  unsigned state=i%D3DRS_MAX;uint32_t before,value;
  seed=seed*1664525u+1013904223u;before=seed;value=(i%3)?before:(before^0x80000001u);
  clean();render_states_told[state]=before;
  /* A direct game-table prewrite must not hide an unsent state change. */
  D3D__RenderState[state]=(i%5)?before:value;
  render(state,value);CHECK(D3D__RenderState[state]==value);
  CHECK(render_states_told[state]==before);
  CHECK(!!(render_states_dirty[state/32]&(1u<<(state%32)))==(before!=value));
  stage=i%4;type=(i/4)%32;clean();texture_states_told[stage][type]=before;
  D3D__TextureState[stage][type]=(i%5)?before:value;
  texture(stage,type,value);CHECK(D3D__TextureState[stage][type]==value);
  CHECK(texture_states_told[stage][type]==before);
  CHECK(!!(texture_states_dirty[stage]&(1u<<type))==(before!=value));
 }
 /* Restoring the last-sent value must also restore a directly changed getter. */
 clean();render_states_told[128]=7;D3D__RenderState[128]=9;
 D3DDevice_SetRenderState_TextureFactor(7);CHECK(D3D__RenderState[128]==7);CHECK(!render_states_dirty[4]);
 clean();texture_states_told[2][28]=7;D3D__TextureState[2][28]=9;
 D3DDevice_SetTextureState_TexCoordIndex(2,7);CHECK(D3D__TextureState[2][28]==7);CHECK(!texture_states_dirty[2]);
 clean();texture_states_told[1][29]=0;D3D__TextureState[1][29]=0xffffffffu;
 D3DDevice_SetTextureState_BorderColor(1,0);CHECK(!D3D__TextureState[1][29]);CHECK(!texture_states_dirty[1]);
 clean();texture_states_told[3][30]=1;D3D__TextureState[3][30]=0;
 D3DDevice_SetTextureState_ColorKeyColor(3,1);CHECK(D3D__TextureState[3][30]==1);CHECK(!texture_states_dirty[3]);
 clean();texture_states_told[2][21]=2;D3DDevice_SetTextureState_BumpEnv(2,21,3);CHECK(texture_states_dirty[2]&(1u<<21));
 /* Simple does not store: raw mismatch retains its old table-marking semantics. */
 clean();render_states_told[2]=7;D3D__RenderState[2]=9;
 D3DDevice_SetRenderState_Simple(SIMPLE_METHOD_BASE,7);CHECK(D3D__RenderState[2]==9);CHECK(render_states_dirty[0]&(1u<<2));
 clean();render_states_told[2]=7;D3D__RenderState[2]=7;
 D3DDevice_SetRenderState_Simple(SIMPLE_METHOD_BASE,7);CHECK(!render_states_dirty[0]);
 clean();render_states_told[2]=7;D3D__RenderState[2]=7;
 D3DDevice_SetRenderState_Simple(SIMPLE_METHOD_BASE,9);D3D__RenderState[2]=9;CHECK(render_states_dirty[0]&(1u<<2));CHECK(D3D__RenderState[2]==9);
 clean();render_states_told[2]=7;D3D__RenderState[2]=9;
 D3DDevice_SetRenderState_Simple(SIMPLE_METHOD_BASE,9);CHECK(render_states_dirty[0]&(1u<<2));
 clean();render_states_all=0;D3DDevice_SetRenderState_Simple(SIMPLE_METHOD_BASE+4,7);CHECK(render_states_all);
 clean();render_states_all=0;D3DDevice_SetRenderState_Simple(0,7);CHECK(render_states_all);
 clean();states_told_valid=0;render_states_told[2]=0;D3D__RenderState[2]=0;
 D3DDevice_SetRenderState_Simple(SIMPLE_METHOD_BASE,0);CHECK(render_states_dirty[0]&(1u<<2));
 /* Unknown consumer state still marks equal zeroes for the initial resync. */
 clean();states_told_valid=0;render_states_told[0]=0;D3DDevice_SetRenderState_Deferred(0,0);CHECK(render_states_dirty[0]&1);
 clean();states_told_valid=0;texture_states_told[0][0]=0;D3DDevice_SetTextureState_Deferred(0,0,0);CHECK(texture_states_dirty[0]&1);
 /* A no-op setter must not erase a previously pending mark. */
 clean();render_states_told[3]=1;D3DDevice_SetRenderState_Deferred(3,2);D3DDevice_SetRenderState_Deferred(3,1);CHECK(render_states_dirty[0]&(1u<<3));
 clean();D3DDevice_SetRenderState_Deferred(144,9);D3DDevice_SetRenderState_Deferred(0xffffffffu,9);
 D3DDevice_SetTextureState_Deferred(4,0,9);D3DDevice_SetTextureState_Deferred(0,32,9);
 D3DDevice_SetTextureState_Deferred(0xffffffffu,0xffffffffu,9);
 for(i=0;i<5;i++)CHECK(!render_states_dirty[i]);for(i=0;i<4;i++)CHECK(!texture_states_dirty[i]);
 puts("setter contract: 20000 prewrite/value cases and boundary cases passed");return 0;
}
'''


def generated(control=None):
    source=(ROOT/'port/wiiu/src/d3d8_gx2.c').read_text(encoding='latin-1')
    names=['render_state_mark','texture_state_mark','render_state_store','texture_state_store',
           'D3DDevice_SetRenderState_Simple','D3DDevice_SetRenderState_Deferred','D3DDevice_SetTextureState_Deferred',
           'D3DDevice_SetTextureState_TexCoordIndex','D3DDevice_SetTextureState_BorderColor',
           'D3DDevice_SetTextureState_ColorKeyColor','D3DDevice_SetTextureState_BumpEnv']
    code='\n'.join(function(source,name) for name in names)
    macro=next(line for line in source.splitlines() if 'void WINAPI D3DDevice_SetRenderState_##name' in line)
    code+='\n#define COMPLEX_RENDER_STATE(name,state) '+macro.strip()+'\nCOMPLEX_RENDER_STATE(TextureFactor,D3DRS_TEXTUREFACTOR)\n'
    if control=='current-table':
        code=code.replace('render_states_told[state] != value','D3D__RenderState[state] != value').replace('texture_states_told[stage][type] != value','D3D__TextureState[stage][type] != value')
    elif control=='skip-restoration':
        code=code.replace('D3D__RenderState[state] = value;','if (!states_told_valid || render_states_told[state] != value) D3D__RenderState[state] = value;')
    elif control=='simple-raw-mismatch':
        code=code.replace(' || D3D__RenderState[state - 1] != value','')
    elif control=='lost-initial-mark':
        code=code.replace('!states_told_valid || ','')
    return PRELUDE+code+DRIVER


def execute(control=None,sanitize=False):
    with tempfile.TemporaryDirectory(prefix='opence-state-setters-') as directory:
        directory=Path(directory);source=directory/'test.c';source.write_text(generated(control));exe=directory/'test'
        target=shlex.split(os.environ.get('STATE_SETTERS_TARGET',''));runner=shlex.split(os.environ.get('STATE_SETTERS_RUNNER',''))
        compiler=os.environ.get('CC',shutil.which('clang') or 'cc')
        flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off']
        if target:flags+=['-fuse-ld=lld','-static']
        if sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        result=subprocess.run([compiler,*target,*flags,str(source),'-o',str(exe)],capture_output=True,text=True)
        if result.returncode:raise AssertionError(result.stdout+result.stderr)
        return subprocess.run([*runner,str(exe)],capture_output=True,text=True)


class StateSetterTests(unittest.TestCase):
    def test_actual_setter_contract(self):
        result=execute();self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    @unittest.skipIf(os.environ.get('STATE_SETTERS_TARGET'),'Host sanitizer only')
    def test_host_sanitizers(self):
        result=execute(sanitize=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_deliberate_faults(self):
        for control in ['current-table','skip-restoration','lost-initial-mark','simple-raw-mismatch']:
            with self.subTest(control=control):
                result=execute(control);self.assertEqual(result.returncode,99,result.stdout+result.stderr)


if __name__=='__main__':unittest.main()
