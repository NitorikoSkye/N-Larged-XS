/* xBRZ for the port's C code (sdl.c): rows [yFirst, yLast) of the
 * w x h XRGB picture `src`, `factor` (2-6) times, into `dst`
 * (w * factor wide). */

#include "xbrz.h"

extern "C" void HostXbrzScale(int factor, const unsigned int *src, unsigned int *dst, int w, int h, int yFirst,
                              int yLast)
{
    xbrz::scale((size_t)factor, (const uint32_t *)src, (uint32_t *)dst, w, h, xbrz::ColorFormat::RGB,
                xbrz::ScalerCfg(), yFirst, yLast);
}

/* xBRZ builds its colour distance table (64 MB) on its first use, in a
 * function's static: done here once, on one thread, before the picture's
 * rows are shared out to the worker threads. */
extern "C" void HostXbrzWarm(void)
{
    static bool done;
    uint32_t src[4 * 4] = { 0 };
    uint32_t dst[8 * 8];

    if (!done) {
        xbrz::scale(2, src, dst, 4, 4, xbrz::ColorFormat::RGB);
        done = true;
    }
}
