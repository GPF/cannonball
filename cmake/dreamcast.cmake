# -----------------------------------------------------------------------------
# CannonBall Dreamcast / KallistiOS Setup
# -----------------------------------------------------------------------------

# SDL2 installed by KOS ports.
set(sdl2_dir /opt/toolchains/dc/kos/addons/lib/dreamcast/cmake/SDL2)

# The Dreamcast build uses lightweight local parsers/helpers instead of Boost.
set(USE_BOOST 0)

# The Dreamcast SDL2 port provides the video backend; do not enable CannonBall's
# standalone OpenGL renderer.
add_definitions(-DDREAMCAST)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

option(DREAMCAST_SKIP_SPRITE_SHADOWS "Skip generated sprite shadow entries on Dreamcast" ON)
if(DREAMCAST_SKIP_SPRITE_SHADOWS)
    add_definitions(-DDREAMCAST_SKIP_SPRITE_SHADOWS)
endif()

option(DREAMCAST_FAST_SPRITES "Enable experimental Dreamcast sprite rasterizer fast paths" OFF)
if(DREAMCAST_FAST_SPRITES)
    add_definitions(-DDREAMCAST_FAST_SPRITES)
endif()

option(DREAMCAST_FAST_SPRITES_VALIDATE "Compare experimental Dreamcast sprite fast paths against the original rasterizer" OFF)
if(DREAMCAST_FAST_SPRITES_VALIDATE)
    add_definitions(-DDREAMCAST_FAST_SPRITES -DDREAMCAST_FAST_SPRITES_VALIDATE)
endif()

# Platform Specific Libraries
set(platform_link_libs
)

# Platform Specific Link Directories
set(platform_link_dirs
)
