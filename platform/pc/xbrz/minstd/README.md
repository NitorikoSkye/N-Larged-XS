# minstd: the C++ library pieces xBRZ uses, for the Android musl cores

The musl cores (platform/android/musl-cc) compile against musl's C headers
only: GCC's libstdc++ headers assume glibc. xBRZ (../xbrz.cpp, the
"cartoon" filters) needs just std::min, max, fill, swap, sqrt,
numeric_limits<int> and one std::vector (its colour-distance table, sized
once). These headers give exactly that, on musl's C library; the vector
takes its memory from malloc (not the game's operator new). musl-cc adds
this directory for C++ compiles; nothing else in the game includes a C++
library header.
