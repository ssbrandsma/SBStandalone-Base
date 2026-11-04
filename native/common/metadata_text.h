#ifndef SB_METADATA_TEXT_H
#define SB_METADATA_TEXT_H
#include <stddef.h>
/* Shared extraction of existing bounded HLS conversion; no parser activation. */
/** Append one Unicode scalar to bounded UTF-8, replacing invalid scalars. */
static inline void sb_metadata_scalar(char *out, size_t capacity, size_t *n, unsigned c) {
    if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
        c = 0xfffd;
    if (c < 32)
        c = ' ';
    unsigned bytes = c < 128 ? 1 : c < 2048 ? 2 : c < 65536 ? 3 : 4;
    if (*n + bytes >= capacity)
        return;
    if (bytes == 1)
        out[(*n)++] = (char)c;
    else {
        out[(*n)++] = (char)((bytes == 2   ? 0xc0
                              : bytes == 3 ? 0xe0
                                           : 0xf0) |
                             (c >> (6 * (bytes - 1))));
        for (unsigned i = bytes - 1; i; i--)
            out[(*n)++] = (char)(0x80 | ((c >> (6 * (i - 1))) & 63));
    }
    out[*n] = 0;
}

/** Convert bounded ID3 text including UTF-16 surrogate pairs to valid UTF-8. */
static inline void sb_metadata_text(char *out, size_t capacity, unsigned enc,
                                    const unsigned char *p, size_t size) {
    size_t n = 0, i = 0;
    int little = 0;
    out[0] = 0;
    if (enc > 3)
        return;
    if (enc == 1) {
        if (size < 2)
            return;
        if (p[0] == 255 && p[1] == 254)
            little = 1;
        else if (p[0] != 254 || p[1] != 255)
            return;
        i = 2;
    }
    while (i < size && n + 4 < capacity) {
        unsigned c = p[i++];
        if (enc == 1 || enc == 2) {
            if (i >= size)
                break;
            c = little ? c | (p[i++] << 8) : (c << 8) | p[i++];
            if (!c)
                break;
            if (c >= 0xd800 && c <= 0xdbff && i + 1 < size) {
                unsigned low = little ? p[i] | (p[i + 1] << 8) : (p[i] << 8) | p[i + 1];
                if (low >= 0xdc00 && low <= 0xdfff) {
                    i += 2;
                    c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                }
            }
        } else if (!c)
            break;
        else if (enc == 3 && c >= 128) {
            unsigned count = c >= 0xc2 && c <= 0xdf   ? 1
                             : c >= 0xe0 && c <= 0xef ? 2
                             : c >= 0xf0 && c <= 0xf4 ? 3
                                                      : 0;
            unsigned value = count ? c & ((1u << (6 - count)) - 1) : 0xfffd;
            if (!count || i + count > size)
                c = 0xfffd;
            else {
                unsigned j = 0;
                for (; j < count && (p[i + j] & 192) == 128; j++)
                    value = (value << 6) | (p[i + j] & 63);
                if (j != count || value < (count == 1 ? 128u : count == 2 ? 2048u : 65536u))
                    c = 0xfffd;
                else {
                    c = value;
                    i += count;
                }
            }
        }
        sb_metadata_scalar(out, capacity, &n, c);
    }
}

#endif
