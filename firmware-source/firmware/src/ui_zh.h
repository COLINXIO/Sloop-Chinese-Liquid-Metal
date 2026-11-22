/* SPDX-License-Identifier: GPL-3.0-only */
#include "zh_catalog.h"

#define ZH_BUFFER 112u

UI_SMALL static uint32_t zh_word(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '#';
}

UI_SMALL static const unsigned char *zh_lookup(const char *first, const char *last)
{
    uint32_t hash = 2166136261u;
    for (; first < last; first++) {
        uint32_t c = (unsigned char)*first;
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        hash = (hash ^ c) * 16777619u;
    }
    uint32_t bucket = (hash >> 20) & 15u;
    uint32_t lo = ZH_BUCKETS[bucket], hi = ZH_BUCKETS[bucket + 1u];
    hash &= 0xfffffu;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        uint32_t key = unpack24(ZH_HASHES + (mid * 20u >> 3));
        key = (key >> ((mid & 1u) * 4u)) & 0xfffffu;
        if (key == hash) {
            uint32_t bit = mid * 13u;
            uint32_t word = unpack24(ZH_OFFSETS + (bit >> 3));
            return ZH_TEXT + ((word >> (bit & 7u)) & 8191u);
        }
        if (key < hash)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return (const unsigned char *)0;
}

UI_SMALL static uint32_t zh_bytes(const char *s)
{
    return zh_glyph((const unsigned char *)s) >= 0 ? 3u : 1u;
}

/* Glossary strings use ASCII, one byte for frequent Chinese glyphs, and
 * two bytes for other glyphs. Decode only the label currently being drawn. */
UI_SMALL static uint32_t zh_expand(char out[ZH_BUFFER], uint32_t n, const unsigned char *s)
{
    while (*s) {
        uint32_t c = *s++;
        if (c < 128u) {
            if (n + 1u >= ZH_BUFFER)
                break;
            out[n++] = (char)c;
        } else {
            if (n + 3u >= ZH_BUFFER)
                break;
            if (c < 0xe0u)
                c = ZH_COMMON[c - 0x80u];
            else {
                uint32_t index = (c - 0xe0u) * 128u + (*s++ - 0x80u);
                c = FONT_ZH[index].code;
            }
            out[n++] = (char)(0xe0u | (c >> 12));
            out[n++] = (char)(0x80u | ((c >> 6) & 63u));
            out[n++] = (char)(0x80u | (c & 63u));
        }
    }
    return n;
}

/* Prefer exact phrases, then translate recognized words in dynamic labels.
 * The original string remains available to icon selection and MIDI. */
UI_SMALL static const char *zh_translate(const char *s, char out[ZH_BUFFER])
{
    const char *first = s, *last = s, *p;
    const unsigned char *hit;
    uint32_t n = 0, changed = 0;
    while (*last)
        last++;
    while (first < last && *first == ' ')
        first++;
    while (first < last && last[-1] == ' ')
        last--;
    hit = zh_lookup(first, last);
    if (hit) {
        out[zh_expand(out, 0, hit)] = 0;
        return out;
    }
    if (*s == '@')
        return s;
    for (p = s; *p; p++)
        if (*p == '.' &&
            ((p[1] == 'C' && p[2] == 'O' && p[3] == 'M') ||
             (p[1] == 'c' && p[2] == 'o' && p[3] == 'm')))
            return s;                                      /* links and attribution */
    for (p = s; *p;) {
        const char *begin = p;
        uint32_t bytes;
        if (zh_word((unsigned char)*p)) {
            do {
                p++;
            } while (zh_word((unsigned char)*p));
            hit = zh_lookup(begin, p);
        } else {
            p += zh_bytes(p);
            hit = (const unsigned char *)0;
        }
        if (hit) {
            changed = 1;
            n = zh_expand(out, n, hit);
            if (n + 3u >= ZH_BUFFER)
                break;
        } else {
            bytes = (uint32_t)(p - begin);
            if (n + bytes >= ZH_BUFFER)
                break;
            while (begin < p)
                out[n++] = *begin++;
        }
    }
    out[n] = 0;
    return changed ? out : s;
}

/* Shrink a label by complete glyphs in the same units used by the renderer.
 * The destination may alias the source. */
UI_SMALL static void zh_cut(char *out, uint32_t capacity, const char *s,
                            const felucca_font_t *f, int32_t pixels)
{
    uint32_t n = 0;
    while (*s) {
        uint32_t bytes = zh_bytes(s);
        int32_t advance = bytes == 3u ? (f->h > 16u ? 2 * ZH_ADV : ZH_ADV)
                                     : f->adv[glyph(f, (unsigned char)*s)];
        if (n + bytes >= capacity || pixels < advance)
            break;
        pixels -= advance;
        while (bytes--)
            out[n++] = *s++;
    }
    out[n] = 0;
}

UI_SMALL static void zh_fit(char *out, uint32_t capacity, const char *input,
                            const felucca_font_t *f, int32_t pixels)
{
    char translated[ZH_BUFFER];
    zh_cut(out, capacity, zh_translate(input, translated), f, pixels);
}

UI_SMALL static int32_t zh_width(const felucca_font_t *f, const char *s)
{
    char translated[ZH_BUFFER];
    return text_w(f, zh_translate(s, translated));
}

UI_SMALL static int32_t zh_render(int32_t x, int32_t y, const felucca_font_t *f,
                                 const char *s, uint16_t c)
{
    char translated[ZH_BUFFER];
    const char *label = zh_translate(s, translated);
    if (label != s && x >= 0 && text_w(f, label) > (int32_t)cv_w - x) {
        zh_cut(translated, sizeof translated, label, f, (int32_t)cv_w - x);
        label = translated;
    }
    return cv_text(x, y, f, label, c);
}

UI_SMALL static void zh_box(uint32_t x, uint32_t y, uint32_t w, const felucca_font_t *f,
                            const char *s, uint16_t c, int align)
{
    char translated[ZH_BUFFER];
    zh_fit(translated, sizeof translated, s, f, (int32_t)w);
    draw_text_box(x, y, w, f, translated, c, align);
}
