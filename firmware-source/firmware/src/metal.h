/* SPDX-License-Identifier: GPL-3.0-only */
/* LCD-only liquid metal. Interpolate four small height maps, then displace
 * their sample coordinates by the sum of recent radial pulses. */
#include "metal_art.h"
#include "pear_art.h"

#define METAL_EMPTY 0x139du
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color);
static uint8_t metal_rendering, metal_pear_visible;
static uint32_t metal_pear_wipe;
static uint32_t metal_clock, metal_seed = 0x6312be9du;
static uint8_t metal_map[256];
static const uint16_t *metal_colors = METAL_COLORS;
#define METAL_STRIP_ROWS 4u
#ifdef LCD_STREAMING
static uint16_t metal_rows[2][240 * METAL_STRIP_ROWS];
#else
static uint16_t metal_rows[240 * METAL_STRIP_ROWS];
#endif
#define METAL_TOUCH_COUNT 8u
static struct {
    uint32_t time;
    uint8_t x, y, radius;
} metal_touches[METAL_TOUCH_COUNT];
static uint8_t metal_latest;
#define metal_touch metal_touches[metal_latest]

UI_SMALL static uint32_t metal_random(void)
{
    metal_seed = metal_seed * 1664525u + 1013904223u;
    return metal_seed >> 16;
}

UI_SMALL static void metal_press(uint32_t now)
{
    uint32_t x, y, i;
    if (!metal_selected)
        return;
    metal_seed ^= now;
    for (i = 0; i < 20u; i++) {
        int32_t dx, dy;
        x = 12u + metal_random() % 216u;
        y = 12u + metal_random() % 216u;
        dx = (int32_t)x - metal_touch.x;
        dy = (int32_t)y - metal_touch.y;
        if (!metal_touch.time || dx * dx + dy * dy >= 72 * 72)
            break;
    }
    if (i == 20u) {
        x = metal_touch.x < 120u ? 227u : 12u;
        y = metal_touch.y < 120u ? 227u : 12u;
    }
    metal_latest = (metal_latest + 1u) % METAL_TOUCH_COUNT;
    metal_touch.x = (uint8_t)x;
    metal_touch.y = (uint8_t)y;
    metal_touch.radius = (uint8_t)(82u + metal_random() % 39u);
    metal_touch.time = now ? now : 1u;
}

#define METAL_DRUM_MS 360u
static struct {
    uint32_t time;
    uint8_t x, y, amount;
} metal_drum;

UI_SMALL static void metal_drum_press(uint32_t now, uint32_t amount)
{
    if (!metal_selected)
        return;
    uint32_t age = now - metal_drum.time;
    if (metal_drum.time && age < METAL_DRUM_MS) {
        uint32_t left = metal_drum.amount * (METAL_DRUM_MS - age) / METAL_DRUM_MS;
        if (left > amount)
            amount = left;
    } else {
        uint32_t best = 0, light = 0;
        for (uint32_t i = 0; i < 8u; i++) {
            uint32_t x = 2u + metal_random() % 12u, y = 2u + metal_random() % 12u;
            uint32_t at = y * 16u + x;
            if (!i || metal_map[at] > light) {
                best = at;
                light = metal_map[at];
            }
        }
        metal_drum.x = (uint8_t)((best & 15u) * 16u);
        metal_drum.y = (uint8_t)((best >> 4) * 16u);
    }
    metal_drum.time = now ? now : 1u;
    metal_drum.amount = (uint8_t)amount;
}

UI_SMALL static uint32_t metal_sample(const uint8_t *map, uint32_t x, uint32_t y)
{
    uint32_t i = (y & ~15u) + (x >> 4), fx = x & 15u, fy = y & 15u;
    uint32_t a = map[i] * (16u - fx) + map[i + 1u] * fx;
    uint32_t b = map[i + 16u] * (16u - fx) + map[i + 17u] * fx;
    return (a * (16u - fy) + b * fy) >> 8;
}

UI_SMALL static uint32_t metal_coord(int32_t n)
{
    return n < 0 ? 0u : n > 239 ? 239u : (uint32_t)n;
}

static uint32_t metal_radius_square(uint32_t start, uint32_t elapsed)
{
    /* Grow by 1/32 pixel per ms, retaining that precision when squaring. */
    uint32_t radius = start * 32u + (elapsed < 2240u ? elapsed : 2240u);
    return radius * radius >> 10;
}

UI_SMALL static int metal_update(uint32_t now, uint32_t interval)
{
    struct {
        int32_t x, y;
        uint32_t square, inverse, strength;
    } active[METAL_TOUCH_COUNT + 1u];
    uint8_t base[256];
    uint32_t phase, frame, blend, count = 0;
    if (!metal_selected || (metal_clock && now - metal_clock < interval))
        return 0;
    metal_clock = now ? now : 1u;
    /* Seven full cycles in 102.4 s: 40% faster, without a wide multiply. */
    phase = (now % 102400u) * 7u / 100u;
    frame = (phase >> 8) & 3u;
    blend = phase & 255u;
    for (uint32_t j = 0; j < METAL_TOUCH_COUNT; j++) {
        uint32_t time = metal_touches[j].time, elapsed = now - time, age = elapsed / 16u;
        if (!time || age >= 180u)
            continue;
        uint32_t square = metal_radius_square(metal_touches[j].radius, elapsed);
        active[count].x = metal_touches[j].x;
        active[count].y = metal_touches[j].y;
        active[count].square = square;
        active[count].inverse = (1u << 24) / square;
        uint32_t strength = age < 32u ? 30u + age * 3u : (180u - age) * 126u / 148u;
        active[count].strength = strength * 12u / 5u;
        count++;
    }
    uint32_t drum_age = now - metal_drum.time;
    if (metal_drum.time && drum_age < METAL_DRUM_MS) {
        uint32_t square = metal_radius_square(104u, drum_age * 3u);
        active[count].x = metal_drum.x;
        active[count].y = metal_drum.y;
        active[count].square = square;
        active[count].inverse = (1u << 24) / square;
        active[count].strength = metal_drum.amount * 5u * (METAL_DRUM_MS - drum_age) / (METAL_DRUM_MS * 2u);
        count++;
    }
    for (uint32_t i = 0, pos = 0; i < 192u; i += 3u) {
        uint32_t a = unpack24(METAL_MESH[frame] + i);
        uint32_t b = unpack24(METAL_MESH[(frame + 1u) & 3u] + i);
        for (uint32_t j = 0; j < 4u; j++, a >>= 6, b >>= 6)
            base[pos++] = (uint8_t)(((a & 63u) * (256u - blend) + (b & 63u) * blend) >> 6);
    }
    for (uint32_t i = 0; i < 256u; i++) {
        int32_t x = (int32_t)((i & 15u) * 16u), y = (int32_t)((i >> 4) * 16u);
        int32_t sx = x, sy = y;
        for (uint32_t j = 0; j < count; j++) {
            int32_t dx = x - active[j].x, dy = y - active[j].y;
            uint32_t distance = (uint32_t)(dx * dx + dy * dy);
            if (distance < active[j].square) {
                uint32_t remain = 256u - ((distance * active[j].inverse) >> 16);
                int32_t push = (int32_t)((remain * remain * active[j].strength) >> 16);
                /* Keep the innermost samples on their own side of the pulse. */
                if (push > 255)
                    push = 255;
                sx -= (dx * push) >> 8;
                sy -= (dy * push) >> 8;
            }
        }
        metal_map[i] = sx == x && sy == y ? base[i] :
            (uint8_t)metal_sample(base, metal_coord(sx), metal_coord(sy));
    }
    return 1;
}

UI_SMALL static void metal_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                               uint16_t *pixels, int background_only)
{
    for (uint32_t row = 0; row < h; row++) {
        uint32_t sy = y + row;
        const uint8_t *mesh = metal_map + (sy & ~15u);
        int fy = (int)(sy & 15u);
        for (uint32_t col = x; col < x + w;) {
            const uint8_t *cell = mesh + (col >> 4);
            int left = cell[0] * (16 - fy) + cell[16] * fy;
            int right = cell[1] * (16 - fy) + cell[17] * fy;
            int step = right - left;
            int value = left * 16 + step * (int)(col & 15u);
            uint32_t limit = (col | 15u) + 1u;
            if (limit > x + w)
                limit = x + w;
            /* Interpolate vertically once per cell, then step across its pixels. */
            for (; col < limit; col++, pixels++, value += step) {
                if (background_only || *pixels == METAL_EMPTY)
                    *pixels = metal_colors[value >> 10];
            }
        }
    }
}

/* Boot-only mask, composited before each full-width strip is transmitted. */
UI_SMALL static void metal_pear_overlay(uint32_t y, uint32_t h, uint16_t *pixels)
{
    for (uint32_t row = 0; row < h; row++, pixels += 240u) {
        uint32_t index = y + row - PEAR_TOP;
        if (index >= sizeof PEAR_SPANS / sizeof PEAR_SPANS[0])
            continue;
        int32_t dy = (int32_t)(y + row) - 122;
        for (uint32_t x = PEAR_SPANS[index][0]; x < PEAR_SPANS[index][1]; x++) {
            int32_t dx = (int32_t)x - 120;
            if ((uint32_t)(dx * dx + dy * dy) >= metal_pear_wipe)
                pixels[x] = swap16(RGB(244, 244, 244));
        }
    }
}

UI_SMALL static void metal_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    if (!metal_rendering || color != pal_bg || x >= 240u || y >= 240u) {
        lcd_fill(x, y, w, h, color);
        return;
    }
    if (w > 240u - x)
        w = 240u - x;
    if (h > 240u - y)
        h = 240u - y;
    if (!w || !h)
        return;
#ifdef LCD_STREAMING
    lcd_window(x, y, x + w - 1u, y + h - 1u);
    uint32_t buffer = 0;
    while (h) {
        uint32_t rows = h > METAL_STRIP_ROWS ? METAL_STRIP_ROWS : h;
        /* Draw while the previous strip transfers from the other buffer. */
        metal_blit(x, y, w, rows, metal_rows[buffer], 1);
        if (metal_pear_visible)
            metal_pear_overlay(y, rows, metal_rows[buffer]);
        lcd_data(metal_rows[buffer], w * rows * 2u);
        buffer ^= 1u;
#else
    while (h) {
        uint32_t rows = h > METAL_STRIP_ROWS ? METAL_STRIP_ROWS : h;
        lcd_sync();
        metal_blit(x, y, w, rows, metal_rows, 1);
        if (metal_pear_visible)
            metal_pear_overlay(y, rows, metal_rows);
        lcd_blit(x, y, w, rows, metal_rows);
#endif
        y += rows;
        h -= rows;
    }
}
