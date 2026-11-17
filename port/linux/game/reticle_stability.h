#ifndef RETICLE_STABILITY_H
#define RETICLE_STABILITY_H
#include <stdint.h>
#include <math.h>
struct reticle_stability
{
    uint32_t stamp;
    long owner,weapon;
    int context,valid;
    float x,y;
};
/* Display-only stabilization of small projection changes. A deliberate aim
 * move snaps immediately; filtered position can never trail by over a pixel. */
static void reticle_stability_update(struct reticle_stability *s,uint32_t stamp,
    long owner,long weapon,int context,float *x,float *y)
{
    uint32_t elapsed=stamp-s->stamp;
    float dx=*x-s->x,dy=*y-s->y,alpha,error;
    if(!s->valid || s->owner!=owner || s->weapon!=weapon || s->context!=context ||
        elapsed>150 || dx*dx+dy*dy>9.0f)
    {
        s->x=*x;s->y=*y;s->valid=1;
    }
    else if(elapsed)
    {
        alpha=(float)elapsed/((float)elapsed+16.0f);
        s->x+=alpha*dx;s->y+=alpha*dy;
    }
    {
        dx=*x-s->x;dy=*y-s->y;error=dx*dx+dy*dy;
        if(error>1.0f) {
            float scale=1.0f/sqrtf(error);
            s->x=*x-dx*scale;s->y=*y-dy*scale;
        }
    }
    s->stamp=stamp;s->owner=owner;s->weapon=weapon;s->context=context;
    *x=s->x;*y=s->y;
}
#endif
