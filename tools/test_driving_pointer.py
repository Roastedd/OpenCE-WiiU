from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'port/linux/game/driving_pointer.c').read_text();s='\n'.join(x for x in s.splitlines() if not x.startswith('#include'))
fixture=r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#define NONE -1
#define MAX(a,b) ((a)>(b)?(a):(b))
#define PIN(v,a,b) ((v)<(a)?(a):(v)>(b)?(b):(v))
typedef uint32_t pixel32;
typedef struct {short x0,y0,x1,y1;} rectangle2d;
typedef struct {float alpha,red,green,blue;} real_argb_color;
static struct {short local_player_index;struct {rectangle2d window_bounds,viewport_bounds;} camera;} render;
static int menu,cinema,active=1,valid=1,quads,texts;static float px,py;
int ui_widgets_active(void){return menu;}int cinematic_in_progress(void){return cinema;}
int remote_driving_pointer(float*x,float*y,int*v){*x=px;*y=py;*v=valid;return active;}
void remote_driving_pointer_layout(float*x,float*y,float*o){*x=.15f;*y=.2f;*o=.75f;}
void offset_rectangle2d(rectangle2d*r,short x,short y){r->x0+=x;r->x1+=x;r->y0+=y;r->y1+=y;}
void draw_quad(rectangle2d*r,pixel32 c){(void)c;assert(r->x0>=0 && r->y0>=0 && r->x1<=640 && r->y1<=480 && r->x1>r->x0 && r->y1>r->y0);quads++;}
long hud_get_font_index(void){return 1;}
void draw_string_set_draw_mode(long a,int b,int c,int d,real_argb_color*e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void rasterizer_draw_string(rectangle2d*r,void*a,void*b,int c,const char*s){(void)r;(void)a;(void)b;(void)c;(void)s;texts++;}
'''
main=r'''
int main(void){render.camera.window_bounds=(rectangle2d){0,0,640,480};
for(int i=-2;i<=2;i++)for(int j=-2;j<=2;j++){px=i;py=j;quads=0;driving_pointer_draw();assert(quads==12);}
valid=0;driving_pointer_draw();assert(texts==1);
menu=1;quads=0;driving_pointer_draw();assert(!quads);menu=0;
render.local_player_index=1;driving_pointer_draw();assert(!quads);
return 0;}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture+s+main)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Driving indicator bounds, tracking warning and menu/player guards passed')
