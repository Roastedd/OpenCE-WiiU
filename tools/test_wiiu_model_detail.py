#!/usr/bin/env python3
"""Exercise production Wii U detail configuration and governor composition."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'port/wiiu/src/d3d8_gx2.c').read_text()
a=s.index('static int model_detail_setting(void)'); b=s.index('/* port/linux/game/render_governor.c',a)
c=s.index('float halo_model_detail_scale(int level)'); d=s.index('\nvoid d3d8_gl_set_height_fade',c)
code='''#include <assert.h>
#include <string.h>
static unsigned long revision; static int config_reads; static const char *setting="stock";
static float screen_scale[2]={1,1.5f},governor=1;
static unsigned long config_changes(void){config_reads++;return revision;}
static const char *config_string(const char *key){(void)key;return setting;}
static int halo_screen_width(void){return 854;}
static float render_governor_lod_scale(void){return governor;}
'''+s[a:b]+s[c:d]+'''
int main(void){
 int reads=config_reads;assert(halo_model_detail_scale(0)==1 && config_reads==reads);
 assert(halo_model_detail_stock()); assert(halo_model_detail_scale(1)==1);
 governor=0.5f;assert(halo_model_detail_scale(1)==0.5f);
 setting="native";revision++;assert(!halo_model_detail_stock());assert(halo_model_detail_scale(1)==0.75f);
 assert(halo_model_detail_scale(0)==1);
 screen_scale[1]=0.5f;assert(halo_model_detail_scale(1)==0.5f);
 setting="highest";revision++;assert(halo_model_detail_scale(1)==500000);
 assert(halo_model_detail_scale(0)==1);
 setting="unknown";revision++;assert(halo_model_detail_stock());assert(halo_model_detail_scale(1)==0.5f);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Wii U detail settings, fallback, visibility distance and governor composition passed')
