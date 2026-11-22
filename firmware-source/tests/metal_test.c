/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t screen[240 * 240];
static uint32_t lcd_position;
static uint32_t lcd_width, lcd_height, lcd_x, lcd_y;
static const uint16_t *lcd_pending;
static uint32_t lcd_count;

static void lcd_sync(void)
{
    if (!lcd_pending)
        return;
    for (uint32_t i = 0; i < lcd_count; i++, lcd_position++)
        screen[(lcd_y + lcd_position / lcd_width) * 240 + lcd_x + lcd_position % lcd_width] = lcd_pending[i];
    lcd_pending = NULL;
}

static void lcd_window(uint32_t x, uint32_t y, uint32_t x_end, uint32_t y_end)
{
    lcd_sync();
    lcd_x = x;
    lcd_y = y;
    lcd_width = x_end - x + 1u;
    lcd_height = y_end - y + 1u;
    lcd_position = 0;
}

static void lcd_data(const void *pixels, uint32_t bytes)
{
    assert(pixels != lcd_pending);
    lcd_sync();
    assert(lcd_position + bytes / 2u <= lcd_width * lcd_height);
    lcd_pending = pixels;
    lcd_count = bytes / 2u;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *pixels)
{
    lcd_sync();
    assert(x + w <= 240 && y + h <= 240);
    for (uint32_t row = 0; row < h; row++)
        memcpy(screen + (y + row) * 240 + x, pixels + row * w, w * sizeof *pixels);
}

#define LCD_STREAMING 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/splash.c"

static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    lcd_sync();
    assert(x + w <= 240 && y + h <= 240);
    for (uint32_t row = 0; row < h; row++)
        for (uint32_t column = 0; column < w; column++)
            screen[(y + row) * 240 + x + column] = swap16(color);
}

static unsigned different(const uint8_t *before)
{
    unsigned count = 0;
    for (unsigned i = 0; i < sizeof metal_map; i++)
        count += before[i] != metal_map[i];
    return count;
}

int main(void)
{
    uint8_t before[sizeof metal_map];
    int centers = 0;
    uint32_t radii = 0;
    for (uint32_t start = 82u; start <= 120u; start++) {
        uint32_t previous = metal_radius_square(start, 0);
        assert(previous == start * start);
        for (uint32_t ms = 1; ms <= 2240u; ms++) {
            uint32_t square = metal_radius_square(start, ms);
            assert(square > previous && square - previous <= 12u);
            previous = square;
        }
        assert(previous == (start + 70u) * (start + 70u));
        assert(metal_radius_square(start, 2880u) == previous);
    }
    palette_set(0);
    assert(!metal_update(100, 170));
    palette_set(METAL_PALETTE);
    assert(metal_update(100, 170));
    assert(!metal_update(240, 170));
    memcpy(before, metal_map, sizeof before);
    assert(metal_update(700, 170) && different(before) > 35);
    memcpy(before, metal_map, sizeof before);
    metal_press(500);
    metal_clock = 0;
    assert(metal_update(700, 170) && different(before) > 55);
    metal_touch.time = 0;
    metal_clock = 0;
    assert(metal_update(700, 170) && different(before) == 0);

    metal_press(800);
    uint32_t first = metal_latest;
    metal_press(900);
    uint32_t second = metal_latest;
    assert(first != second && metal_touches[first].time == 800);
    metal_clock = 0;
    assert(metal_update(1100, 85));
    memcpy(before, metal_map, sizeof before);
    metal_touches[first].time = 0;
    metal_clock = 0;
    assert(metal_update(1100, 85) && different(before) > 15);
    metal_touches[first].time = 800;
    metal_touches[second].time = 0;
    metal_clock = 0;
    assert(metal_update(1100, 85) && different(before) > 15);
    metal_touches[first].time = 0;
    for (uint32_t i = 0; i < METAL_TOUCH_COUNT; i++)
        metal_press(1200 + i * 40);
    for (uint32_t i = 0; i < METAL_TOUCH_COUNT; i++)
        assert(metal_touches[i].time >= 1200 && metal_touches[i].time <= 1480);

    for (uint32_t i = 1; i <= 600u; i++) {
        int32_t x = metal_touch.x, y = metal_touch.y;
        metal_press(i * 4000u);
        int32_t dx = (int32_t)metal_touch.x - x, dy = (int32_t)metal_touch.y - y;
        if (i > 1u)
            assert(dx * dx + dy * dy >= 72 * 72);
        centers += metal_touch.x > 85u && metal_touch.x < 155u &&
                   metal_touch.y > 85u && metal_touch.y < 155u;
        assert(metal_touch.radius >= 82u && metal_touch.radius <= 120u);
        radii |= 1u << ((metal_touch.radius - 82u) / 4u);
    }
    assert(centers > 20);
    assert(radii == 0x3ffu);

    metal_rendering = 1;
    metal_fill(0, 0, 240, 240, pal_bg);
    lcd_sync();
    for (uint32_t y = 0; y < 240; y++)
        for (uint32_t x = 0; x < 240; x++)
            assert(screen[y * 240 + x] == METAL_COLORS[metal_sample(metal_map, x, y) >> 2]);
    assert(screen[90 * 240 + 20] != screen[90 * 240 + 205]);
    cv_begin(22, 30, pal_bg);
    cv_pset(2, 3, 0xffffu);
    cv_pset(2, 4, RGB(0, 0, 0));
    cv_blit(130, 101);
    lcd_sync();
    assert(screen[104 * 240 + 132] == 0xffffu);
    assert(screen[105 * 240 + 132] == 0u);
    assert(screen[104 * 240 + 133] != METAL_EMPTY);
    cv_begin(79, 19, pal_bg);
    cv_pset(2, 17, 0xffffu);
    uint16_t untouched = screen[50 * 240 + 12];
    cv_blit_from(12, 50, 3);
    lcd_sync();
    assert(screen[50 * 240 + 12] == untouched);
    assert(screen[67 * 240 + 14] == 0xffffu);
    assert(screen[68 * 240 + 90] == METAL_COLORS[metal_sample(metal_map, 90, 68) >> 2]);
    metal_rendering = 0;
    metal_fill(0, 0, 240, 240, 0);
    assert(screen[180 * 240 + 60] == 0);
    memset(metal_touches, 0, sizeof metal_touches);
    const uint16_t mark = swap16(RGB(244, 244, 244));
    metal_clock = 0;
    assert(metal_update(10100, 85));
    memcpy(before, metal_map, sizeof before);
    uint8_t last_manual = metal_latest;
    metal_drum_press(10100, 100);
    assert(metal_drum.amount == 100 && metal_latest == last_manual);
    uint8_t drum_x = metal_drum.x, drum_y = metal_drum.y;
    metal_clock = 0;
    assert(metal_update(10100, 85) && different(before) > 8);
    metal_drum_press(10120, 30);
    assert(metal_drum.x == drum_x && metal_drum.y == drum_y && metal_drum.amount > 30);
    metal_clock = 0;
    assert(metal_update(10120 + METAL_DRUM_MS, 85));
    memcpy(before, metal_map, sizeof before);
    metal_drum.time = 0;
    metal_clock = 0;
    assert(metal_update(10120 + METAL_DRUM_MS, 85) && different(before) == 0);
    sloop_splash(0, 0);
    lcd_sync();
    assert(screen[0] == 0u && screen[122 * 240 + 120] == mark);
    assert(screen[73 * 240 + 134] == mark && screen[86 * 240 + 125] == 0u);
    assert(screen[124 * 240 + 142] == 0u && screen[124 * 240 + 133] == mark);
    sloop_splash(500, 500);
    lcd_sync();
    assert(screen[122 * 240 + 120] == 0xffffu && screen[0] == 0u);
    sloop_splash(960, 960);
    lcd_sync();
    for (uint32_t y = 0; y < 240; y++)
        for (uint32_t x = 0; x < 240; x++)
            assert(screen[y * 240 + x] == METAL_COLORS[metal_sample(metal_map, x, y) >> 2]);
    puts("Boot reveal: pear leaf and round notch, expanding white center, same final wallpaper.");
    puts("Liquid metal: background, pulse, spacing, center coverage, and foreground preservation passed.");
    return 0;
}
