#ifndef MODEL_LOD_STABILITY_H
#define MODEL_LOD_STABILITY_H
#include <stdint.h>
struct model_lod_memory {uint32_t object,model,frame;int window,level,valid;};
static int model_lod_stable(struct model_lod_memory *entry,uint32_t object,uint32_t model,
    uint32_t frame,int window,int chosen,float pixels,const float *cutoffs)
{
    int result=chosen;
    if(entry->valid && entry->object==object && entry->model==model && entry->window==window &&
        frame-entry->frame<=2 && entry->level>chosen && entry->level<5 &&
        pixels>=cutoffs[entry->level]*0.80f) result=entry->level;
    entry->object=object;entry->model=model;entry->frame=frame;entry->window=window;
    entry->level=result;entry->valid=1;
    return result;
}
#endif
