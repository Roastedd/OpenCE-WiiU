#include "cseries.h"
#include "real_math.h"
#include "render/render.h"
#include "model_matrix_cache.h"

int config_boolean(const char *name);
unsigned long config_changes(void);
void platform_log(const char *format, ...);
static struct model_matrix_cache cache;

/* Read configuration once per frame, outside the per-node loop. */
int model_render_matrix_cache_enabled(void)
{
    static int enabled = -1;
    static long checked_frame;
    static unsigned long read_at;
    if (enabled < 0 || checked_frame != render.frame_index)
    {
        unsigned long changes = config_changes();
        checked_frame = render.frame_index;
        if (enabled < 0 || read_at != changes)
        {
            int next = config_boolean("display.wiiu_model_matrix_cache");
            if (next != enabled)
                memset(&cache, 0, sizeof(cache));
            enabled = next;
            read_at = changes;
        }
        if (enabled && render.frame_index % 300 == 0)
            platform_log("render matrix cache: %lu hits, %lu misses (cumulative)", cache.hits, cache.misses);
    }
    return enabled;
}

/* Called only by render_model on the render-owning game thread. */
void model_render_matrix_multiply(const real_matrix4x3 *left,
    const real_matrix4x3 *right, real_matrix4x3 *out)
{
    model_matrix_cache_multiply(&cache, (uint32_t)render.frame_index, left, right, out);
}
