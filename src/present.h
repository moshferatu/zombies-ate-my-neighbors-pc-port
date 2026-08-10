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
  SDL_Texture* frame;  // the source, at its native size
  SDL_Texture* stage;  // `sharp`'s whole-multiple intermediate, or NULL
  int stage_scale;     // the multiple `stage` was built at; 0 if there is none
  int sw, sh;          // the framebuffer's size
  bool can_target;     // can this renderer draw into a texture at all
  ScaleMode mode;
} Present;

// The offscreen target `sharp` needs, at `n` times the framebuffer. Kept between
// frames and rebuilt only when `n` changes — which happens when the window is
// resized across a whole multiple, and not once a frame.
static inline bool present_stage(Present* p, int n) {
  if (p->stage && p->stage_scale == n) return true;
  if (p->stage) SDL_DestroyTexture(p->stage);
  p->stage = SDL_CreateTexture(p->ren, SDL_PIXELFORMAT_RGBX8888,
                               SDL_TEXTUREACCESS_TARGET, p->sw * n, p->sh * n);
  p->stage_scale = p->stage ? n : 0;
  // Linear on the way *out* of the stage is the entire point of building one.
  // The way in is nearest, and that is the frame texture's own mode.
  if (p->stage) SDL_SetTextureScaleMode(p->stage, SDL_ScaleModeLinear);
  return p->stage != NULL;
}

static inline bool present_init(Present* p, SDL_Renderer* ren, int sw, int sh,
                                ScaleMode mode) {
  memset(p, 0, sizeof *p);
  p->ren = ren;
  p->mode = mode;
  p->sw = sw;
  p->sh = sh;

  SDL_RendererInfo info;
  p->can_target = SDL_GetRendererInfo(ren, &info) == 0 &&
                  (info.flags & SDL_RENDERER_TARGETTEXTURE) != 0;

  p->frame = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBX8888,
                               SDL_TEXTUREACCESS_STREAMING, sw, sh);
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
  ScalePlan plan = scale_plan(p->mode, p->sw, p->sh, ow, oh, p->can_target);
  if (plan.dst.w <= 0 || plan.dst.h <= 0) return;  // minimised, or not up yet

  const SDL_Rect dst = {plan.dst.x, plan.dst.y, plan.dst.w, plan.dst.h};

  // The letterbox. Cleared before anything is drawn into it, and before the
  // render target moves, because this is the screen's border and not the
  // stage's.
  SDL_SetRenderDrawColor(p->ren, 0, 0, 0, 255);
  SDL_RenderClear(p->ren);

  if (plan.stage > 0 && present_stage(p, plan.stage)) {
    SDL_Texture* was = SDL_GetRenderTarget(p->ren);
    if (SDL_SetRenderTarget(p->ren, p->stage) == 0) {
      // Up to the whole multiple with nearest — an exact blow-up, every block
      // the same size — and back down to the window with one bilinear step.
      SDL_SetTextureScaleMode(p->frame, SDL_ScaleModeNearest);
      SDL_RenderCopy(p->ren, p->frame, NULL, NULL);
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
  SDL_RenderCopy(p->ren, p->frame, NULL, &dst);
}

#endif
