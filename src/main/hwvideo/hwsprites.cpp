#include "video.hpp"
#include "hwvideo/hwsprites.hpp"
#include "globals.hpp"
#include "frontend/config.hpp"

#ifdef __DREAMCAST__
#include <kos/dbglog.h>
#include <SDL.h>
#include <cstring>
#define DC_SPRITE_PERF_INTERVAL_MS 30000
#endif

/***************************************************************************
    Video Emulation: OutRun Sprite Rendering Hardware.
    Based on MAME source code.

    Copyright Aaron Giles.
    All rights reserved.
***************************************************************************/

/*******************************************************************************************
*  Out Run/X-Board-style sprites
*
*      Offs  Bits               Usage
*       +0   e------- --------  Signify end of sprite list
*       +0   -h-h---- --------  Hide this sprite if either bit is set
*       +0   ----bbb- --------  Sprite bank
*       +0   -------t tttttttt  Top scanline of sprite + 256
*       +2   oooooooo oooooooo  Offset within selected sprite bank
*       +4   ppppppp- --------  Signed 7-bit pitch value between scanlines
*       +4   -------x xxxxxxxx  X position of sprite (position $BE is screen position 0)
*       +6   -s------ --------  Enable shadows
*       +6   --pp---- --------  Sprite priority, relative to tilemaps
*       +6   ------vv vvvvvvvv  Vertical zoom factor (0x200 = full size, 0x100 = half size, 0x300 = 2x size)
*       +8   y------- --------  Render from top-to-bottom (1) or bottom-to-top (0) on screen
*       +8   -f------ --------  Horizontal flip: read the data backwards if set
*       +8   --x----- --------  Render from left-to-right (1) or right-to-left (0) on screen
*       +8   ------hh hhhhhhhh  Horizontal zoom factor (0x200 = full size, 0x100 = half size, 0x300 = 2x size)
*       +E   dddddddd dddddddd  Scratch space for current address
*
*  Out Run only:
*       +A   hhhhhhhh --------  Height in scanlines - 1
*       +A   -------- -ccccccc  Sprite color palette
*
*  X-Board only:
*       +A   ----hhhh hhhhhhhh  Height in scanlines - 1
*       +C   -------- cccccccc  Sprite color palette
*
*  Final bitmap format:
*
*            -s------ --------  Shadow control
*            --pp---- --------  Sprite priority
*            ----cccc cccc----  Sprite color palette
*            -------- ----llll  4-bit pixel data
*
 *******************************************************************************************/

// Enable for hardware pixel accuracy, where sprite shadowing delayed by 1 clock cycle (slower)
#define PIXEL_ACCURACY 0 

hwsprites::hwsprites()
{
}

hwsprites::~hwsprites()
{
}

void hwsprites::init(const uint8_t* src_sprites)
{
    reset();

    if (src_sprites)
    {
        // Convert S16 tiles to a more useable format
        const uint8_t *spr = src_sprites;

        for (uint32_t i = 0; i < SPRITES_LENGTH; i++)
        {
            uint8_t d3 = *spr++;
            uint8_t d2 = *spr++;
            uint8_t d1 = *spr++;
            uint8_t d0 = *spr++;

            sprites[i] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
        }
    }
}

void hwsprites::reset()
{
    // Clear Sprite RAM buffers
    for (uint16_t i = 0; i < SPRITE_RAM_SIZE; i++)
    {
        ram[i] = 0;
        ramBuff[i] = 0;
    }
}

// Clip areas of the screen in wide-screen mode
void hwsprites::set_x_clip(bool on)
{
    // Clip to central 320 width window.
    if (on)
    {
        x1 = config.s16_x_off;
        x2 = x1 + S16_WIDTH;

        if (config.video.hires)
        {
            x1 <<= 1;
            x2 <<= 1;
        }
    }
    // Allow full wide-screen.
    else
    {
        x1 = 0;
        x2 = config.s16_width;
    }
}

uint8_t hwsprites::read(const uint16_t adr)
{
    uint16_t a = adr >> 1;
    if ((adr & 1) == 1)
        return ram[a] & 0xff;
    else
        return ram[a] >> 8;
}

void hwsprites::write(const uint16_t adr, const uint16_t data)
{
    ram[adr >> 1] = data;
}

// Copy back buffer to main ram, ready for blit
void hwsprites::swap()
{
    uint16_t *src = (uint16_t *)ram;
    uint16_t *dst = (uint16_t *)ramBuff;

    // swap the halves of the road RAM
    for (uint16_t i = 0; i < SPRITE_RAM_SIZE; i++)
    {
        uint16_t temp = *src;
        *src++ = *dst;
        *dst++ = temp;
    }
}

#if PIXEL_ACCURACY

// Reproduces glowy edge around sprites on top of shadows as seen on Hardware.
// Believed to be caused by shadowing being out by one clock cycle / pixel.
//
// 1/ Sprites Drawn on top of Shadow clears the shadow flags for its opaque pixels.
// 2/ Either the flag clear or the sprite itself is offset by one pixel horizontally.
// 
// Thanks to Alex B. for this implementation.

#define draw_pixel()                                                                                  \
{                                                                                                     \
    if (x >= x1 && x < x2)                                                                            \
    {                                                                                                 \
        if (shadow && pix == 0xa)                                                                     \
        {                                                                                             \
            pPixel[x] &= 0xfff;                                                                       \
            pPixel[x] += S16_PALETTE_ENTRIES;                                                         \
        }                                                                                             \
        else if (pix != 0 && pix != 15)                                                               \
        {                                                                                             \
            if (x > x1) pPixel[x-1] &= 0xfff;                                                         \
            pPixel[x] = (pix | color);                                                                \
        }                                                                                             \
    }                                                                                                 \
}

#else

#ifdef __DREAMCAST__

#define draw_pixel_clipped_shadow()                                                                  \
{                                                                                                     \
    if (x >= x1 && x < x2 && pix != 0 && pix != 15)                                                   \
    {                                                                                                 \
        if (pix == 0xa)                                                                               \
        {                                                                                             \
            pPixel[x] &= 0xfff;                                                                       \
            pPixel[x] += S16_PALETTE_ENTRIES;                                                         \
        }                                                                                             \
        else                                                                                          \
        {                                                                                             \
            pPixel[x] = (pix | color);                                                                \
        }                                                                                             \
    }                                                                                                 \
}

#define draw_pixel_clipped_noshadow()                                                                \
{                                                                                                     \
    if (x >= x1 && x < x2 && pix != 0 && pix != 15)                                                   \
    {                                                                                                 \
        pPixel[x] = (pix | color);                                                                    \
    }                                                                                                 \
}

#define draw_pixel_full_shadow()                                                                     \
{                                                                                                     \
    if (x >= 0 && x < config.s16_width && pix != 0 && pix != 15)                                      \
    {                                                                                                 \
        if (pix == 0xa)                                                                               \
        {                                                                                             \
            pPixel[x] &= 0xfff;                                                                       \
            pPixel[x] += S16_PALETTE_ENTRIES;                                                         \
        }                                                                                             \
        else                                                                                          \
        {                                                                                             \
            pPixel[x] = (pix | color);                                                                \
        }                                                                                             \
    }                                                                                                 \
}

#define draw_pixel_full_noshadow()                                                                   \
{                                                                                                     \
    if (x >= 0 && x < config.s16_width && pix != 0 && pix != 15)                                      \
    {                                                                                                 \
        pPixel[x] = (pix | color);                                                                    \
    }                                                                                                 \
}

#else

#define draw_pixel()                                                                                  \
{                                                                                                     \
    if (x >= x1 && x < x2 && pix != 0 && pix != 15)                                                   \
    {                                                                                                 \
        if (shadow && pix == 0xa)                                                                     \
        {                                                                                             \
            pPixel[x] &= 0xfff;                                                                       \
            pPixel[x] += S16_PALETTE_ENTRIES;                                                         \
        }                                                                                             \
        else                                                                                          \
        {                                                                                             \
            pPixel[x] = (pix | color);                                                                \
        }                                                                                             \
    }                                                                                                 \
}

#endif
#endif

#ifdef __DREAMCAST__
#define draw_pixels_forward(draw_pixel_func)                                                          \
{                                                                                                     \
    pix = (pixels >> 28) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >> 24) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >> 20) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >> 16) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >> 12) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >>  8) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >>  4) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >>  0) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
}

#define draw_pixels_reverse(draw_pixel_func)                                                          \
{                                                                                                     \
    pix = (pixels >>  0) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >>  4) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >>  8) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >> 12) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >> 16) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >> 20) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >> 24) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
    pix = (pixels >> 28) & 0xf; while (xacc < 0x200) { draw_pixel_func(); x += xdelta; xacc += hzoom; } xacc -= 0x200; \
}

#define draw_pixels_forward_1x(draw_pixel_func)                                                       \
{                                                                                                     \
    pix = (pixels >> 28) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >> 24) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >> 20) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >> 16) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >> 12) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >>  8) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >>  4) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >>  0) & 0xf; draw_pixel_func(); x += xdelta;                                       \
}

#define draw_pixels_reverse_1x(draw_pixel_func)                                                       \
{                                                                                                     \
    pix = (pixels >>  0) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >>  4) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >>  8) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >> 12) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >> 16) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >> 20) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >> 24) & 0xf; draw_pixel_func(); x += xdelta;                                       \
    pix = (pixels >> 28) & 0xf; draw_pixel_func(); x += xdelta;                                       \
}

#ifdef DREAMCAST_FAST_SPRITES
#define draw_scaled_full_noshadow(pix_expr)                                                           \
{                                                                                                     \
    pix = (pix_expr) & 0xf;                                                                           \
    if (pix != 0 && pix != 15)                                                                        \
    {                                                                                                 \
        const uint16_t dc_pixel = (uint16_t)(pix | color);                                            \
        while (xacc < 0x200)                                                                          \
        {                                                                                             \
            if (x >= 0 && x < dc_screen_width)                                                        \
                pPixel[x] = dc_pixel;                                                                 \
            x += xdelta;                                                                              \
            xacc += hzoom;                                                                            \
        }                                                                                             \
    }                                                                                                 \
    else                                                                                              \
    {                                                                                                 \
        while (xacc < 0x200)                                                                          \
        {                                                                                             \
            x += xdelta;                                                                              \
            xacc += hzoom;                                                                            \
        }                                                                                             \
    }                                                                                                 \
    xacc -= 0x200;                                                                                    \
}

#define draw_pixels_forward_full_noshadow_fast()                                                      \
{                                                                                                     \
    draw_scaled_full_noshadow(pixels >> 28);                                                          \
    draw_scaled_full_noshadow(pixels >> 24);                                                          \
    draw_scaled_full_noshadow(pixels >> 20);                                                          \
    draw_scaled_full_noshadow(pixels >> 16);                                                          \
    draw_scaled_full_noshadow(pixels >> 12);                                                          \
    draw_scaled_full_noshadow(pixels >>  8);                                                          \
    draw_scaled_full_noshadow(pixels >>  4);                                                          \
    draw_scaled_full_noshadow(pixels >>  0);                                                          \
}

#define draw_pixels_reverse_full_noshadow_fast()                                                      \
{                                                                                                     \
    draw_scaled_full_noshadow(pixels >>  0);                                                          \
    draw_scaled_full_noshadow(pixels >>  4);                                                          \
    draw_scaled_full_noshadow(pixels >>  8);                                                          \
    draw_scaled_full_noshadow(pixels >> 12);                                                          \
    draw_scaled_full_noshadow(pixels >> 16);                                                          \
    draw_scaled_full_noshadow(pixels >> 20);                                                          \
    draw_scaled_full_noshadow(pixels >> 24);                                                          \
    draw_scaled_full_noshadow(pixels >> 28);                                                          \
}

#define draw_scaled_full_shadow(pix_expr)                                                             \
{                                                                                                     \
    pix = (pix_expr) & 0xf;                                                                           \
    if (pix == 0xa)                                                                                   \
    {                                                                                                 \
        while (xacc < 0x200)                                                                          \
        {                                                                                             \
            if (x >= 0 && x < dc_screen_width)                                                        \
            {                                                                                         \
                pPixel[x] &= 0xfff;                                                                   \
                pPixel[x] += S16_PALETTE_ENTRIES;                                                     \
            }                                                                                         \
            x += xdelta;                                                                              \
            xacc += hzoom;                                                                            \
        }                                                                                             \
    }                                                                                                 \
    else if (pix != 0 && pix != 15)                                                                   \
    {                                                                                                 \
        const uint16_t dc_pixel = (uint16_t)(pix | color);                                            \
        while (xacc < 0x200)                                                                          \
        {                                                                                             \
            if (x >= 0 && x < dc_screen_width)                                                        \
                pPixel[x] = dc_pixel;                                                                 \
            x += xdelta;                                                                              \
            xacc += hzoom;                                                                            \
        }                                                                                             \
    }                                                                                                 \
    else                                                                                              \
    {                                                                                                 \
        while (xacc < 0x200)                                                                          \
        {                                                                                             \
            x += xdelta;                                                                              \
            xacc += hzoom;                                                                            \
        }                                                                                             \
    }                                                                                                 \
    xacc -= 0x200;                                                                                    \
}

#define draw_pixels_forward_full_shadow_fast()                                                        \
{                                                                                                     \
    draw_scaled_full_shadow(pixels >> 28);                                                            \
    draw_scaled_full_shadow(pixels >> 24);                                                            \
    draw_scaled_full_shadow(pixels >> 20);                                                            \
    draw_scaled_full_shadow(pixels >> 16);                                                            \
    draw_scaled_full_shadow(pixels >> 12);                                                            \
    draw_scaled_full_shadow(pixels >>  8);                                                            \
    draw_scaled_full_shadow(pixels >>  4);                                                            \
    draw_scaled_full_shadow(pixels >>  0);                                                            \
}

#define draw_pixels_reverse_full_shadow_fast()                                                        \
{                                                                                                     \
    draw_scaled_full_shadow(pixels >>  0);                                                            \
    draw_scaled_full_shadow(pixels >>  4);                                                            \
    draw_scaled_full_shadow(pixels >>  8);                                                            \
    draw_scaled_full_shadow(pixels >> 12);                                                            \
    draw_scaled_full_shadow(pixels >> 16);                                                            \
    draw_scaled_full_shadow(pixels >> 20);                                                            \
    draw_scaled_full_shadow(pixels >> 24);                                                            \
    draw_scaled_full_shadow(pixels >> 28);                                                            \
}
#endif
#endif

void hwsprites::render(const uint8_t priority)
{
    const uint32_t numbanks = SPRITES_LENGTH / 0x10000;

#ifdef __DREAMCAST__
    static uint32_t perf_last = SDL_GetTicks();
    static int perf_frames = 0;
    static uint32_t perf_sprites = 0;
    static uint32_t perf_shadow_sprites = 0;
    static uint32_t perf_rows = 0;
    static uint32_t perf_rows_1x = 0;
    static uint32_t perf_fullclip_sprites = 0;
    uint32_t frame_sprites = 0;
    uint32_t frame_shadow_sprites = 0;
    uint32_t frame_rows = 0;
    uint32_t frame_rows_1x = 0;
    uint32_t frame_fullclip_sprites = 0;
    const bool full_clip = (x1 == 0 && x2 == config.s16_width);
#ifdef DREAMCAST_FAST_SPRITES
    const int32_t dc_screen_width = config.s16_width;
#endif
#ifdef DREAMCAST_FAST_SPRITES_VALIDATE
    static uint16_t dc_validate_row[1024];
    static int dc_validate_mismatch_budget = 32;
#endif
#endif

    for (uint16_t data = 0; data < SPRITE_RAM_SIZE; data += 8) 
    {
        // stop when we hit the end of sprite list
        if ((ramBuff[data+0] & 0x8000) != 0) break;

        uint32_t sprpri  = 1 << ((ramBuff[data+3] >> 12) & 3);
        if (sprpri != priority) continue;

        // if hidden, or top greater than/equal to bottom, or invalid bank, punt
        int16_t hide    = (ramBuff[data+0] & 0x5000);
        int32_t height  = (ramBuff[data+5] >> 8) + 1;       
        if (hide != 0 || height == 0) continue;
        
        int16_t bank    = (ramBuff[data+0] >> 9) & 7;
        int32_t top     = (ramBuff[data+0] & 0x1ff) - 0x100;
        uint32_t addr    = ramBuff[data+1];
        int32_t pitch  = ((ramBuff[data+2] >> 1) | ((ramBuff[data+4] & 0x1000) << 3)) >> 8;
        int32_t xpos    =  ramBuff[data+6]; // moved from original structure to accomodate widescreen
        uint8_t shadow  = (ramBuff[data+3] >> 14) & 1;
#if defined(DREAMCAST) && defined(DREAMCAST_SKIP_SPRITE_SHADOWS)
        shadow = 0;
#endif
        int32_t vzoom    = ramBuff[data+3] & 0x7ff;
        int32_t ydelta = ((ramBuff[data+4] & 0x8000) != 0) ? 1 : -1;
        int32_t flip   = (~ramBuff[data+4] >> 14) & 1;
        int32_t xdelta = ((ramBuff[data+4] & 0x2000) != 0) ? 1 : -1;
        int32_t hzoom    = ramBuff[data+4] & 0x7ff;     
        int32_t color   = COLOR_BASE + ((ramBuff[data+5] & 0x7f) << 4);
        int32_t x, y, ytarget, yacc = 0, pix;
            
        // adjust X coordinate
        // note: the threshhold below is a guess. If it is too high, rachero will draw garbage
        // If it is too low, smgp won't draw the bottom part of the road
        if (xpos < 0x80 && xdelta < 0)
            xpos += 0x200;
        xpos -= 0xbe;

        // initialize the end address to the start address
        ramBuff[data+7] = addr;

        // clamp to within the memory region size
        if (numbanks)
            bank %= numbanks;

        const uint32_t* spritedata = sprites + 0x10000 * bank;

        // clamp to a maximum of 8x (not 100% confirmed)
        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;

        // loop from top to bottom
        ytarget = top + ydelta * height;

        // Adjust for widescreen mode
        xpos += config.s16_x_off;

        // Adjust for hi-res mode
        if (config.video.hires)
        {
            xpos <<= 1;
            top <<= 1;
            ytarget <<= 1;
            hzoom >>= 1;
            vzoom >>= 1;
        }

#ifdef __DREAMCAST__
        frame_sprites++;
        if (shadow)
            frame_shadow_sprites++;
        if (full_clip)
            frame_fullclip_sprites++;
#endif

        for (y = top; y != ytarget; y += ydelta)
        {
            // skip drawing if not within the cliprect
            if (y >= 0 && y < config.s16_height)
            {
                uint16_t* pPixel = &video.pixels[y * config.s16_width];
                int32_t xacc = 0;
#ifdef __DREAMCAST__
                frame_rows++;
                if (hzoom == 0x200)
                    frame_rows_1x++;
#endif

                // non-flipped case
                if (flip == 0)
                {
                    // start at the word before because we preincrement below
                    ramBuff[data+7] = (addr - 1);

#ifdef __DREAMCAST__
                    if (full_clip)
                    {
                        if (shadow)
                        {
                            uint16_t sprite_addr = ramBuff[data+7];
                            for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                            {
                                uint32_t pixels = spritedata[++sprite_addr];
                                if (hzoom == 0x200)
                                {
                                    draw_pixels_forward_1x(draw_pixel_full_shadow);
                                }
                                else
                                {
#ifdef DREAMCAST_FAST_SPRITES
                                    draw_pixels_forward_full_shadow_fast();
#else
                                    draw_pixels_forward(draw_pixel_full_shadow);
#endif
                                }

                                if ((pixels & 0x000000f0) == 0x000000f0)
                                    break;
                            }
                            ramBuff[data+7] = sprite_addr;
                        }
                        else
                        {
                            uint16_t sprite_addr = ramBuff[data+7];
                            for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                            {
                                uint32_t pixels = spritedata[++sprite_addr];
                                if (hzoom == 0x200)
                                {
                                    draw_pixels_forward_1x(draw_pixel_full_noshadow);
                                }
                                else
                                {
#ifdef DREAMCAST_FAST_SPRITES_VALIDATE
                                    if (dc_screen_width <= 1024)
                                    {
                                        uint16_t* dc_real_row = pPixel;
                                        const int32_t dc_start_x = x;
                                        const int32_t dc_start_xacc = xacc;
                                        const int32_t dc_start_pix = pix;

                                        std::memcpy(dc_validate_row, dc_real_row,
                                                    (size_t)dc_screen_width * sizeof(uint16_t));
                                        pPixel = dc_validate_row;
                                        draw_pixels_forward(draw_pixel_full_noshadow);
                                        const int32_t dc_original_x = x;
                                        const int32_t dc_original_xacc = xacc;

                                        pPixel = dc_real_row;
                                        x = dc_start_x;
                                        xacc = dc_start_xacc;
                                        pix = dc_start_pix;
                                        draw_pixels_forward_full_noshadow_fast();

                                        if (dc_validate_mismatch_budget > 0)
                                        {
                                            int32_t dc_mismatch = -1;
                                            for (int32_t dc_i = 0; dc_i < dc_screen_width; dc_i++)
                                            {
                                                if (dc_validate_row[dc_i] != dc_real_row[dc_i])
                                                {
                                                    dc_mismatch = dc_i;
                                                    break;
                                                }
                                            }
                                            if (dc_mismatch >= 0 || x != dc_original_x || xacc != dc_original_xacc)
                                            {
                                                dbglog(DBG_INFO,
                                                       "cannonball: sprite fast mismatch dir=fwd data=%u y=%ld x0=%ld xorig=%ld xfast=%ld xaccorig=%ld xaccfast=%ld xdelta=%ld hzoom=%ld pixword=%08lx mismatch=%ld orig=%04x fast=%04x\n",
                                                       data,
                                                       (long)y,
                                                       (long)dc_start_x,
                                                       (long)dc_original_x,
                                                       (long)x,
                                                       (long)dc_original_xacc,
                                                       (long)xacc,
                                                       (long)xdelta,
                                                       (long)hzoom,
                                                       (unsigned long)pixels,
                                                       (long)dc_mismatch,
                                                       dc_mismatch >= 0 ? dc_validate_row[dc_mismatch] : 0,
                                                       dc_mismatch >= 0 ? dc_real_row[dc_mismatch] : 0);
                                                dc_validate_mismatch_budget--;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        draw_pixels_forward(draw_pixel_full_noshadow);
                                    }
#elif defined(DREAMCAST_FAST_SPRITES)
                                    draw_pixels_forward_full_noshadow_fast();
#else
                                    draw_pixels_forward(draw_pixel_full_noshadow);
#endif
                                }

                                if ((pixels & 0x000000f0) == 0x000000f0)
                                    break;
                            }
                            ramBuff[data+7] = sprite_addr;
                        }
                    }
                    else if (shadow)
                    {
                        uint16_t sprite_addr = ramBuff[data+7];
                        for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                        {
                            uint32_t pixels = spritedata[++sprite_addr];
                            if (hzoom == 0x200)
                            {
                                draw_pixels_forward_1x(draw_pixel_clipped_shadow);
                            }
                            else
                            {
                                draw_pixels_forward(draw_pixel_clipped_shadow);
                            }

                            if ((pixels & 0x000000f0) == 0x000000f0)
                                break;
                        }
                        ramBuff[data+7] = sprite_addr;
                    }
                    else
                    {
                        uint16_t sprite_addr = ramBuff[data+7];
                        for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                        {
                            uint32_t pixels = spritedata[++sprite_addr];
                            if (hzoom == 0x200)
                            {
                                draw_pixels_forward_1x(draw_pixel_clipped_noshadow);
                            }
                            else
                            {
                                draw_pixels_forward(draw_pixel_clipped_noshadow);
                            }

                            if ((pixels & 0x000000f0) == 0x000000f0)
                                break;
                        }
                        ramBuff[data+7] = sprite_addr;
                    }
#else
                    for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                    {
                        uint32_t pixels = spritedata[++ramBuff[data+7]]; // Add to base sprite data the vzoom value

                        // draw four pixels
                        pix = (pixels >> 28) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 24) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 20) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 16) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 12) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >>  8) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >>  4) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >>  0) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;

                        // stop if the second-to-last pixel in the group was 0xf
                        if ((pixels & 0x000000f0) == 0x000000f0)
                            break;
                    }
#endif
                }
                // flipped case
                else
                {
                    // start at the word after because we predecrement below
                    ramBuff[data+7] = (addr + 1);

#ifdef __DREAMCAST__
                    if (full_clip)
                    {
                        if (shadow)
                        {
                            uint16_t sprite_addr = ramBuff[data+7];
                            for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                            {
                                uint32_t pixels = spritedata[--sprite_addr];
                                if (hzoom == 0x200)
                                {
                                    draw_pixels_reverse_1x(draw_pixel_full_shadow);
                                }
                                else
                                {
#ifdef DREAMCAST_FAST_SPRITES
                                    draw_pixels_reverse_full_shadow_fast();
#else
                                    draw_pixels_reverse(draw_pixel_full_shadow);
#endif
                                }

                                if ((pixels & 0x0f000000) == 0x0f000000)
                                    break;
                            }
                            ramBuff[data+7] = sprite_addr;
                        }
                        else
                        {
                            uint16_t sprite_addr = ramBuff[data+7];
                            for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                            {
                                uint32_t pixels = spritedata[--sprite_addr];
                                if (hzoom == 0x200)
                                {
                                    draw_pixels_reverse_1x(draw_pixel_full_noshadow);
                                }
                                else
                                {
#ifdef DREAMCAST_FAST_SPRITES_VALIDATE
                                    if (dc_screen_width <= 1024)
                                    {
                                        uint16_t* dc_real_row = pPixel;
                                        const int32_t dc_start_x = x;
                                        const int32_t dc_start_xacc = xacc;
                                        const int32_t dc_start_pix = pix;

                                        std::memcpy(dc_validate_row, dc_real_row,
                                                    (size_t)dc_screen_width * sizeof(uint16_t));
                                        pPixel = dc_validate_row;
                                        draw_pixels_reverse(draw_pixel_full_noshadow);
                                        const int32_t dc_original_x = x;
                                        const int32_t dc_original_xacc = xacc;

                                        pPixel = dc_real_row;
                                        x = dc_start_x;
                                        xacc = dc_start_xacc;
                                        pix = dc_start_pix;
                                        draw_pixels_reverse_full_noshadow_fast();

                                        if (dc_validate_mismatch_budget > 0)
                                        {
                                            int32_t dc_mismatch = -1;
                                            for (int32_t dc_i = 0; dc_i < dc_screen_width; dc_i++)
                                            {
                                                if (dc_validate_row[dc_i] != dc_real_row[dc_i])
                                                {
                                                    dc_mismatch = dc_i;
                                                    break;
                                                }
                                            }
                                            if (dc_mismatch >= 0 || x != dc_original_x || xacc != dc_original_xacc)
                                            {
                                                dbglog(DBG_INFO,
                                                       "cannonball: sprite fast mismatch dir=rev data=%u y=%ld x0=%ld xorig=%ld xfast=%ld xaccorig=%ld xaccfast=%ld xdelta=%ld hzoom=%ld pixword=%08lx mismatch=%ld orig=%04x fast=%04x\n",
                                                       data,
                                                       (long)y,
                                                       (long)dc_start_x,
                                                       (long)dc_original_x,
                                                       (long)x,
                                                       (long)dc_original_xacc,
                                                       (long)xacc,
                                                       (long)xdelta,
                                                       (long)hzoom,
                                                       (unsigned long)pixels,
                                                       (long)dc_mismatch,
                                                       dc_mismatch >= 0 ? dc_validate_row[dc_mismatch] : 0,
                                                       dc_mismatch >= 0 ? dc_real_row[dc_mismatch] : 0);
                                                dc_validate_mismatch_budget--;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        draw_pixels_reverse(draw_pixel_full_noshadow);
                                    }
#elif defined(DREAMCAST_FAST_SPRITES)
                                    draw_pixels_reverse_full_noshadow_fast();
#else
                                    draw_pixels_reverse(draw_pixel_full_noshadow);
#endif
                                }

                                if ((pixels & 0x0f000000) == 0x0f000000)
                                    break;
                            }
                            ramBuff[data+7] = sprite_addr;
                        }
                    }
                    else if (shadow)
                    {
                        uint16_t sprite_addr = ramBuff[data+7];
                        for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                        {
                            uint32_t pixels = spritedata[--sprite_addr];
                            if (hzoom == 0x200)
                            {
                                draw_pixels_reverse_1x(draw_pixel_clipped_shadow);
                            }
                            else
                            {
                                draw_pixels_reverse(draw_pixel_clipped_shadow);
                            }

                            if ((pixels & 0x0f000000) == 0x0f000000)
                                break;
                        }
                        ramBuff[data+7] = sprite_addr;
                    }
                    else
                    {
                        uint16_t sprite_addr = ramBuff[data+7];
                        for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                        {
                            uint32_t pixels = spritedata[--sprite_addr];
                            if (hzoom == 0x200)
                            {
                                draw_pixels_reverse_1x(draw_pixel_clipped_noshadow);
                            }
                            else
                            {
                                draw_pixels_reverse(draw_pixel_clipped_noshadow);
                            }

                            if ((pixels & 0x0f000000) == 0x0f000000)
                                break;
                        }
                        ramBuff[data+7] = sprite_addr;
                    }
#else
                    for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                    {
                        uint32_t pixels = spritedata[--ramBuff[data+7]];

                        // draw four pixels
                        pix = (pixels >>  0) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >>  4) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >>  8) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 12) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 16) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 20) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 24) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 28) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;

                        // stop if the second-to-last pixel in the group was 0xf
                        if ((pixels & 0x0f000000) == 0x0f000000)
                            break;
                    }
#endif
                }
            }
            // accumulate zoom factors; if we carry into the high bit, skip an extra row
            yacc += vzoom; 
            addr += pitch * (yacc >> 9);
            yacc &= 0x1ff;
        }
    }

#ifdef __DREAMCAST__
    perf_sprites += frame_sprites;
    perf_shadow_sprites += frame_shadow_sprites;
    perf_rows += frame_rows;
    perf_rows_1x += frame_rows_1x;
    perf_fullclip_sprites += frame_fullclip_sprites;
    perf_frames++;

    const uint32_t perf_now = SDL_GetTicks();
    if (perf_now - perf_last >= DC_SPRITE_PERF_INTERVAL_MS)
    {
        const uint32_t perf_elapsed = perf_now - perf_last;
        const uint32_t perf_fps = (perf_frames * 1000) / perf_elapsed;
        dbglog(DBG_INFO,
               "cannonball: spriteperf fps=%lu sprites=%lu shadow=%lu fullclip=%lu rows=%lu rows_1x=%lu\n",
               perf_fps,
               (unsigned long)(perf_sprites / perf_frames),
               (unsigned long)(perf_shadow_sprites / perf_frames),
               (unsigned long)(perf_fullclip_sprites / perf_frames),
               (unsigned long)(perf_rows / perf_frames),
               (unsigned long)(perf_rows_1x / perf_frames));
        perf_last = perf_now;
        perf_frames = 0;
        perf_sprites = 0;
        perf_shadow_sprites = 0;
        perf_rows = 0;
        perf_rows_1x = 0;
        perf_fullclip_sprites = 0;
    }
#endif
}
