/* SPDX-License-Identifier: GPL-3.0-only */
/* Open a white center into the liquid-metal palette, then blend the same
 * height map into the wallpaper that the first Home frame will use. */
#define SPLASH_MS 1000u
#define SPLASH_LOGO_MS 170u
#define SPLASH_FADE_START 540u
#define SPLASH_FADE_END 890u

UI_SMALL __attribute__((noinline)) static void sloop_splash(uint32_t now, uint32_t elapsed)
{
    static uint16_t colors[64];
    uint32_t blend = elapsed < SPLASH_FADE_START ? 0u : elapsed >= SPLASH_FADE_END ? 256u :
        (elapsed - SPLASH_FADE_START) * 256u / (SPLASH_FADE_END - SPLASH_FADE_START);
    uint32_t spread = elapsed > SPLASH_LOGO_MS ? elapsed - SPLASH_LOGO_MS : 0u;
    if (spread > 530u)
        spread = 530u;
    uint32_t radius = 8u + spread * 200u / 530u;
    uint32_t white = spread * 70u / 530u;
    if (metal_selected)
        metal_update(now, 0);
    for (uint32_t i = 0; i < 256u; i++) {
        uint32_t dx = (i & 15u) * 16u, dy = (i >> 4) * 16u;
        dx = dx < 120u ? 120u - dx : dx - 120u;
        dy = dy < 122u ? 122u - dy : dy - 122u;
        uint32_t distance = dx > dy ? dx + 3u * dy / 8u : dy + 3u * dx / 8u;
        uint32_t ring = !spread || distance >= radius ? 0u : distance <= white ? 252u :
            (radius - distance) * 252u / (radius - white);
        uint32_t wallpaper = metal_selected ? metal_map[i] : 0u;
        metal_map[i] = (uint8_t)((ring * (256u - blend) + wallpaper * blend) >> 8);
    }
    memcpy(colors, METAL_COLORS, sizeof colors);
    /* The reveal starts from black, and the center starts neutral white. */
    uint16_t dark = metal_selected ? swap16(colors[0]) : pal_bg;
    colors[0] = swap16(rgb565_scale(dark, blend, 256u));
    colors[63] = swap16(rgb565_scale(swap16(colors[63]), blend, 256u) +
                        rgb565_scale(0xffffu, 256u - blend, 256u));
    metal_colors = colors;
    metal_pear_wipe = white * white;
    metal_pear_visible = white < 64u;
    metal_rendering = 1;
    metal_fill(0, 0, 240, 240, pal_bg);
    metal_pear_visible = 0;
    metal_rendering = 0;
    metal_colors = METAL_COLORS;
}
