# Dreamcast Optimization Notes

## Commit Message Draft

```text
Add Dreamcast SDL2/GLdc port fixes and sprite performance diagnostics

- Use the SDL2 OpenGL renderer path for Dreamcast/GLdc
- Add Dreamcast config fallback and startup/runtime logging
- Keep Dreamcast app logic on a larger KOS thread stack
- Add Dreamcast controller exit callback and joystick/controller event logging
- Use split POT SDL textures for the 320x224 framebuffer
- Add Dreamcast video/render/audio/sprite performance logging
- Add Dreamcast-only sprite renderer branch splits and test options
- Add optional Dreamcast sprite shadow skip path for profiling
- Build Dreamcast with -O3
```

## Resume Prompt

```text
We are optimizing CannonBall on Dreamcast using KOS + SDL2 + GLdc. SDL2/GLdc texture upload is now fast, around 1-2 ms, and render/present is not the bottleneck. The remaining slowdown is in CannonBall software sprite rasterization in src/main/hwvideo/hwsprites.cpp, specifically the general zoomed sprite loop in hwsprites::render().

Known profiling:
- Heavy sections are ~19-20 fps.
- videoperf sprites can be ~20-24 ms.
- spriteperf rows can be ~3,400 rows/frame.
- rows_1x is low in heavy scenes, often only ~100-230 rows, so the hzoom == 0x200 fast path is not useful.
- Disabling shadow pixels improves only modestly, not enough.
- Local sprite address cursor did not materially improve performance.
- SDL_UpdateTexture is not the problem anymore.

Next steps:
1. Revisit hwsprites.cpp and optimize the general zoom loop, not SDL2.
2. Profile common hzoom values in heavy scenes.
3. Consider lookup/table or span-based rendering for common hzoom values to avoid per-output-pixel xacc loops.
4. Keep Dreamcast-specific risky optimizations guarded with DREAMCAST or a CMake option.
5. Consider removing temporary profiling/test options once a stable optimization is chosen.
```
