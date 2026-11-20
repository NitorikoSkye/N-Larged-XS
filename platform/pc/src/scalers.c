/* The pixel-art scalers the filters use (Graphics' and Display's filter:
 * sdl.c's window and stream.c's pictures for the Android app), on the
 * GBA's own pixels (0x00RRGGBB). */

#include "gba/gba.h"
#include "host_platform.h"

/* Scale2x and Scale3x (EPX): each pixel becomes 2x2 (3x3) taking its
 * neighbours' colour along an edge where two of them agree. */
void HostScale2x(const u32 *in, u32 *out, int w, int h)
{
    int x;
    int y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            u32 p = in[y * w + x];
            u32 a = in[(y > 0 ? y - 1 : y) * w + x];
            u32 b = in[y * w + (x < w - 1 ? x + 1 : x)];
            u32 c = in[y * w + (x > 0 ? x - 1 : x)];
            u32 d = in[(y < h - 1 ? y + 1 : y) * w + x];
            u32 *o = &out[(2 * y) * 2 * w + 2 * x];

            if (c == a && c != d && a != b) {
                o[0] = a;
            } else {
                o[0] = p;
            }
            o[1] = (a == b && a != c && b != d) ? b : p;
            o[2 * w] = (d == c && d != b && c != a) ? c : p;
            o[2 * w + 1] = (b == d && b != a && d != c) ? d : p;
        }
    }
}

void HostScale3x(const u32 *in, u32 *out, int w, int h)
{
    int x;
    int y;

    for (y = 0; y < h; y++) {
        int yu = y > 0 ? y - 1 : y;
        int yd = y < h - 1 ? y + 1 : y;

        for (x = 0; x < w; x++) {
            int xl = x > 0 ? x - 1 : x;
            int xr = x < w - 1 ? x + 1 : x;
            u32 A = in[yu * w + xl], B = in[yu * w + x], C = in[yu * w + xr];
            u32 D = in[y * w + xl], E = in[y * w + x], F = in[y * w + xr];
            u32 G = in[yd * w + xl], Hh = in[yd * w + x], I = in[yd * w + xr];
            u32 *o = &out[(3 * y) * 3 * w + 3 * x];
            int ow = 3 * w;

            if (B != Hh && D != F) {
                o[0] = D == B ? D : E;
                o[1] = (D == B && E != C) || (B == F && E != A) ? B : E;
                o[2] = B == F ? F : E;
                o[ow] = (D == B && E != G) || (D == Hh && E != A) ? D : E;
                o[ow + 1] = E;
                o[ow + 2] = (B == F && E != I) || (Hh == F && E != C) ? F : E;
                o[2 * ow] = D == Hh ? D : E;
                o[2 * ow + 1] = (D == Hh && E != I) || (Hh == F && E != G) ? Hh : E;
                o[2 * ow + 2] = Hh == F ? F : E;
            } else {
                o[0] = o[1] = o[2] = o[ow] = o[ow + 1] = o[ow + 2] = o[2 * ow] = o[2 * ow + 1] =
                    o[2 * ow + 2] = E;
            }
        }
    }
}
