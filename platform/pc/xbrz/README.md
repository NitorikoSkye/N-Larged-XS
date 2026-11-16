# xBRZ

xBRZ ("scale by rules"), the pixel-art upscaler by Zenju (HqMAME), GPLv3
like this project: `xbrz.cpp` and `xbrz.h` as PPSSPP carries them
(hrydgard/ppsspp, ext/xbrz), the header without PPSSPP's own includes and
with xBRZ's `ScalerCfg` (its config.h) inside. `xbrz_c.cpp` gives the port's
C code (sdl.c's "cartoon" filters) one function to call.
