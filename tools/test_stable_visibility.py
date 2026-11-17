from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'port/linux/game/render_governor.c').read_text()
a=s.index('int render_governor_stable_visibility(void)');b=s.index('struct render_governor_values const *render_governor_values(void)',a)
c=s.index('float render_governor_lod_scale(void)');d=s.index('\n}',c)+2
fixture=r'''
#include <assert.h>
#include <math.h>
#define HALO_PORT_MAXIMUM_OBJECTS_PER_MAP 8192
static unsigned char cull_bits[1024];
static unsigned long revision=1;static int enabled=1;
unsigned long config_changes(void){return revision;}
int config_boolean(const char*n){(void)n;return enabled;}
struct render_governor_values {float lod_scale;};
static struct render_governor_values v={.25f};
const struct render_governor_values*render_governor_values(void){return &v;}
'''
main=r'''
int main(void){
 assert(render_governor_protect_near(13*13,1));
 assert(!render_governor_protect_near(14*14,1));
 assert(!render_governor_cull_small(1,20,42));
 assert(render_governor_cull_small(1,10,42));
 assert(!render_governor_cull_small(1,10,0));
 assert(render_governor_lod_scale()==.7f);
 enabled=0;revision++;
 assert(!render_governor_protect_near(1,1));
 assert(render_governor_cull_small(1,20,42));
 assert(render_governor_lod_scale()==.25f);
 return 0;}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture+s[a:b]+s[c:d]+main)
 subprocess.run(['cc','-Wall','-Wextra','-Werror',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Near-object protection, cull-history clearing, LOD floor and opt-out passed')
