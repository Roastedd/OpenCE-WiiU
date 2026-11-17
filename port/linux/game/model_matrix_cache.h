#ifndef MODEL_MATRIX_CACHE_H
#define MODEL_MATRIX_CACHE_H

#include <stdint.h>
#include <string.h>

/* Render-only exact memoization. Both operands are compared byte-for-byte,
 * so recycled pointers, changed poses and reloaded model tags cannot hit
 * stale results. Collisions only evict an entry. No simulation uses this. */
#define MODEL_MATRIX_CACHE_SLOTS 128
struct model_matrix_cache_entry
{
    int valid;
    uint32_t frame;
    real_matrix4x3 left, right, result;
};
struct model_matrix_cache
{
    struct model_matrix_cache_entry entries[MODEL_MATRIX_CACHE_SLOTS];
    unsigned long hits, misses;
};

static void model_matrix_cache_multiply(struct model_matrix_cache *cache,
    uint32_t frame, const real_matrix4x3 *left, const real_matrix4x3 *right, real_matrix4x3 *out)
{
    uintptr_t key = ((uintptr_t)left >> 2) ^ ((uintptr_t)right >> 2);
    struct model_matrix_cache_entry *entry;
    key ^= key >> 7;
    entry = &cache->entries[key & (MODEL_MATRIX_CACHE_SLOTS - 1)];
    if (entry->valid && entry->frame == frame && !memcmp(&entry->left, left, sizeof(*left)) &&
        !memcmp(&entry->right, right, sizeof(*right)))
    {
        memcpy(out, &entry->result, sizeof(*out));
        ++cache->hits;
        return;
    }
    /* Capture before computing to retain alias-safe input identity. */
    memcpy(&entry->left, left, sizeof(*left));
    memcpy(&entry->right, right, sizeof(*right));
    matrix4x3_multiply(left, right, out);
    memcpy(&entry->result, out, sizeof(*out));
    entry->valid = 1;
    entry->frame = frame;
    ++cache->misses;
}
#endif
