#include "cseries.h"
#include "render/render.h"
#include "interface/ui_widget.h"
#include "interface/hud.h"
#include "cutscene/cinematics.h"
#include "rasterizer/rasterizer.h"
#include "text/draw_string.h"

int remote_driving_pointer(float *x, float *y, int *valid);
void remote_driving_pointer_layout(float *x,float *y,float *outer);
void draw_quad(rectangle2d *rectangle, pixel32 color);

static void driving_line(short x0, short y0, short x1, short y1, pixel32 color)
{
    rectangle2d r;
    r.x0=x0; r.y0=y0; r.x1=x1; r.y1=y1;
    draw_quad(&r, color);
}

void driving_pointer_draw(void)
{
    float x,y,inner_x,inner_y,outer;
    int valid;
    short cx,cy,px,py,dx,dy,w,h;
    rectangle2d bounds;
    pixel32 color;
    if (render.local_player_index != 0 || ui_widgets_active() || cinematic_in_progress() ||
        !remote_driving_pointer(&x,&y,&valid)) return;
    bounds=render.camera.window_bounds;
    offset_rectangle2d(&bounds,-render.camera.viewport_bounds.x0,-render.camera.viewport_bounds.y0);
    w=bounds.x1-bounds.x0;h=bounds.y1-bounds.y0;
    if (w<32 || h<32) return;
    cx=bounds.x0+w/2;cy=bounds.y0+h/2;
    remote_driving_pointer_layout(&inner_x,&inner_y,&outer);
    dx=(short)(w*0.5f*inner_x);dy=(short)(h*0.5f*inner_y);
    /* Four dim corner marks outline the region with no steering input. */
    driving_line(cx-dx,cy-dy,cx-dx+5,cy-dy+1,0x8079ceff);
    driving_line(cx+dx-5,cy-dy,cx+dx,cy-dy+1,0x8079ceff);
    driving_line(cx-dx,cy+dy-1,cx-dx+5,cy+dy,0x8079ceff);
    driving_line(cx+dx-5,cy+dy-1,cx+dx,cy+dy,0x8079ceff);
    /* Dim outer ticks mark full-speed turning before the sensor edge. */
    {
        short ox=(short)(w*0.5f*MAX(outer,inner_x+0.05f));
        short oy=(short)(h*0.5f*MAX(outer,inner_y+0.05f));
        driving_line(cx-ox,cy-4,cx-ox+1,cy+4,0x6079ceff);
        driving_line(cx+ox-1,cy-4,cx+ox,cy+4,0x6079ceff);
        driving_line(cx-4,cy-oy,cx+4,cy-oy+1,0x6079ceff);
        driving_line(cx-4,cy+oy-1,cx+4,cy+oy,0x6079ceff);
    }
    x=PIN(x,-1.0f,1.0f);y=PIN(y,-1.0f,1.0f);
    px=(short)PIN(cx+x*w*0.5f,bounds.x0+8,bounds.x1-8);
    py=(short)PIN(cy+y*h*0.5f,bounds.y0+8,bounds.y1-8);
    color=valid ? ((x < -inner_x || x > inner_x || y < -inner_y || y > inner_y) ? 0xffffffff : 0xff79ceff) : 0xffffb347;
    /* Open reticle remains distinct from the vehicle's weapon crosshair. */
    driving_line(px-6,py-6,px-2,py-4,color);
    driving_line(px+2,py-6,px+6,py-4,color);
    driving_line(px-6,py+4,px-2,py+6,color);
    driving_line(px+2,py+4,px+6,py+6,color);
    if (!valid)
    {
        long font=hud_get_font_index();
        real_argb_color text_color;
        if (font==NONE) return;
        text_color.alpha=1;text_color.red=1;text_color.green=0.7f;text_color.blue=0.28f;
        bounds.y0=bounds.y0+h*3/4;bounds.y1=bounds.y0+32;
        draw_string_set_draw_mode(font,NONE,2,0,&text_color);
        rasterizer_draw_string(&bounds,NULL,NULL,0,"POINT AT SENSOR BAR");
    }
}
