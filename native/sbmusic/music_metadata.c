#define _POSIX_C_SOURCE 200809L
#include "music_metadata.h"
#include "../common/metadata_text.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    int fd;
    uint64_t size, read;
    sb_metadata_result *out;
} reader;
static uint32_t be(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint32_t le(const unsigned char *p) {
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static uint32_t sync32(const unsigned char *p) {
    return (p[0] | p[1] | p[2] | p[3]) & 128
               ? UINT32_MAX
               : (uint32_t)p[0] << 21 | (uint32_t)p[1] << 14 | (uint32_t)p[2] << 7 | p[3];
}
static int fail(reader *r, sb_meta_status s, const char *message) {
    r->out->status = s;
    snprintf(r->out->error, sizeof r->out->error, "%s", message);
    return 0;
}
static int read_at(reader *r, uint64_t at, void *buffer, size_t bytes) {
    if (at > r->size || bytes > r->size - at)
        return fail(r, SB_META_INVALID, "Truncated metadata/file bounds");
    if (bytes > SB_META_READ_MAX - r->read)
        return fail(r, SB_META_LIMIT, "Metadata read budget exceeded");
    size_t n = 0;
    while (n < bytes) {
        ssize_t got = pread(r->fd, (char *)buffer + n, bytes - n, (off_t)(at + n));
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            return fail(r, SB_META_IO, "Metadata read failed");
        n += (size_t)got;
        r->read += (size_t)got;
    }
    return 1;
}
static int decoded(reader *r, char *out, size_t cap, unsigned encoding, const unsigned char *p,
                   size_t n) {
    if (encoding > 3 || n > 4096)
        return fail(r, SB_META_LIMIT, "Text field encoding/size unsupported");
    if (encoding == 1 && (n < 2 || !((p[0] == 255 && p[1] == 254) || (p[0] == 254 && p[1] == 255))))
        return fail(r, SB_META_INVALID, "UTF-16 BOM missing");
    char temp[SB_ML_TEXT_MAX + 8];
    sb_metadata_text(temp, sizeof temp, encoding, p, n);
    if (strlen(temp) >= cap || strlen(temp) >= sizeof temp - 5)
        return fail(r, SB_META_LIMIT, "Full metadata field exceeds DTO bound");
    /* Same replacement policy as HLS; no lossy NFC/case normalization. */
    strcpy(out, temp);
    return 1;
}
static void number(const char *s, unsigned *value, unsigned *total) {
    unsigned a = 0, b = 0;
    if (sscanf(s, "%u/%u", &a, &b) >= 1) {
        if (a < 100000)
            *value = a;
        if (total && b < 100000)
            *total = b;
    }
}
static const char *genre(unsigned n) {
    static const char *names[] = {"Blues",
                                  "Classic Rock",
                                  "Country",
                                  "Dance",
                                  "Disco",
                                  "Funk",
                                  "Grunge",
                                  "Hip-Hop",
                                  "Jazz",
                                  "Metal",
                                  "New Age",
                                  "Oldies",
                                  "Other",
                                  "Pop",
                                  "R&B",
                                  "Rap",
                                  "Reggae",
                                  "Rock",
                                  "Techno",
                                  "Industrial",
                                  "Alternative",
                                  "Ska",
                                  "Death Metal",
                                  "Pranks",
                                  "Soundtrack",
                                  "Euro-Techno",
                                  "Ambient",
                                  "Trip-Hop",
                                  "Vocal",
                                  "Jazz+Funk",
                                  "Fusion",
                                  "Trance",
                                  "Classical",
                                  "Instrumental",
                                  "Acid",
                                  "House",
                                  "Game",
                                  "Sound Clip",
                                  "Gospel",
                                  "Noise",
                                  "AlternRock",
                                  "Bass",
                                  "Soul",
                                  "Punk",
                                  "Space",
                                  "Meditative",
                                  "Instrumental Pop",
                                  "Instrumental Rock",
                                  "Ethnic",
                                  "Gothic",
                                  "Darkwave",
                                  "Techno-Industrial",
                                  "Electronic",
                                  "Pop-Folk",
                                  "Eurodance",
                                  "Dream",
                                  "Southern Rock",
                                  "Comedy",
                                  "Cult",
                                  "Gangsta",
                                  "Top 40",
                                  "Christian Rap",
                                  "Pop/Funk",
                                  "Jungle",
                                  "Native American",
                                  "Cabaret",
                                  "New Wave",
                                  "Psychadelic",
                                  "Rave",
                                  "Showtunes",
                                  "Trailer",
                                  "Lo-Fi",
                                  "Tribal",
                                  "Acid Punk",
                                  "Acid Jazz",
                                  "Polka",
                                  "Retro",
                                  "Musical",
                                  "Rock & Roll",
                                  "Hard Rock"};
    return n < sizeof names / sizeof names[0] ? names[n] : NULL;
}
static int field(reader *r, const char *key, const unsigned char *p, size_t n, unsigned enc) {
    sb_ml_track *t = &r->out->track;
    char value[SB_ML_TEXT_MAX];
    if (!decoded(r, value, sizeof value, enc, p, n))
        return 0;
    if (!value[0])
        return 1;
    char *single = NULL;
    size_t cap = 0;
    if (!strcmp(key, "TITLE")) {
        single = t->title;
        cap = sizeof t->title;
    } else if (!strcmp(key, "ALBUM")) {
        single = t->album;
        cap = sizeof t->album;
    } else if (!strcmp(key, "ALBUMARTIST") || !strcmp(key, "ALBUM ARTIST")) {
        single = t->album_artist;
        cap = sizeof t->album_artist;
    } else if (!strcmp(key, "ARTIST") || !strcmp(key, "GENRE")) {
        unsigned *count = !strcmp(key, "ARTIST") ? &t->artist_count : &t->genre_count;
        size_t width = !strcmp(key, "ARTIST") ? sizeof t->artists[0] : sizeof t->genres[0];
        char *array = !strcmp(key, "ARTIST") ? (char *)t->artists : (char *)t->genres;
        if (!strcmp(key, "GENRE") && value[0] == '(') {
            unsigned g;
            if (sscanf(value, "(%u)", &g) == 1 && genre(g))
                snprintf(value, sizeof value, "%s", genre(g));
        }
        for (unsigned i = 0; i < *count; i++)
            if (!strcmp(array + i * width, value))
                return 1;
        if (*count == SB_ML_NAMES_MAX || strlen(value) >= width)
            return fail(r, SB_META_LIMIT, "Multi-value field exceeds library bounds");
        strcpy(array + (*count)++ * width, value);
        return 1;
    } else if (!strcmp(key, "TRACKNUMBER")) {
        number(value, &t->track, &t->track_total);
        return 1;
    } else if (!strcmp(key, "DISCNUMBER")) {
        number(value, &t->disc, &t->disc_total);
        return 1;
    } else if (!strcmp(key, "TRACKTOTAL") || !strcmp(key, "TOTALTRACKS")) {
        number(value, &t->track_total, NULL);
        return 1;
    } else if (!strcmp(key, "DISCTOTAL") || !strcmp(key, "TOTALDISCS")) {
        number(value, &t->disc_total, NULL);
        return 1;
    } else if (!strcmp(key, "DATE") || !strcmp(key, "YEAR")) {
        if (strlen(value) >= sizeof t->release_date)
            return fail(r, SB_META_LIMIT, "Full release date exceeds bound");
        if (!t->release_date[0])
            strcpy(t->release_date, value);
        else if (strcmp(t->release_date, value)) {
            r->out->warnings++;
            return 1;
        }
        number(value, &t->year, NULL);
        return 1;
    } else if (!strcmp(key, "COMPILATION")) {
        t->compilation = !strcmp(value, "1") || !strcmp(value, "true");
        return 1;
    }
    if (single) {
        if (strlen(value) >= cap)
            return fail(r, SB_META_LIMIT, "Metadata value exceeds library bound");
        if (!single[0])
            strcpy(single, value);
        else if (strcmp(single, value))
            r->out->warnings++;
    }
    return 1;
}
static void artwork(reader *r, const char *kind, uint64_t offset, uint64_t length, const char *mime,
                    int reparse) {
    if (offset > r->size || length > r->size - offset) {
        fail(r, SB_META_INVALID, "Artwork bounds invalid");
        return;
    }
    if (r->out->track.artwork_ref[0])
        return;
    snprintf(r->out->track.artwork_ref, sizeof r->out->track.artwork_ref, "%s:%llu:%llu:%s:%s",
             kind, (unsigned long long)offset, (unsigned long long)length, mime,
             reparse ? "reparse" : "direct");
}
static const char *image_type(const unsigned char *p, size_t n) {
    if (n >= 8 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8))
        return "image/png";
    if (n >= 3 && p[0] == 255 && p[1] == 216 && p[2] == 255)
        return "image/jpeg";
    return NULL;
}
static size_t terminated(const unsigned char *p, size_t n, unsigned encoding) {
    size_t step = (encoding == 1 || encoding == 2) ? 2 : 1;
    for (size_t i = 0; i + step <= n; i += step)
        if (!p[i] && (step == 1 || !p[i + 1]))
            return i + step;
    return 0;
}
static int picture(reader *r, const unsigned char *p, size_t n, uint64_t base, const char *kind,
                   int reparse) {
    if (n < 8)
        return fail(r, SB_META_INVALID, "Truncated FLAC picture");
    size_t at = 4;
    uint32_t mime = be(p + at);
    at += 4;
    if (mime > n - at)
        return fail(r, SB_META_INVALID, "Picture MIME bounds");
    at += mime;
    if (n - at < 4)
        return fail(r, SB_META_INVALID, "Picture description missing");
    uint32_t description = be(p + at);
    at += 4;
    if (description > n - at)
        return fail(r, SB_META_INVALID, "Picture description bounds");
    at += description;
    if (n - at < 20)
        return fail(r, SB_META_INVALID, "Picture dimensions missing");
    at += 16;
    uint32_t bytes = be(p + at);
    at += 4;
    if (bytes > n - at)
        return fail(r, SB_META_INVALID, "Picture data bounds");
    const char *type = image_type(p + at, bytes);
    if (type)
        artwork(r, kind, base + at, bytes, type, reparse);
    return 1;
}
static int comments(reader *r, const unsigned char *p, size_t n, int ogg) {
    if (n < 8)
        return fail(r, SB_META_INVALID, "Truncated Vorbis comments");
    size_t at = 4;
    uint32_t vendor = le(p);
    if (vendor > n - at)
        return fail(r, SB_META_INVALID, "Vendor bounds");
    at += vendor;
    if (n - at < 4)
        return fail(r, SB_META_INVALID, "Comment count missing");
    uint32_t count = le(p + at);
    at += 4;
    if (count > 4096)
        return fail(r, SB_META_LIMIT, "Comment count bound");
    for (uint32_t i = 0; i < count; i++) {
        if (n - at < 4)
            return fail(r, SB_META_INVALID, "Comment size missing");
        uint32_t length = le(p + at);
        at += 4;
        if (length > n - at)
            return fail(r, SB_META_INVALID, "Comment bounds");
        const unsigned char *equal = memchr(p + at, '=', length);
        if (equal) {
            size_t keylen = (size_t)(equal - (p + at));
            char key[64];
            if (keylen < sizeof key) {
                for (size_t k = 0; k < keylen; k++)
                    key[k] = (char)toupper(p[at + k]);
                key[keylen] = 0;
                const unsigned char *value = equal + 1;
                size_t bytes = length - keylen - 1;
                if (!strcmp(key, "METADATA_BLOCK_PICTURE")) {
                    /* Base64 picture may span Ogg pages; never advertise a false byte range. */
                    if (bytes > SB_META_MAX)
                        return fail(r, SB_META_LIMIT, "Embedded comment artwork too large");
                    unsigned char *decoded = malloc(bytes + 1);
                    if (!decoded)
                        return fail(r, SB_META_LIMIT, "Picture allocation failed");
                    size_t used = 0;
                    unsigned bits = 0, acc = 0;
                    int valid = 1;
                    for (size_t j = 0; j < bytes && value[j] != '='; j++) {
                        unsigned c = value[j], v = c >= 'A' && c <= 'Z'   ? c - 'A'
                                                   : c >= 'a' && c <= 'z' ? c - 'a' + 26
                                                   : c >= '0' && c <= '9' ? c - '0' + 52
                                                   : c == '+'             ? 62
                                                   : c == '/'             ? 63
                                                                          : 255;
                        if (v == 255) {
                            valid = 0;
                            break;
                        }
                        acc = (acc << 6) | v;
                        bits += 6;
                        if (bits >= 8) {
                            bits -= 8;
                            decoded[used++] = (unsigned char)(acc >> bits);
                        }
                    }
                    if (valid) { /* Reparse reference, not base64-decoded offsets into original
                                    file. */
                        size_t old = strlen(r->out->track.artwork_ref);
                        if (picture(r, decoded, used, 0, "vorbis-picture", 1) && !old &&
                            r->out->track.artwork_ref[0]) {
                            int png = strstr(r->out->track.artwork_ref, "image/png") != NULL;
                            snprintf(r->out->track.artwork_ref, sizeof r->out->track.artwork_ref,
                                     "vorbis-picture:0:0:%s:reparse",
                                     png ? "image/png" : "image/jpeg");
                        }
                    } else
                        fail(r, SB_META_INVALID, "Invalid picture base64");
                    free(decoded);
                    if (r->out->status != SB_META_OK)
                        return 0;
                } else if (!field(r, key, value, bytes, 3))
                    return 0;
            }
        }
        at += length;
    }
    if (ogg && (at >= n || !(p[at] & 1)))
        return fail(r, SB_META_INVALID, "Vorbis comment framing bit missing");
    return 1;
}
static int flac(reader *r) {
    uint64_t at = 4;
    unsigned blocks = 0;
    int last = 0, stream = 0;
    while (!last) {
        unsigned char h[4];
        if (++blocks > 256)
            return fail(r, SB_META_LIMIT, "FLAC block count bound");
        if (!read_at(r, at, h, 4))
            return 0;
        at += 4;
        last = h[0] & 128;
        unsigned type = h[0] & 127;
        uint32_t n = (uint32_t)h[1] << 16 | (uint32_t)h[2] << 8 | h[3];
        if (n > r->size - at)
            return fail(r, SB_META_INVALID, "FLAC block bounds");
        if (blocks == 1 && (type != 0 || n != 34))
            return fail(r, SB_META_INVALID, "FLAC STREAMINFO must be first");
        if (type == 0 || type == 4 || type == 6) {
            if (n > SB_META_MAX)
                return fail(r, SB_META_LIMIT, "FLAC metadata block bound");
            unsigned char *p = malloc(n ? n : 1);
            if (!p)
                return fail(r, SB_META_LIMIT, "Metadata allocation failed");
            if (!read_at(r, at, p, n)) {
                free(p);
                return 0;
            }
            if (type == 0) {
                if (stream++ || n != 34) {
                    free(p);
                    return fail(r, SB_META_INVALID, "Duplicate/invalid STREAMINFO");
                }
                sb_ml_track *t = &r->out->track;
                uint64_t v = 0;
                for (unsigned i = 10; i < 18; i++)
                    v = (v << 8) | p[i];
                t->sample_rate = (unsigned)(v >> 44);
                t->channels = (unsigned)((v >> 41) & 7) + 1;
                t->bit_depth = (unsigned)((v >> 36) & 31) + 1;
                uint64_t samples = v & UINT64_C(0xfffffffff);
                t->total_samples = samples;
                if (!t->sample_rate) {
                    free(p);
                    return fail(r, SB_META_INVALID, "Zero FLAC sample rate");
                }
                t->duration_ms = samples * 1000 / t->sample_rate;
            } else if (type == 4)
                comments(r, p, n, 0);
            else
                picture(r, p, n, at, "flac-picture", 0);
            free(p);
            if (r->out->status != SB_META_OK)
                return 0;
        }
        at += n;
        if (at > 16 * 1024 * 1024)
            return fail(r, SB_META_LIMIT, "FLAC metadata span bound");
    }
    if (!stream || at >= r->size)
        return fail(r, SB_META_INVALID, "FLAC audio absent");
    sb_ml_track *t = &r->out->track;
    strcpy(t->codec, "flac");
    t->audio_offset = at;
    t->audio_length = r->size - at;
    if (t->duration_ms)
        t->bitrate = (unsigned)((r->size - at) * 8 / t->duration_ms);
    return 1;
}
static size_t deunsync(unsigned char *p, size_t n) {
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        p[j++] = p[i];
        if (p[i] == 255 && i + 1 < n && !p[i + 1])
            i++;
    }
    return j;
}
static int id3(reader *r, uint64_t *audio) {
    unsigned char h[10];
    if (!read_at(r, 0, h, 10))
        return 0;
    if (memcmp(h, "ID3", 3)) {
        *audio = 0;
        return 1;
    }
    unsigned version = h[3], flags = h[5];
    uint32_t size = sync32(h + 6);
    if (version < 2 || version > 4 || size == UINT32_MAX)
        return fail(r, SB_META_INVALID, "Invalid ID3 header");
    if (size > SB_META_MAX)
        return fail(r, SB_META_LIMIT, "ID3 tag exceeds bounded parser budget");
    if (version == 2 && (flags & 64))
        return fail(r, SB_META_UNSUPPORTED, "Compressed ID3v2.2 unsupported");
    *audio = 10 + (uint64_t)size + (version == 4 && (flags & 16) ? 10 : 0);
    if (*audio > r->size)
        return fail(r, SB_META_INVALID, "ID3 exceeds file");
    unsigned char *p = malloc(size ? size : 1);
    if (!p)
        return fail(r, SB_META_LIMIT, "ID3 allocation failed");
    if (!read_at(r, 10, p, size)) {
        free(p);
        return 0;
    }
    size_t n = size, at = 0;
    int tag_unsync = (flags & 128) && version < 4;
    if (tag_unsync)
        n = deunsync(p, n);
    if (flags & 64) {
        if (n < 4) {
            free(p);
            return fail(r, SB_META_INVALID, "Truncated ID3 extended header");
        }
        uint32_t extra = version == 4 ? sync32(p) : be(p);
        at = version == 4 ? extra : 4 + (uint64_t)extra;
        if (at > n || at < 4) {
            free(p);
            return fail(r, SB_META_INVALID, "Extended header bounds");
        }
    }
    unsigned frames = 0;
    while (at < n && p[at]) {
        size_t head = version == 2 ? 6 : 10;
        if (n - at < head || ++frames > 4096) {
            fail(r, SB_META_INVALID, "ID3 frame header bounds");
            break;
        }
        char id[5] = {0};
        memcpy(id, p + at, version == 2 ? 3 : 4);
        uint32_t bytes = version == 2
                             ? (uint32_t)p[at + 3] << 16 | (uint32_t)p[at + 4] << 8 | p[at + 5]
                         : version == 4 ? sync32(p + at + 4)
                                        : be(p + at + 4);
        unsigned f = version == 2 ? 0 : p[at + 9];
        size_t body = at + head;
        if (bytes > n - body) {
            fail(r, SB_META_INVALID, "ID3 frame bounds");
            break;
        }
        size_t count = bytes;
        if ((version == 3 && (f & 0xe0)) || (version == 4 && (f & 0x4c))) {
            r->out->warnings++;
            at = body + bytes;
            continue;
        }
        int unsync = tag_unsync || (version == 4 && ((flags & 128) || (f & 2)));
        if (version == 4 && unsync)
            count = deunsync(p + body, count);
        size_t skip = version == 4 && (f & 1) ? 4 : 0;
        if (skip > count) {
            fail(r, SB_META_INVALID, "ID3 data length bounds");
            break;
        }
        unsigned char *v = p + body + skip;
        count -= skip;
        const char *key = !strcmp(id, "TIT2") || !strcmp(id, "TT2")   ? "TITLE"
                          : !strcmp(id, "TPE1") || !strcmp(id, "TP1") ? "ARTIST"
                          : !strcmp(id, "TALB") || !strcmp(id, "TAL") ? "ALBUM"
                          : !strcmp(id, "TPE2") || !strcmp(id, "TP2") ? "ALBUMARTIST"
                          : !strcmp(id, "TRCK") || !strcmp(id, "TRK") ? "TRACKNUMBER"
                          : !strcmp(id, "TPOS") || !strcmp(id, "TPA") ? "DISCNUMBER"
                          : !strcmp(id, "TDRC") || !strcmp(id, "TYER") || !strcmp(id, "TYE")
                              ? "DATE"
                          : !strcmp(id, "TCON") || !strcmp(id, "TCO") ? "GENRE"
                          : !strcmp(id, "TCMP") || !strcmp(id, "TCP") ? "COMPILATION"
                                                                      : NULL;
        if (key && count) {
            unsigned enc = v[0];
            size_t off = 1;
            do {
                size_t term = terminated(v + off, count - off, enc);
                size_t part = term ? term - (enc == 1 || enc == 2 ? 2 : 1) : count - off;
                if (!field(r, key, v + off, part, enc))
                    break;
                if (!term)
                    break;
                off += term;
            } while (version == 4 && off < count &&
                     (!strcmp(key, "ARTIST") || !strcmp(key, "GENRE")));
        } else if ((!strcmp(id, "APIC") || !strcmp(id, "PIC")) && count > 5) {
            unsigned enc = v[0];
            size_t i = 1;
            if (version == 2)
                i = 4;
            else {
                while (i < count && v[i])
                    i++;
                i++;
            }
            if (i < count) {
                i++;
                size_t term = terminated(v + i, count - i, enc);
                if (term && term <= count - i) {
                    i += term;
                    const char *mime = image_type(v + i, count - i);
                    if (mime)
                        artwork(r, "id3-picture", 10 + body + skip + i, count - i, mime, unsync);
                }
            }
        }
        if (r->out->status != SB_META_OK)
            break;
        at = body + bytes;
    }
    free(p);
    return r->out->status == SB_META_OK;
}
typedef struct {
    unsigned rate, kbps, channels, samples, length, version;
} mp3_frame;
static int mpeg(const unsigned char *p, mp3_frame *f) {
    uint32_t v = be(p);
    unsigned version = (v >> 19) & 3, layer = (v >> 17) & 3, index = (v >> 12) & 15,
             sr = (v >> 10) & 3;
    if ((v & 0xffe00000) != 0xffe00000 || version == 1 || layer != 1 || !index || index == 15 ||
        sr == 3)
        return 0;
    static const unsigned rates[] = {44100, 48000, 32000},
                          v1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
                          v2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    f->version = version;
    f->rate = rates[sr] / (version == 3 ? 1 : version == 2 ? 2 : 4);
    f->kbps = version == 3 ? v1[index] : v2[index];
    f->channels = ((v >> 6) & 3) == 3 ? 1 : 2;
    f->samples = version == 3 ? 1152 : 576;
    f->length = (version == 3 ? 144000 : 72000) * f->kbps / f->rate + ((v >> 9) & 1);
    return 1;
}
static int mp3(reader *r) {
    uint64_t at = 0;
    if (!id3(r, &at))
        return 0;
    unsigned char tail[128];
    uint64_t end = r->size;
    if (r->size >= 128 && read_at(r, r->size - 128, tail, 128) && !memcmp(tail, "TAG", 3)) {
        sb_ml_track *t = &r->out->track;
        char temp[31];
        const char *keys[] = {"TITLE", "ARTIST", "ALBUM", "DATE"};
        unsigned offsets[] = {3, 33, 63, 93}, lengths[] = {30, 30, 30, 4};
        for (unsigned i = 0; i < 4; i++) {
            int missing = i == 0   ? !t->title[0]
                          : i == 1 ? !t->artist_count
                          : i == 2 ? !t->album[0]
                                   : !t->year;
            if (missing) {
                memcpy(temp, tail + offsets[i], lengths[i]);
                temp[lengths[i]] = 0;
                size_t n = strlen(temp);
                while (n && temp[n - 1] == ' ')
                    temp[--n] = 0;
                if (!field(r, keys[i], (unsigned char *)temp, n, 0))
                    return 0;
            }
        }
        if (!t->track && !tail[125])
            t->track = tail[126];
        if (!t->genre_count && tail[127] != 255) {
            if (genre(tail[127]))
                strcpy(t->genres[0], genre(tail[127]));
            else
                snprintf(t->genres[0], sizeof t->genres[0], "ID3v1:%u", tail[127]);
            t->genre_count = 1;
        }
        end -= 128;
    }
    if (at >= end)
        return fail(r, SB_META_INVALID, "MP3 audio absent");
    size_t n = (size_t)(end - at > 65536 ? 65536 : end - at);
    unsigned char *p = malloc(n);
    if (!p)
        return fail(r, SB_META_LIMIT, "Audio probe allocation failed");
    if (!read_at(r, at, p, n)) {
        free(p);
        return 0;
    }
    size_t start;
    mp3_frame f, next;
    for (start = 0; start + 4 <= n; start++)
        if (mpeg(p + start, &f) && start + f.length + 4 <= n && mpeg(p + start + f.length, &next) &&
            next.rate == f.rate)
            break;
    if (start + 4 > n) {
        free(p);
        return fail(r, SB_META_INVALID, "No validated MPEG Layer III frames");
    }
    sb_ml_track *t = &r->out->track;
    strcpy(t->codec, "mp3");
    t->sample_rate = f.rate;
    t->channels = f.channels;
    t->bitrate = f.kbps;
    t->audio_offset = at + start;
    t->audio_length = end - t->audio_offset;
    uint64_t frames = 0;
    size_t side = f.version == 3 ? (f.channels == 1 ? 17 : 32) : (f.channels == 1 ? 9 : 17);
    size_t x = start + 4 + ((p[start + 1] & 1) ? 0 : 2) + side;
    if (x + 12 <= n && (!memcmp(p + x, "Xing", 4) || !memcmp(p + x, "Info", 4)) &&
        (be(p + x + 4) & 1))
        frames = be(p + x + 8);
    if (!frames && start + 54 <= n && !memcmp(p + start + 36, "VBRI", 4) && !p[start + 40] &&
        p[start + 41] == 1)
        frames = be(p + start + 50);
    if (frames > t->audio_length / 24 + 1) {
        frames = 0;
        r->out->warnings++;
    }
    t->duration_ms = frames ? frames * f.samples * 1000 / f.rate : t->audio_length * 8 / f.kbps;
    if (frames)
        t->total_samples = frames * f.samples;
    if (!frames)
        r->out->warnings++; /* CBR estimate, never a full-file walk. */
    free(p);
    return 1;
}
static uint32_t crc32_ogg(const unsigned char *p, size_t n) {
    uint32_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint32_t)(i >= 22 && i < 26 ? 0 : p[i]) << 24;
        for (unsigned j = 0; j < 8; j++)
            crc = crc & 0x80000000 ? (crc << 1) ^ 0x04c11db7 : crc << 1;
    }
    return crc;
}
static int ogg(reader *r) {
    unsigned char *packet = malloc(SB_META_MAX), *page = malloc(65536 + 512);
    if (!packet || !page) {
        free(packet);
        free(page);
        return fail(r, SB_META_LIMIT, "Ogg allocation failed");
    }
    uint64_t at = 0;
    size_t used = 0;
    unsigned serial = 0, sequence = 0, packets = 0, pages = 0;
    int continued = 0;
    while (packets < 3) {
        if (++pages > 64) {
            fail(r, SB_META_LIMIT, "Ogg header page bound");
            break;
        }
        if (!read_at(r, at, page, 27))
            break;
        if (memcmp(page, "OggS", 4) || page[4] || ((page[5] & 1) != !!continued) ||
            (pages == 1 && !(page[5] & 2))) {
            fail(r, SB_META_INVALID, "Ogg capture/version/continuation invalid");
            break;
        }
        unsigned segments = page[26];
        if (!read_at(r, at + 27, page + 27, segments))
            break;
        size_t bytes = 0;
        for (unsigned i = 0; i < segments; i++)
            bytes += page[27 + i];
        size_t total = 27 + segments + bytes;
        if (!read_at(r, at + 27 + segments, page + 27 + segments, bytes))
            break;
        if (crc32_ogg(page, total) != le(page + 22)) {
            fail(r, SB_META_INVALID, "Ogg page CRC invalid");
            break;
        }
        if (pages == 1) {
            serial = le(page + 14);
            sequence = le(page + 18);
        } else if (le(page + 14) != serial || le(page + 18) != ++sequence) {
            fail(r, SB_META_UNSUPPORTED, "Multiplexed/discontinuous Ogg headers unsupported");
            break;
        }
        size_t offset = 27 + segments;
        for (unsigned i = 0; i < segments && packets < 3; i++) {
            size_t count = page[27 + i];
            if (count > SB_META_MAX - used) {
                fail(r, SB_META_LIMIT, "Ogg packet exceeds bound");
                break;
            }
            memcpy(packet + used, page + offset, count);
            used += count;
            offset += count;
            continued = count == 255;
            if (!continued) {
                if (used < 7 || memcmp(packet + 1, "vorbis", 6)) {
                    fail(r, SB_META_UNSUPPORTED, "Only Ogg Vorbis is supported");
                    break;
                }
                if (!packets) {
                    if (used < 30 || packet[0] != 1 || le(packet + 7) || !packet[11] ||
                        !le(packet + 12) || !(packet[29] & 1)) {
                        fail(r, SB_META_INVALID, "Vorbis identification invalid");
                        break;
                    }
                    r->out->track.channels = packet[11];
                    r->out->track.sample_rate = le(packet + 12);
                } else if (packets == 1) {
                    if (packet[0] != 3 || !comments(r, packet + 7, used - 7, 1))
                        break;
                } else if (packet[0] != 5) {
                    fail(r, SB_META_INVALID, "Vorbis setup marker invalid");
                    break;
                }
                packets++;
                used = 0;
            }
        }
        if (r->out->status != SB_META_OK)
            break;
        at += total;
    }
    if (r->out->status == SB_META_OK && packets == 3) {
        sb_ml_track *t = &r->out->track;
        strcpy(t->codec, "ogg");
        t->audio_length = r->size;
        /* A bounded tail search can establish EOS for a single logical stream.
         * Chained/multiplexed tails are explicitly refused rather than mis-timed. */
        size_t n = (size_t)(r->size < 65536 ? r->size : 65536);
        unsigned char *tail = malloc(n ? n : 1);
        if (!tail)
            fail(r, SB_META_LIMIT, "Ogg tail allocation failed");
        else if (read_at(r, r->size - n, tail, n)) {
            int eos = 0;
            for (size_t i = 0; i + 27 <= n; i++)
                if (!memcmp(tail + i, "OggS", 4) && tail[i + 4] == 0) {
                    size_t segments = tail[i + 26], end = i + 27 + segments;
                    if (end > n)
                        continue;
                    for (size_t k = 0; k < segments; k++)
                        end += tail[i + 27 + k];
                    if (end == n && (tail[i + 5] & 4) &&
                        crc32_ogg(tail + i, end - i) == le(tail + i + 22)) {
                        if (le(tail + i + 14) != serial) {
                            fail(r, SB_META_UNSUPPORTED, "Chained Ogg tail unsupported");
                            break;
                        }
                        uint64_t granule = 0;
                        for (unsigned j = 0; j < 8; j++)
                            granule |= (uint64_t)tail[i + 6 + j] << (8 * j);
                        if (granule != UINT64_MAX && granule <= UINT64_MAX / 1000) {
                            t->duration_ms = granule * 1000 / t->sample_rate;
                            t->total_samples = granule;
                            eos = 1;
                        }
                        break;
                    }
                }
            if (!eos && r->out->status == SB_META_OK)
                r->out->warnings++;
        }
        free(tail);
        if (t->duration_ms && r->size <= UINT64_MAX / 8)
            t->bitrate = (unsigned)(r->size * 8 / t->duration_ms);
    } else if (r->out->status == SB_META_OK)
        fail(r, SB_META_INVALID, "Incomplete Vorbis headers");
    free(packet);
    free(page);
    return r->out->status == SB_META_OK;
}
void sb_music_metadata(int fd, sb_metadata_result *out) {
    memset(out, 0, sizeof *out);
    out->magic = 0x53424d32;
    reader r = {fd, 0, 0, out};
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 10) {
        fail(&r, SB_META_INVALID, "Not a regular supported audio file");
        return;
    }
    r.size = (uint64_t)st.st_size;
    out->track.file_size = r.size;
    out->track.mtime = st.st_mtime;
    unsigned char magic[10];
    if (!read_at(&r, 0, magic, sizeof magic))
        return;
    if (!memcmp(magic, "fLaC", 4))
        flac(&r);
    else if (!memcmp(magic, "OggS", 4))
        ogg(&r);
    else if (!memcmp(magic, "ID3", 3) || (magic[0] == 255 && (magic[1] & 224) == 224))
        mp3(&r);
    else
        fail(&r, SB_META_UNSUPPORTED, "Audio signature unsupported");
    out->bytes_read = r.read;
}
