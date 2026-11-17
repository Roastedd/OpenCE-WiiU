#ifndef REMOTE_CONTROL_TUNING_H
#define REMOTE_CONTROL_TUNING_H
#include <math.h>
/* Speed-adaptive first-order low-pass, with filtered velocity. */
struct remote_adaptive_filter {float x,y,dx,dy,previous_x,previous_y;int ready;};
static float remote_filter_alpha(float hz,float dt) {return dt/(dt+1.0f/(6.283185307f*hz));}
static void remote_adaptive_sample(struct remote_adaptive_filter *f,float *x,float *y,float dt)
{
    float a,vx,vy;
    if (!(dt>0 && dt<=0.1f) || !f->ready) {
        f->x=f->previous_x=*x;f->y=f->previous_y=*y;f->dx=f->dy=0;f->ready=1;return;
    }
    a=remote_filter_alpha(1.0f,dt);
    vx=(*x-f->previous_x)/dt;vy=(*y-f->previous_y)/dt;
    f->previous_x=*x;f->previous_y=*y;
    f->dx+=a*(vx-f->dx);f->dy+=a*(vy-f->dy);
    f->x+=remote_filter_alpha(6.0f+4.0f*fabsf(f->dx),dt)*(*x-f->x);
    f->y+=remote_filter_alpha(6.0f+4.0f*fabsf(f->dy),dt)*(*y-f->y);
    *x=f->x;*y=f->y;
}
static float remote_calibrate(float value,float center,float range)
{
    if (!(range>=0.5f && range<=1.5f)) range=1;
    if (!(center>=-0.3f && center<=0.3f)) center=0;
    return fmaxf(-1,fminf(1,(value-center)/range));
}
static float remote_turn_outer(float position,float inner,float outer,float speed,float seconds)
{
    float fraction;
    inner=fmaxf(0,fminf(inner,0.9f));outer=fmaxf(inner+0.05f,fminf(outer,1));
    if (!(seconds>0) || fabsf(position)<=inner) return 0;
    fraction=fminf(1,(fabsf(position)-inner)/(outer-inner));
    return -copysignf(speed*fraction*fraction*fminf(seconds,0.1f),position);
}
/* Preserve optical precision near center; allow deliberate outer-edge scans. */
static float remote_scope_turn(float position,float outer,float speed,float magnification,float floor_gain,float seconds)
{
    float fraction,blend,optical,gain;
    if (!(magnification>=1) || !(magnification<=3.402823466e38f)) return 0;
    optical=1.0f/magnification;
    fraction=fminf(1,fmaxf(0,(fabsf(position)-0.08f)/(fmaxf(0.13f,outer)-0.08f)));
    blend=fminf(1,fmaxf(0,(fraction-0.55f)/0.45f));
    blend=blend*blend*(3-2*blend);
    gain=optical+(fmaxf(optical,floor_gain)-optical)*blend;
    return remote_turn_outer(position,0.08f,outer,speed*gain,seconds);
}
#endif
