/***************************************************************************
    SDL2 Hardware Surface Video Rendering.  
    
    Known Bugs:
    - Missing Scanlines

    Copyright Manuel Alfayate, Chris White.
    See license.txt for more details.
***************************************************************************/

#include <iostream>

#include "rendersurface.hpp"
#include "frontend/config.hpp"

#ifdef __DREAMCAST__
#include <kos/dbglog.h>
#endif

#ifdef __DREAMCAST__
#define DC_RENDER_TRACE(...) dbglog(DBG_INFO, __VA_ARGS__)
#else
#define DC_RENDER_TRACE(...) do {} while (0)
#endif

#ifdef __DREAMCAST__
static int dc_next_power_of_two(int value)
{
    int pot = 1;
    while (pot < value)
        pot <<= 1;
    return pot;
}
#endif

Render::Render(void)
{
#ifdef __DREAMCAST__
    screen_pixels_16_left = NULL;
    screen_pixels_16_right = NULL;
    texture_right = NULL;
    dc_left_width = 0;
    dc_right_width = 0;
    dc_right_texture_width = 0;
#endif
}

Render::~Render(void)
{
}

bool Render::init(int src_width, int src_height,
                    int scale,
                    int video_mode,
                    int scanlines)
{
    this->src_width  = src_width;
    this->src_height = src_height;
    this->scale      = scale;
    this->video_mode = video_mode;
    this->scanlines  = scanlines;

    // Setup SDL Screen size
    if (!RenderBase::sdl_screen_size())
        return false;

#ifdef __DREAMCAST__
    video_mode = video_settings_t::MODE_STRETCH;
    this->video_mode = video_mode;
#endif

    int flags = SDL_WINDOW_SHOWN;

    // In SDL2, we calculate the output dimensions, but then in draw_frame() we won't do any scaling: SDL2
    // will do that for us, using the rects passed to SDL_RenderCopy().
    // scn_* -> physical screen dimensions OR window dimensions. On FULLSCREEN MODE it has the physical screen
    //		dimensions and in windowed mode it has the window dimensions.
    // src_* -> real, internal, frame dimensions. Will ALWAYS be 320 or 398 x 224. NEVER CHANGES. 
    // corrected_scn_width_* -> output screen size for scaling.
    // In windowed mode it's the size of the window. 
   
    // --------------------------------------------------------------------------------------------
    // Full Screen Mode
    // --------------------------------------------------------------------------------------------
    if (video_mode == video_settings_t::MODE_FULL || video_mode == video_settings_t::MODE_STRETCH)
    {
	    flags |= (SDL_WINDOW_FULLSCREEN); // Set SDL flag

	    // Fullscreen window size: SDL2 ignores w and h in SDL_CreateWindow() if FULLSCREEN flag
	    // is enable, which is fine, so the window will be fullscreen of the physical videomode
	    // size, but then, if we want to preserve ratio, we need dst_width bigger than src_width.	
	    scn_width  = orig_width;
        scn_height = orig_height;

	    src_rect.w = src_width;
	    src_rect.h = src_height;
	    src_rect.x = 0;
	    src_rect.y = 0;
        
        if (video_mode == video_settings_t::MODE_FULL)
        {
            uint32_t w = (scn_width << 16) / src_width;
            uint32_t h = (scn_height << 16) / src_height;
            dst_rect.w = (src_width * std::min(w, h)) >> 16;
            dst_rect.h = (src_height * std::min(w, h)) >> 16;

            screen_xoff = scn_width - dst_rect.w;
            if (screen_xoff)
                screen_xoff = (screen_xoff / 2);
            
            screen_yoff = scn_height - dst_rect.h;
            if (screen_yoff)
                screen_yoff = screen_yoff / 2;

            dst_rect.x = screen_xoff;
            dst_rect.y = screen_yoff;
        }
        else
        {
            dst_rect.x = 0;
            dst_rect.y = 0;
            dst_rect.w = scn_width;
            dst_rect.h = scn_height;
        }


        SDL_ShowCursor(false);
     }
   
    // --------------------------------------------------------------------------------------------
    // Windowed Mode
    // --------------------------------------------------------------------------------------------
    else
    {
        this->video_mode = video_settings_t::MODE_WINDOW;
       
        scn_width  = src_width  * scale;
        scn_height = src_height * scale;

	    src_rect.w = src_width;
	    src_rect.h = src_height;
	    src_rect.x = 0;
	    src_rect.y = 0;
	    dst_rect.w = scn_width;
	    dst_rect.h = scn_height;
        dst_rect.x = 0;
        dst_rect.y = 0;

#ifdef __DREAMCAST__
        SDL_ShowCursor(false);
#else
        SDL_ShowCursor(true);
#endif
    }

#ifdef __DREAMCAST__
    const int bpp = 16;
    const uint32_t rmask = 0x7C00;
    const uint32_t gmask = 0x03E0;
    const uint32_t bmask = 0x001F;
    const uint32_t amask = 0x8000;
#else
    const int bpp = 32;
    const uint32_t rmask = 0;
    const uint32_t gmask = 0;
    const uint32_t bmask = 0;
    const uint32_t amask = 0;
#endif

    // Frees (Deletes) existing surface
    if (surface)
    {
        SDL_FreeSurface(surface);
        surface = NULL;
    }

    surface = SDL_CreateRGBSurface(0,
                                  src_width,
                                  src_height,
                                  bpp,
                                  rmask,
                                  gmask,
                                  bmask,
                                  amask);

    if (!surface)
    {
        std::cerr << "Surface creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, config.video.filtering ? "linear" : "nearest");
#ifdef __DREAMCAST__
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    DC_RENDER_TRACE("cannonball: Dreamcast video layout src=%dx%d screen=%dx%d dst=%d,%d %dx%d mode=%d\n",
                    src_width, src_height, scn_width, scn_height,
                    dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h, video_mode);
#endif
    window = SDL_CreateWindow(
        "Cannonball", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, scn_width, scn_height,
        flags);
    if (!window)
    {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        DC_RENDER_TRACE("cannonball: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

#ifdef __DREAMCAST__
    SDL_ShowCursor(false);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
#else
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");
    renderer = SDL_CreateRenderer(window, 1, SDL_RENDERER_PRESENTVSYNC);
#endif
    if (!renderer)
    {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        DC_RENDER_TRACE("cannonball: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        window = NULL;
        return false;
    }

#ifdef __DREAMCAST__
    SDL_RendererInfo renderer_info;
    if (SDL_GetRendererInfo(renderer, &renderer_info) == 0)
    {
        DC_RENDER_TRACE("cannonball: SDL renderer selected name=%s flags=0x%x formats=%u\n",
                        renderer_info.name ? renderer_info.name : "(null)",
                        static_cast<unsigned>(renderer_info.flags),
                        static_cast<unsigned>(renderer_info.num_texture_formats));
    }
#endif

    int texture_width = src_width;
    int texture_height = src_height;
#ifdef __DREAMCAST__
    dc_left_width = src_width > 256 ? 256 : src_width;
    dc_right_width = src_width - dc_left_width;
    dc_right_texture_width = dc_right_width > 0 ? dc_next_power_of_two(dc_right_width) : 0;
    texture_width = dc_left_width;
    texture_height = 256;
#endif

    texture = SDL_CreateTexture(renderer,
#ifdef __DREAMCAST__
                               SDL_PIXELFORMAT_ARGB1555,
#else
                               SDL_PIXELFORMAT_ARGB8888,
#endif
                               SDL_TEXTUREACCESS_STREAMING,
                               texture_width, texture_height);
    if (!texture)
    {
        std::cerr << "Texture creation failed: " << SDL_GetError() << std::endl;
        DC_RENDER_TRACE("cannonball: SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        renderer = NULL;
        window = NULL;
        return false;
    }

#ifdef __DREAMCAST__
    screen_pixels_16_left = (uint16_t*)SDL_malloc((size_t)dc_left_width * src_height * sizeof (uint16_t));
    screen_pixels_16_right = dc_right_width > 0
                            ? (uint16_t*)SDL_malloc((size_t)dc_right_width * src_height * sizeof (uint16_t))
                            : NULL;
    if (!screen_pixels_16_left || (dc_right_width > 0 && !screen_pixels_16_right))
    {
        std::cerr << "Dreamcast pixel buffer allocation failed" << std::endl;
        SDL_free(screen_pixels_16_left);
        SDL_free(screen_pixels_16_right);
        screen_pixels_16_left = NULL;
        screen_pixels_16_right = NULL;
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        texture = NULL;
        renderer = NULL;
        window = NULL;
        return false;
    }

    if (dc_right_width > 0)
    {
        texture_right = SDL_CreateTexture(renderer,
                                          SDL_PIXELFORMAT_ARGB1555,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          dc_right_texture_width, texture_height);
        if (!texture_right)
        {
            std::cerr << "Right texture creation failed: " << SDL_GetError() << std::endl;
            DC_RENDER_TRACE("cannonball: SDL_CreateTexture right failed: %s\n", SDL_GetError());
            SDL_free(screen_pixels_16_left);
            SDL_free(screen_pixels_16_right);
            screen_pixels_16_left = NULL;
            screen_pixels_16_right = NULL;
            SDL_DestroyTexture(texture);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            texture = NULL;
            renderer = NULL;
            window = NULL;
            return false;
        }
    }

    src_rect.w = dc_left_width;
    src_rect.h = src_height;
    src_rect_right.x = 0;
    src_rect_right.y = 0;
    src_rect_right.w = dc_right_width;
    src_rect_right.h = src_height;

    const int left_dst_width = (dst_rect.w * dc_left_width) / src_width;
    dst_rect_right.x = dst_rect.x + left_dst_width;
    dst_rect_right.y = dst_rect.y;
    dst_rect_right.w = dst_rect.w - left_dst_width;
    dst_rect_right.h = dst_rect.h;
    dst_rect.w = left_dst_width;

    DC_RENDER_TRACE("cannonball: Dreamcast split textures left=%dx%d right=%dx%d right_tex=%dx%d dst_left=%d,%d %dx%d dst_right=%d,%d %dx%d bpp=%d\n",
                    dc_left_width, src_height,
                    dc_right_width, src_height,
                    dc_right_texture_width, texture_height,
                    dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h,
                    dst_rect_right.x, dst_rect_right.y, dst_rect_right.w, dst_rect_right.h,
                    bpp);
#else
    // Convert the SDL pixel surface to 32 bit.
    // This is potentially a larger surface area than the internal pixel array.
    screen_pixels = (uint32_t*)surface->pixels;
#endif
    
    // SDL Pixel Format Information
    Rshift = surface->format->Rshift;
    Gshift = surface->format->Gshift;
    Bshift = surface->format->Bshift;
    Rmask  = surface->format->Rmask;
    Gmask  = surface->format->Gmask;
    Bmask  = surface->format->Bmask;

    return true;
}

void Render::disable()
{
#ifdef __DREAMCAST__
    SDL_free(screen_pixels_16_left);
    SDL_free(screen_pixels_16_right);
    screen_pixels_16_left = NULL;
    screen_pixels_16_right = NULL;
    SDL_DestroyTexture(texture_right);
    texture_right = NULL;
#endif
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

bool Render::start_frame()
{
    return true;
}

bool Render::finalize_frame()
{
#ifdef __DREAMCAST__
    static uint32_t perf_last = SDL_GetTicks();
    static int perf_frames = 0;
    static uint32_t perf_update = 0;
    static uint32_t perf_copy = 0;
    static uint32_t perf_present = 0;

    uint32_t perf_start = SDL_GetTicks();
    SDL_UpdateTexture(texture, &src_rect, screen_pixels_16_left, dc_left_width * sizeof (Uint16));
    if (texture_right)
        SDL_UpdateTexture(texture_right, &src_rect_right, screen_pixels_16_right, dc_right_width * sizeof (Uint16));
    perf_update += SDL_GetTicks() - perf_start;
#else
    SDL_UpdateTexture(texture, NULL, screen_pixels, src_width * sizeof (Uint32));
#endif
#ifdef __DREAMCAST__
    perf_start = SDL_GetTicks();
#endif
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, &src_rect, &dst_rect);
#ifdef __DREAMCAST__
    if (texture_right)
        SDL_RenderCopy(renderer, texture_right, &src_rect_right, &dst_rect_right);
#endif
#ifdef __DREAMCAST__
    perf_copy += SDL_GetTicks() - perf_start;
#endif

    // Very basic scanlines
    if (scanlines && scale != 1)
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, ((scanlines - 1) << 8) / 100);
        SDL_Rect r;
        r.x = dst_rect.x;
        r.w = dst_rect.w;
        r.h = scale >> 1;
        r.y = scale >> 1;

        for (; r.y < dst_rect.h; r.y += scale)
            SDL_RenderDrawRect(renderer, &r);
    }

#ifdef __DREAMCAST__
    perf_start = SDL_GetTicks();
#endif
    SDL_RenderPresent(renderer);
#ifdef __DREAMCAST__
    perf_present += SDL_GetTicks() - perf_start;
    perf_frames++;
    const uint32_t perf_now = SDL_GetTicks();
    if (perf_now - perf_last >= 1000)
    {
        DC_RENDER_TRACE("cannonball: renderperf fps=%d update=%lu copy=%lu present=%lu\n",
                        perf_frames,
                        (unsigned long)(perf_update / perf_frames),
                        (unsigned long)(perf_copy / perf_frames),
                        (unsigned long)(perf_present / perf_frames));
        perf_last = perf_now;
        perf_frames = 0;
        perf_update = 0;
        perf_copy = 0;
        perf_present = 0;
    }
#endif
    return true;
}

void Render::draw_frame(uint16_t* pixels)
{
#ifdef __DREAMCAST__
    static uint32_t perf_last = SDL_GetTicks();
    static int perf_frames = 0;
    static uint32_t perf_draw = 0;
    uint32_t perf_start = SDL_GetTicks();
    uint16_t* left = screen_pixels_16_left;
    uint16_t* right = screen_pixels_16_right;

    // Lookup real ARGB1555 value from rgb array for the GLdc texture upload.
    for (int y = 0; y < src_height; y++)
    {
        for (int x = 0; x < dc_left_width; x++)
            *(left++) = (uint16_t)rgb[*(pixels++)];

        for (int x = 0; x < dc_right_width; x++)
            *(right++) = (uint16_t)rgb[*(pixels++)];
    }
    perf_draw += SDL_GetTicks() - perf_start;
    perf_frames++;
    const uint32_t perf_now = SDL_GetTicks();
    if (perf_now - perf_last >= 1000)
    {
        DC_RENDER_TRACE("cannonball: drawperf fps=%d draw=%lu\n",
                        perf_frames,
                        (unsigned long)(perf_draw / perf_frames));
        perf_last = perf_now;
        perf_frames = 0;
        perf_draw = 0;
    }
#else
    uint32_t* spix = screen_pixels;

    // Lookup real RGB value from rgb array for backbuffer
    for (int i = 0; i < (src_width * src_height); i++)
        *(spix++) = rgb[*(pixels++)];
#endif
}

void Render::convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1)
{
#ifdef __DREAMCAST__
    adr >>= 1;

    rgb[adr] = 0x8000 | ((r1 & 0x1F) << 10) | ((g1 & 0x1F) << 5) | (b1 & 0x1F);

    const uint32_t r = ((r1 * shadow_multi) / 31) >> 3;
    const uint32_t g = ((g1 * shadow_multi) / 31) >> 3;
    const uint32_t b = ((b1 * shadow_multi) / 31) >> 3;
    rgb[adr + S16_PALETTE_ENTRIES] = 0x8000 | ((r & 0x1F) << 10) | ((g & 0x1F) << 5) | (b & 0x1F);
#else
    RenderBase::convert_palette(adr, r1, g1, b1);
#endif
}
