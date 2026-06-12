/***************************************************************************
    Cannonball Main Entry Point.
    
    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <cstring>
#include <iostream>

// SDL Library
#include <SDL.h>

#ifdef __DREAMCAST__
#include <arch/arch.h>
#include <dc/maple/controller.h>
#include <kos/dbglog.h>
#include <kos/thread.h>
#include <stdint.h>
#endif

// SDL Specific Code
#include "sdl2/timer.hpp"
#include "sdl2/input.hpp"

#include "video.hpp"

#include "romloader.hpp"
#include "trackloader.hpp"
#include "stdint.hpp"
#include "main.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"
#include "frontend/menu.hpp"

#include "engine/oinputs.hpp"
#include "engine/ooutputs.hpp"
#include "engine/omusic.hpp"

// Direct X Haptic Support.
// Fine to include on non-windows builds as dummy functions used.
#include "directx/ffeedback.hpp"

// ------------------------------------------------------------------------------------------------
// Initialize Shared Variables
// ------------------------------------------------------------------------------------------------
using namespace cannonball;

int    cannonball::state       = STATE_BOOT;
double cannonball::frame_ms    = 0;
int    cannonball::frame       = 0;
bool   cannonball::tick_frame  = true;
int    cannonball::fps_counter = 0;

// ------------------------------------------------------------------------------------------------
// Main Variables and Pointers
// ------------------------------------------------------------------------------------------------
Audio cannonball::audio;
Menu* menu;
bool pause_engine;

#ifdef __DREAMCAST__
#define DC_TRACE(...) dbglog(DBG_INFO, __VA_ARGS__)
#define DC_STARTUP_TRACE(...) do {} while (0)
#define DC_INPUT_TRACE(...) do {} while (0)
#define DC_PERF_INTERVAL_MS 2000
#else
#define DC_TRACE(...) do {} while (0)
#define DC_STARTUP_TRACE(...) do {} while (0)
#define DC_INPUT_TRACE(...) do {} while (0)
#endif

// ------------------------------------------------------------------------------------------------

static void quit_func(int code)
{
    audio.stop_audio();
    input.close_joy();
    forcefeedback::close();
    delete menu;
    SDL_Quit();
    exit(code);
}

static void process_events(void)
{
    SDL_Event event;

    // Grab all events from the queue.
    while(SDL_PollEvent(&event))
    {
        switch(event.type)
        {
            case SDL_KEYDOWN:
                // Handle key presses.
                if (event.key.keysym.sym == SDLK_ESCAPE)
#ifdef __DREAMCAST__
                    arch_exit();
#else
                    state = STATE_QUIT;
#endif
                else
                    input.handle_key_down(&event.key.keysym);
                break;

            case SDL_KEYUP:
                input.handle_key_up(&event.key.keysym);
                break;

            case SDL_JOYAXISMOTION:
                input.handle_joy_axis(&event.jaxis);
                break;

            case SDL_JOYBUTTONDOWN:
                input.handle_joy_down(&event.jbutton);
                break;

            case SDL_JOYBUTTONUP:
                input.handle_joy_up(&event.jbutton);
                break;

            case SDL_CONTROLLERAXISMOTION:
                input.handle_controller_axis(&event.caxis);
                break;

            case SDL_CONTROLLERBUTTONDOWN:
                input.handle_controller_down(&event.cbutton);
                break;

            case SDL_CONTROLLERBUTTONUP:
                input.handle_controller_up(&event.cbutton);
                break;

            case SDL_JOYHATMOTION:
                input.handle_joy_hat(&event.jhat);
                break;

            case SDL_JOYDEVICEADDED:
                DC_INPUT_TRACE("cannonball: SDL_JOYDEVICEADDED which=%ld\n", (long)event.jdevice.which);
                input.open_joy();
                break;

            case SDL_JOYDEVICEREMOVED:
                DC_INPUT_TRACE("cannonball: SDL_JOYDEVICEREMOVED which=%ld\n", (long)event.jdevice.which);
                input.close_joy();
                break;

            case SDL_CONTROLLERDEVICEADDED:
                DC_INPUT_TRACE("cannonball: SDL_CONTROLLERDEVICEADDED which=%ld\n", (long)event.cdevice.which);
                input.open_joy();
                break;

            case SDL_CONTROLLERDEVICEREMOVED:
                DC_INPUT_TRACE("cannonball: SDL_CONTROLLERDEVICEREMOVED which=%ld\n", (long)event.cdevice.which);
                input.close_joy();
                break;

            case SDL_QUIT:
                // Handle quit requests (like Ctrl-c).
                state = STATE_QUIT;
                break;
        }
    }
}

static void tick()
{
    frame++;
    static int last_state = -1;
    if (last_state != state)
    {
        DC_TRACE("cannonball: state %d -> %d frame=%d\n", last_state, state, frame);
        last_state = state;
    }

    // Non standard FPS: Determine whether to tick certain logic for the current frame.
    if (config.fps == 60)
        tick_frame = frame & 1;
    else if (config.fps == 120)
        tick_frame = (frame & 3) == 1;

    process_events();

    if (tick_frame)
    {
        oinputs.tick();           // Do Controls
        oinputs.do_gear();        // Digital Gear
    }
     
    switch (state)
    {
        case STATE_GAME:
        {
            if (tick_frame)
            {
                if (input.has_pressed(Input::TIMER)) outrun.freeze_timer = !outrun.freeze_timer;
                if (input.has_pressed(Input::PAUSE)) pause_engine = !pause_engine;
                if (input.has_pressed(Input::MENU))  state = STATE_INIT_MENU;
            }

            if (!pause_engine || input.has_pressed(Input::STEP))
            {
                outrun.tick(tick_frame);
                if (tick_frame) input.frame_done();
                osoundint.tick();
            }
            else
            {                
                if (tick_frame) input.frame_done();
            }
        }
        break;

        case STATE_INIT_GAME:
            if (config.engine.jap && !roms.load_japanese_roms())
            {
                DC_TRACE("cannonball: STATE_INIT_GAME japanese rom load failed\n");
                state = STATE_QUIT;
            }
            else
            {
                tick_frame = true;
                pause_engine = false;
                outrun.init();
                state = STATE_GAME;
            }
            break;

        case STATE_MENU:
            menu->tick();
            input.frame_done();
            osoundint.tick();
            break;

        case STATE_INIT_MENU:
            oinputs.init();
            outrun.outputs->init();
            menu->init();
            state = STATE_MENU;
            break;
    }

    // Map OutRun outputs to CannonBall devices (SmartyPi Interface / Controller Rumble)
    outrun.outputs->writeDigitalToConsole();
    if (tick_frame)
    {
         input.set_rumble(outrun.outputs->is_set(OOutputs::D_MOTOR), config.controls.rumble);
    }
}

static void main_loop()
{
    // FPS Counter (If Enabled)
    Timer fps_count;
    int frame = 0;
    fps_count.start();

    // General Frame Timing
    bool vsync = config.video.vsync == 1 && video.supports_vsync();
    Timer frame_time;
    int t;                              // Actual timing of tick in ms as measured by SDL (ms)
    double deltatime  = 0;              // Time we want an entire frame to take (ms)
    int deltaintegral = 0;              // Integer version of above

#ifdef __DREAMCAST__
    uint32_t perf_last = SDL_GetTicks();
    int perf_frames = 0;
    uint32_t perf_tick = 0;
    uint32_t perf_prepare = 0;
    uint32_t perf_render = 0;
    uint32_t perf_audio = 0;
    uint32_t perf_total = 0;
#endif

    while (state != STATE_QUIT)
    {
        frame_time.start();
#ifdef __DREAMCAST__
        const uint32_t perf_frame_start = SDL_GetTicks();
#endif
        // Tick Engine
#ifdef __DREAMCAST__
        uint32_t perf_start = SDL_GetTicks();
#endif
        tick();
#ifdef __DREAMCAST__
        perf_tick += SDL_GetTicks() - perf_start;
#endif

        // Draw SDL Video
#ifdef __DREAMCAST__
        perf_start = SDL_GetTicks();
#endif
        video.prepare_frame();
#ifdef __DREAMCAST__
        perf_prepare += SDL_GetTicks() - perf_start;
        perf_start = SDL_GetTicks();
#endif
        video.render_frame();
#ifdef __DREAMCAST__
        perf_render += SDL_GetTicks() - perf_start;
#endif

        // Fill SDL Audio Buffer For Callback
#ifdef __DREAMCAST__
        perf_start = SDL_GetTicks();
#endif
        audio.tick();
#ifdef __DREAMCAST__
        perf_audio += SDL_GetTicks() - perf_start;
#endif
        
        // Calculate Timings. Cap Frame Rate. Note this might be trumped by V-Sync
        if (!vsync)
        {
            deltatime += (frame_ms * audio.adjust_speed());
            deltaintegral = (int)deltatime;
            t = frame_time.get_ticks();
            
            if (t < deltatime)
                SDL_Delay((Uint32)(deltatime - t));

            deltatime -= deltaintegral;
        }

#ifdef __DREAMCAST__
        perf_total += SDL_GetTicks() - perf_frame_start;
        perf_frames++;
        const uint32_t perf_now = SDL_GetTicks();
        if (perf_now - perf_last >= DC_PERF_INTERVAL_MS)
        {
            const uint32_t perf_elapsed = perf_now - perf_last;
            const uint32_t perf_fps = (perf_frames * 1000) / perf_elapsed;
            DC_TRACE("cannonball: perf fps=%lu avg_ms total=%lu tick=%lu prep=%lu render=%lu audio=%lu state=%d target_fps=%d\n",
                     perf_fps,
                     (unsigned long)(perf_total / perf_frames),
                     (unsigned long)(perf_tick / perf_frames),
                     (unsigned long)(perf_prepare / perf_frames),
                     (unsigned long)(perf_render / perf_frames),
                     (unsigned long)(perf_audio / perf_frames),
                     state,
                     config.fps);
            perf_last = perf_now;
            perf_frames = 0;
            perf_tick = 0;
            perf_prepare = 0;
            perf_render = 0;
            perf_audio = 0;
            perf_total = 0;
        }
#endif

        if (config.video.fps_count)
        {
            frame++;
            // One second has elapsed
            if (fps_count.get_ticks() >= 1000)
            {
                fps_counter = frame;
                frame       = 0;
                fps_count.start();
            }
        }
    }

    quit_func(0);
}

// Very (very) simple command line parser.
// Returns true if everything is ok to proceed with launching th engine.
static bool parse_command_line(int argc, char* argv[])
{
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-cfgfile") == 0 && i+1 < argc)
        {
            config.set_config_file(argv[i+1]);
        }
        else if (strcmp(argv[i], "-file") == 0 && i+1 < argc)
        {
            if (!trackloader.set_layout_track(argv[i+1]))
                return false;
        }
        else if (strcmp(argv[i], "-help") == 0)
        {
            std::cout << "Command Line Options:\n\n" <<
                         "-cfgfile: Location and name of config.xml\n" <<
                         "-file   : LayOut Editor track data to load\n" << std::endl;
            return false;
        }
    }
    return true;
}

static int cannonball_main(int argc, char* argv[])
{
    DC_STARTUP_TRACE("cannonball: app thread start argc=%d\n", argc);
    // Parse command line arguments (config file location, LayOut data) 
    bool ok = parse_command_line(argc, argv);
    DC_STARTUP_TRACE("cannonball: parse_command_line -> %d\n", ok);

    if (ok)
    {
        DC_STARTUP_TRACE("cannonball: config.load begin\n");
        config.load(); // Load config.XML file
        DC_STARTUP_TRACE("cannonball: config.load done rom=%s res=%s save=%s\n",
                 config.data.rom_path.c_str(), config.data.res_path.c_str(), config.data.save_path.c_str());
        DC_STARTUP_TRACE("cannonball: roms.load_revb_roms begin\n");
        ok = roms.load_revb_roms(config.sound.fix_samples);
        DC_STARTUP_TRACE("cannonball: roms.load_revb_roms -> %d\n", ok);
    }
    if (!ok)
    {
        DC_TRACE("cannonball: startup failed before SDL init\n");
        return 1;
    }
#ifdef __DREAMCAST__
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_TEXTURED_STRIDED_VIDEO");
    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    // SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
#endif
    // Load gamecontrollerdb.txt mappings
    DC_STARTUP_TRACE("cannonball: SDL mappings begin\n");
    if (SDL_GameControllerAddMappingsFromFile((config.data.res_path + "gamecontrollerdb.txt").c_str()) == -1)
        std::cout << "Unable to load controller mapping" << std::endl;
    DC_STARTUP_TRACE("cannonball: SDL mappings done\n");
    // Initialize timer and video systems
    DC_STARTUP_TRACE("cannonball: SDL_Init begin\n");
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) == -1)
    {
        std::cerr << "SDL Initialization Failed: " << SDL_GetError() << std::endl;
        DC_TRACE("cannonball: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    DC_STARTUP_TRACE("cannonball: SDL_Init done\n");

    // Load patched widescreen tilemaps
    DC_STARTUP_TRACE("cannonball: load_widescreen_map begin\n");
    if (!omusic.load_widescreen_map(config.data.res_path))
        std::cout << "Unable to load widescreen tilemaps" << std::endl;
    DC_STARTUP_TRACE("cannonball: load_widescreen_map done\n");

    // Initialize SDL Video
    DC_STARTUP_TRACE("cannonball: video.init begin\n");
    config.set_fps(config.video.fps);
    if (!video.init(&roms, &config.video))
    {
        DC_TRACE("cannonball: video.init failed\n");
        quit_func(1);
    }
    DC_STARTUP_TRACE("cannonball: video.init done\n");

    // Initialize SDL Audio
    DC_STARTUP_TRACE("cannonball: audio.init begin\n");
    audio.init();
    DC_STARTUP_TRACE("cannonball: audio.init done\n");

    state = config.menu.enabled ? STATE_INIT_MENU : STATE_INIT_GAME;

    // Initalize SDL Controls
    DC_STARTUP_TRACE("cannonball: input.init begin\n");
    input.init(config.controls.pad_id,
               config.controls.keyconfig, config.controls.padconfig, 
               config.controls.analog,    config.controls.axis, config.controls.invert, config.controls.asettings);
    DC_STARTUP_TRACE("cannonball: input.init done\n");

    if (config.controls.haptic) 
        config.controls.haptic = forcefeedback::init(config.controls.max_force, config.controls.min_force, config.controls.force_duration);
        
    // Populate menus
    DC_STARTUP_TRACE("cannonball: menu populate begin\n");
    menu = new Menu();
    menu->populate();
    DC_STARTUP_TRACE("cannonball: main_loop begin state=%d\n", state);
    main_loop();  // Loop until we quit the app

    // Never Reached
    return 0;
}

#ifdef __DREAMCAST__
struct dreamcast_main_args_t
{
    int argc;
    char** argv;
};

static void* dreamcast_main_thread(void* param)
{
    dreamcast_main_args_t* args = static_cast<dreamcast_main_args_t*>(param);
    return reinterpret_cast<void*>(static_cast<intptr_t>(cannonball_main(args->argc, args->argv)));
}

int main(int argc, char* argv[])
{
    cont_btn_callback(0,
        CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y,
        (cont_btn_callback_t)arch_exit);

    dreamcast_main_args_t args = { argc, argv };
    kthread_attr_t attr = {};
    void* thread_result = NULL;

    attr.stack_size = 2 * 1024 * 1024;
    attr.prio = PRIO_DEFAULT;
    attr.label = "cannonball";

    kthread_t* thread = thd_create_ex(&attr, dreamcast_main_thread, &args);
    if (!thread)
    {
        dbglog(DBG_ERROR, "cannonball: failed to create app thread\n");
        return 1;
    }

    thd_join(thread, &thread_result);
    return static_cast<int>(reinterpret_cast<intptr_t>(thread_result));
}
#else
int main(int argc, char* argv[])
{
    return cannonball_main(argc, argv);
}
#endif
