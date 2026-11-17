#ifndef WIIU_UNIFORM_RUNS_H
#define WIIU_UNIFORM_RUNS_H
#include <stdint.h>

/* Shader register layouts are immutable. Store the remaining length of each
 * contiguous run, including unused rows, once when resolving the shader. */
static void wiiu_uniform_runs_build(const int16_t *elements, uint8_t *runs, unsigned count)
{
    unsigned row = count;
    while (row) {
        --row;
        runs[row] = 1;
        if (row + 1 < count &&
            ((elements[row] < 0 && elements[row + 1] < 0) ||
             (elements[row] >= 0 && elements[row + 1] == elements[row] + 4)))
            runs[row] = (uint8_t)(runs[row + 1] + 1);
    }
}

static unsigned wiiu_uniform_run(const int16_t *elements, const uint8_t *runs,
                                 unsigned row, unsigned end)
{
    unsigned length = 1;
    if (runs) {
        length = runs[row];
        return length < end - row ? length : end - row;
    }
    /* Allocation failure retains the original scan. */
    if (elements[row] < 0)
        return 1;
    while (row + length < end && elements[row + length] == elements[row] + (int)length * 4)
        ++length;
    return length;
}
#endif
