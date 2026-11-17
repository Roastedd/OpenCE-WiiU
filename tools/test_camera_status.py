"""Exercise the production camera notice's replacement, expiry and view isolation."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'port/linux/game/camera_status.c').read_text()
source='\n'.join(l for l in source.splitlines() if not l.startswith('#include'))
fixture=r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef float real;
typedef uint32_t pixel32;
typedef struct {short y0,x0,y1,x1;} rectangle2d;
typedef struct {float alpha,red,green,blue;} real_argb_color;
#define MAXIMUM_LOCAL_PLAYERS 4
#define NONE -1
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
struct font_header {short ascending_height,descending_height,leading_height;};
static struct font_header font_data={12,3,1};
static unsigned long clock_ms;
static int menu,cinema,quads,strings;
static char last_text[64];
static struct {short local_player_index;struct {rectangle2d window_bounds,viewport_bounds;} camera;} render;
unsigned long system_milliseconds(void){return clock_ms;}
void draw_quad(rectangle2d *r,pixel32 c){(void)c;assert(r->x0>=0&&r->x1<=640&&r->y0>=0&&r->y1<=480);quads++;}
static int cinematic_in_progress(void){return cinema;}
static int ui_widgets_active(void){return menu;}
static long hud_get_font_index(void){return 0;}
static struct font_header *font_definition_get(long n){(void)n;return &font_data;}
static void offset_rectangle2d(rectangle2d *r,int x,int y){r->x0+=x;r->x1+=x;r->y0+=y;r->y1+=y;}
static void draw_string_set_draw_mode(long f,int s,int j,int flags,const real_argb_color *c){(void)f;(void)s;(void)j;(void)flags;assert(c->alpha>=0&&c->alpha<=1);}
static void rasterizer_draw_string(rectangle2d *r,void *a,void *b,int c,const char *s){(void)r;(void)a;(void)b;(void)c;strcpy(last_text,s);strings++;}
"""
main=r"""
int main(void){
 render.camera.window_bounds=render.camera.viewport_bounds=(rectangle2d){0,0,480,640};
 camera_status_reset();camera_status_draw();assert(!strings);
 camera_status_post(0,4,-1);camera_status_draw();assert(!strcmp(last_text,"FIRST PERSON")&&strings==1&&quads==2);
 clock_ms=100;camera_status_post(0,0,-1);camera_status_draw();assert(!strcmp(last_text,"THIRD PERSON"));
 clock_ms=2200;camera_status_draw();assert(strings==3);
 clock_ms=2600;camera_status_draw();assert(strings==3);
 camera_status_post(1,2,1);camera_status_draw();assert(strings==3);
 render.local_player_index=1;camera_status_draw();assert(strings==5&&!strcmp(last_text,"Camera controls on"));
 menu=1;camera_status_draw();assert(strings==5);
 menu=0;camera_status_reset();camera_status_draw();assert(strings==5);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text(fixture+source+main)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Camera notice timing, replacement and per-view checks passed')
