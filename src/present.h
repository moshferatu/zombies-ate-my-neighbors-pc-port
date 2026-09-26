// The SDL half of putting the framebuffer on the screen.
//
// `src/scale.h` decides *where* the picture goes and *how* it should get there;
// this carries the decision out. It is a header of static inline functions
// rather than part of `main_sdl.c` for one reason: `tools/test_present.c` drives
// the same code against a software renderer and reads the pixels back, so the
// claim "sharp keeps blocks sharp" is checked rather than asserted. A copy of
// these eight SDL calls living privately in the frontend would be eight calls
// nothing tested.
//
// `present_draw` is deliberately separate from getting a frame *into* the
// texture: the frontend fills it from the core with `snes_setPixels` under a
// lock, and the test fills it with a pattern. Neither has any business knowing
// how the other does it.

#ifndef ZAMN_PRESENT_H
#define ZAMN_PRESENT_H

#include <string.h>

#include <SDL.h>

#include "scale.h"

typedef struct {
  SDL_Renderer* ren;
  SDL_Texture* frame;  // the source, at the full texture size
  SDL_Texture* stage;  // `sharp`'s whole-multiple intermediate, or NULL
  int stage_x, stage_y;  // the multiples `stage` was built at; 0 if none
  // The live part of `frame`. The core writes 512x480 but blanks sixteen rows
  // top and bottom, so this is the 512x448 that is actually picture — and it is
  // what every size below is measured against.
  SDL_Rect src;
  bool can_target;     // can this renderer draw into a texture at all
  ScaleMode mode;
  // Square pixels rather than 4:3. Nothing in the game sets it: it is for
  // `tools/test_present.c`, which needs a whole multiple to be one on both
  // axes at once.
  bool square;
} Present;

// The shape `scale_plan` is asked for.
static inline void present_ratio(const Present* p, int* aw, int* ah) {
  if (p->square) { *aw = p->src.w; *ah = p->src.h; }
  else aspect_ratio(p->src.w, p->src.h, aw, ah);
}

// The offscreen target `sharp` needs, at `nx` by `ny` times the source. Kept
// between frames and rebuilt only when the multiples change — which happens
// when the window is resized across a whole multiple, and not once a frame.
static inline bool present_stage(Present* p, int nx, int ny) {
  if (p->stage && p->stage_x == nx && p->stage_y == ny) return true;
  if (p->stage) SDL_DestroyTexture(p->stage);
  p->stage = SDL_CreateTexture(p->ren, SDL_PIXELFORMAT_RGBX8888,
                               SDL_TEXTUREACCESS_TARGET, p->src.w * nx,
                               p->src.h * ny);
  p->stage_x = p->stage ? nx : 0;
  p->stage_y = p->stage ? ny : 0;
  // Linear on the way *out* of the stage is the entire point of building one.
  // The way in is nearest, and that is the frame texture's own mode.
  if (p->stage) SDL_SetTextureScaleMode(p->stage, SDL_ScaleModeLinear);
  return p->stage != NULL;
}

static inline bool present_init(Present* p, SDL_Renderer* ren, int tex_w,
                                int tex_h, SDL_Rect src, ScaleMode mode) {
  memset(p, 0, sizeof *p);
  p->ren = ren;
  p->mode = mode;
  p->src = src;

  SDL_RendererInfo info;
  p->can_target = SDL_GetRendererInfo(ren, &info) == 0 &&
                  (info.flags & SDL_RENDERER_TARGETTEXTURE) != 0;

  p->frame = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBX8888,
                               SDL_TEXTUREACCESS_STREAMING, tex_w, tex_h);
  if (!p->frame) return false;
  // Set rather than left to the default. Nearest *is* SDL's default, but it is
  // also overridable from outside the process by `SDL_RENDER_SCALE_QUALITY`,
  // and a frontend whose sharpness depends on an environment variable nobody
  // set is one that will one day be blurry for no reason anyone can find.
  SDL_SetTextureScaleMode(p->frame, SDL_ScaleModeNearest);
  return true;
}

static inline void present_free(Present* p) {
  if (p->stage) SDL_DestroyTexture(p->stage);
  if (p->frame) SDL_DestroyTexture(p->frame);
  p->stage = p->frame = NULL;
}

// Whatever is in `p->frame`, scaled onto the current target. Does not present:
// the caller decides whether this went to a screen or to a texture it is about
// to read back.
static inline void present_draw(Present* p) {
  // Output size, not window size: with `SDL_WINDOW_ALLOW_HIGHDPI` those differ
  // on a scaled display, and it is the pixels that decide whether a factor is a
  // whole number.
  int ow = 0, oh = 0;
  SDL_GetRendererOutputSize(p->ren, &ow, &oh);
  int aw = 0, ah = 0;
  present_ratio(p, &aw, &ah);
  ScalePlan plan =
      scale_plan(p->mode, p->src.w, p->src.h, aw, ah, ow, oh, p->can_target);
  if (plan.dst.w <= 0 || plan.dst.h <= 0) return;  // minimised, or not up yet

  const SDL_Rect dst = {plan.dst.x, plan.dst.y, plan.dst.w, plan.dst.h};

  // The letterbox. Cleared before anything is drawn into it, and before the
  // render target moves, because this is the screen's border and not the
  // stage's.
  SDL_SetRenderDrawColor(p->ren, 0, 0, 0, 255);
  SDL_RenderClear(p->ren);

  if (plan.stage_x > 0 && plan.stage_y > 0 &&
      present_stage(p, plan.stage_x, plan.stage_y)) {
    SDL_Texture* was = SDL_GetRenderTarget(p->ren);
    if (SDL_SetRenderTarget(p->ren, p->stage) == 0) {
      // Up to the whole multiple with nearest — an exact blow-up, every block
      // the same size — and back down to the window with one bilinear step.
      // Only `p->src` goes up: the blank rows the core leaves top and bottom
      // are not picture and must not be given any of the screen.
      SDL_SetTextureScaleMode(p->frame, SDL_ScaleModeNearest);
      SDL_RenderCopy(p->ren, p->frame, &p->src, NULL);
      SDL_SetRenderTarget(p->ren, was);
      SDL_RenderCopy(p->ren, p->stage, NULL, &dst);
      return;
    }
    // Restore whatever we were drawing to and fall through. A failure here is a
    // lost device or exhausted video memory, and a plain copy still shows the
    // frame, so nothing is said about it.
    SDL_SetRenderTarget(p->ren, was);
  }

  SDL_SetTextureScaleMode(
      p->frame, plan.linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
  SDL_RenderCopy(p->ren, p->frame, &p->src, &dst);
}

#endif
