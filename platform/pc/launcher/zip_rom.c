/* A ROM in a .zip (platform/pc/LAUNCHER.md): the launcher takes the
 * player's ROM as it is or zipped, as most ROM sets keep them. ZipRom
 * finds the archive's .gba (or its only file), and gives it back stored
 * or inflated (deflate, RFC 1951), its CRC-32 checked. No zip64, no
 * encryption: a ROM is 8 MB. */

#include <stdlib.h>
#include <string.h>

#include "zip_rom.h"

/* --- Inflate ------------------------------------------------------------------ */

struct Bits {
    const unsigned char *in;
    size_t inSize;
    size_t pos;
    unsigned bitBuf;
    int bitCount;
    unsigned char *out;
    size_t outSize;
    size_t outPos;
    int bad;
};

static int Bit(struct Bits *b)
{
    int v;

    if (b->bitCount == 0) {
        if (b->pos >= b->inSize) {
            b->bad = 1;
            return 0;
        }
        b->bitBuf = b->in[b->pos++];
        b->bitCount = 8;
    }
    v = b->bitBuf & 1;
    b->bitBuf >>= 1;
    b->bitCount--;
    return v;
}

static unsigned Bits(struct Bits *b, int n)
{
    unsigned v = 0;
    int i;

    for (i = 0; i < n; i++) {
        v |= (unsigned)Bit(b) << i;
    }
    return v;
}

/* A canonical Huffman code: counts of each length, and the symbols in
 * code order. */
struct Huffman {
    unsigned short count[16];
    unsigned short symbol[320];
};

static int Build(struct Huffman *h, const unsigned char *lengths, int n)
{
    unsigned short offs[16];
    int left = 1;
    int i;

    memset(h->count, 0, sizeof(h->count));
    for (i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;
    for (i = 1; i < 16; i++) {
        left <<= 1;
        left -= h->count[i];
        if (left < 0) {
            return 0; /* over-subscribed */
        }
    }
    offs[1] = 0;
    for (i = 1; i < 15; i++) {
        offs[i + 1] = offs[i] + h->count[i];
    }
    for (i = 0; i < n; i++) {
        if (lengths[i] != 0) {
            h->symbol[offs[lengths[i]]++] = (unsigned short)i;
        }
    }
    return 1;
}

static int Decode(struct Bits *b, const struct Huffman *h)
{
    int code = 0;
    int first = 0;
    int index = 0;
    int len;

    for (len = 1; len < 16; len++) {
        int count;

        code |= Bit(b);
        count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (b->bad) {
            return -1;
        }
    }
    b->bad = 1;
    return -1;
}

static const unsigned short sLenBase[29] = { 3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                             15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                             67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char sLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                             2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short sDistBase[30] = { 1,    2,    3,    4,     5,     7,    9,    13,
                                              17,   25,   33,   49,    65,    97,   129,  193,
                                              257,  385,  513,  769,   1025,  1537, 2049, 3073,
                                              4097, 6145, 8193, 12289, 16385, 24577 };
static const unsigned char sDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                              6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int Codes(struct Bits *b, const struct Huffman *lit, const struct Huffman *dist)
{
    for (;;) {
        int sym = Decode(b, lit);

        if (sym < 0 || b->bad) {
            return 0;
        }
        if (sym < 256) {
            if (b->outPos >= b->outSize) {
                return 0;
            }
            b->out[b->outPos++] = (unsigned char)sym;
        } else if (sym == 256) {
            return 1;
        } else {
            unsigned len;
            unsigned d;
            int ds;

            sym -= 257;
            if (sym >= 29) {
                return 0;
            }
            len = sLenBase[sym] + Bits(b, sLenExtra[sym]);
            ds = Decode(b, dist);
            if (ds < 0 || ds >= 30) {
                return 0;
            }
            d = sDistBase[ds] + Bits(b, sDistExtra[ds]);
            if (d > b->outPos || len > b->outSize - b->outPos) {
                return 0;
            }
            while (len-- > 0) {
                b->out[b->outPos] = b->out[b->outPos - d];
                b->outPos++;
            }
        }
    }
}

static int Fixed(struct Bits *b)
{
    static struct Huffman lit;
    static struct Huffman dist;
    static int built;

    if (!built) {
        unsigned char l[288];
        int i;
        /* RFC 1951 3.2.6: the fixed code's lengths, by symbol range */
        static const unsigned short ends[4] = { 144, 256, 280, 288 };
        static const unsigned char lens[4] = { 8, 9, 7, 8 };
        int r;

        for (i = 0, r = 0; r < 4; r++) {
            for (; i < ends[r]; i++)
                l[i] = lens[r];
        }
        Build(&lit, l, 288);
        for (i = 0; i < 30; i++)
            l[i] = 5;
        Build(&dist, l, 30);
        built = 1;
    }
    return Codes(b, &lit, &dist);
}

static int Dynamic(struct Bits *b)
{
    static const unsigned char order[19] = { 16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                             11, 4,  12, 3, 13, 2, 14, 1, 15 };
    unsigned char lengths[320];
    struct Huffman lit;
    struct Huffman dist;
    struct Huffman lencode;
    int nlen = (int)Bits(b, 5) + 257;
    int ndist = (int)Bits(b, 5) + 1;
    int ncode = (int)Bits(b, 4) + 4;
    int i;

    if (nlen > 286 || ndist > 30) {
        return 0;
    }
    memset(lengths, 0, sizeof(lengths));
    for (i = 0; i < ncode; i++) {
        lengths[order[i]] = (unsigned char)Bits(b, 3);
    }
    if (!Build(&lencode, lengths, 19)) {
        return 0;
    }
    i = 0;
    while (i < nlen + ndist) {
        int sym = Decode(b, &lencode);
        int rep;
        unsigned char v = 0;

        if (sym < 0 || b->bad) {
            return 0;
        }
        if (sym < 16) {
            lengths[i++] = (unsigned char)sym;
            continue;
        }
        if (sym == 16) {
            if (i == 0) {
                return 0;
            }
            v = lengths[i - 1];
            rep = 3 + (int)Bits(b, 2);
        } else if (sym == 17) {
            rep = 3 + (int)Bits(b, 3);
        } else {
            rep = 11 + (int)Bits(b, 7);
        }
        if (i + rep > nlen + ndist) {
            return 0;
        }
        while (rep-- > 0) {
            lengths[i++] = v;
        }
    }
    if (lengths[256] == 0 || !Build(&lit, lengths, nlen) || !Build(&dist, lengths + nlen, ndist)) {
        return 0;
    }
    return Codes(b, &lit, &dist);
}

static int Inflate(const unsigned char *in, size_t inSize, unsigned char *out, size_t outSize)
{
    struct Bits b;
    int last;

    memset(&b, 0, sizeof(b));
    b.in = in;
    b.inSize = inSize;
    b.out = out;
    b.outSize = outSize;
    do {
        int type;

        last = Bit(&b);
        type = (int)Bits(&b, 2);
        if (type == 0) {
            unsigned len;

            b.bitCount = 0; /* to the byte */
            if (b.pos + 4 > inSize) {
                return 0;
            }
            len = in[b.pos] | in[b.pos + 1] << 8;
            b.pos += 4;
            if (len > inSize - b.pos || len > outSize - b.outPos) {
                return 0;
            }
            memcpy(out + b.outPos, in + b.pos, len);
            b.pos += len;
            b.outPos += len;
        } else if (type == 1) {
            if (!Fixed(&b)) {
                return 0;
            }
        } else if (type == 2) {
            if (!Dynamic(&b)) {
                return 0;
            }
        } else {
            return 0;
        }
        if (b.bad) {
            return 0;
        }
    } while (!last);
    return b.outPos == outSize;
}

/* --- The archive ---------------------------------------------------------------- */

static unsigned Le16(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static unsigned long Le32(const unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 |
           (unsigned long)p[3] << 24;
}

static unsigned long Crc32(const unsigned char *p, size_t n)
{
    static unsigned long table[256];
    unsigned long c = 0xFFFFFFFFul;
    size_t i;

    if (table[1] == 0) {
        unsigned long k;

        for (k = 0; k < 256; k++) {
            unsigned long v = k;
            int j;

            for (j = 0; j < 8; j++) {
                v = (v & 1) ? 0xEDB88320ul ^ (v >> 1) : v >> 1;
            }
            table[k] = v;
        }
    }
    for (i = 0; i < n; i++) {
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return (c ^ 0xFFFFFFFFul) & 0xFFFFFFFFul;
}

int ZipIsArchive(const unsigned char *data, size_t size)
{
    return size >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 3 && data[3] == 4;
}

static int EndsWithGba(const unsigned char *name, unsigned n)
{
    return n >= 4 && name[n - 4] == '.' && (name[n - 3] | 0x20) == 'g' &&
           (name[n - 2] | 0x20) == 'b' && (name[n - 1] | 0x20) == 'a';
}

unsigned char *ZipRom(const unsigned char *zip, size_t zipSize, size_t *romSize, size_t limit)
{
    const unsigned char *eocd = NULL;
    const unsigned char *p;
    const unsigned char *pick = NULL;
    const unsigned char *only = NULL;
    unsigned files = 0;
    unsigned count;
    unsigned long dirOffset;
    unsigned i;
    size_t k;

    *romSize = 0;
    /* the end of the central directory: in the last 64 KB and 22 bytes */
    for (k = zipSize >= 22 ? zipSize - 22 : 0; zipSize >= 22; k--) {
        if (Le32(zip + k) == 0x06054b50ul) {
            eocd = zip + k;
            break;
        }
        if (k == 0 || zipSize - k > 65557) {
            break;
        }
    }
    if (eocd == NULL) {
        return NULL;
    }
    count = Le16(eocd + 10);
    dirOffset = Le32(eocd + 16);
    if (dirOffset >= zipSize) {
        return NULL;
    }
    p = zip + dirOffset;
    for (i = 0; i < count; i++) {
        unsigned nameLen;

        if ((size_t)(p - zip) + 46 > zipSize || Le32(p) != 0x02014b50ul) {
            return NULL;
        }
        nameLen = Le16(p + 28);
        if ((size_t)(p - zip) + 46 + nameLen > zipSize) {
            return NULL;
        }
        if (nameLen > 0 && p[46 + nameLen - 1] != '/') {
            files++;
            only = p;
            if (pick == NULL && EndsWithGba(p + 46, nameLen)) {
                pick = p;
            }
        }
        p += 46 + nameLen + Le16(p + 30) + Le16(p + 32);
    }
    if (pick == NULL && files == 1) {
        pick = only;
    }
    if (pick == NULL) {
        return NULL;
    }
    {
        unsigned flags = Le16(pick + 8);
        unsigned method = Le16(pick + 10);
        unsigned long crc = Le32(pick + 16);
        unsigned long packed = Le32(pick + 20);
        unsigned long size = Le32(pick + 24);
        unsigned long local = Le32(pick + 42);
        const unsigned char *data;
        unsigned char *out;
        int ok;

        if ((flags & 1) != 0 || size > limit || local + 30 > zipSize ||
            Le32(zip + local) != 0x04034b50ul) {
            return NULL; /* encrypted, too big, or broken */
        }
        data = zip + local + 30 + Le16(zip + local + 26) + Le16(zip + local + 28);
        if ((size_t)(data - zip) > zipSize || packed > zipSize - (size_t)(data - zip)) {
            return NULL;
        }
        out = malloc(size + 1);
        if (out == NULL) {
            return NULL;
        }
        if (method == 0) {
            ok = packed == size;
            if (ok) {
                memcpy(out, data, size);
            }
        } else if (method == 8) {
            ok = Inflate(data, packed, out, size);
        } else {
            ok = 0;
        }
        if (!ok || Crc32(out, size) != crc) {
            free(out);
            return NULL;
        }
        *romSize = size;
        return out;
    }
}
