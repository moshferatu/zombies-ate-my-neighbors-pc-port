// The renderer's half of `src/layers.h`: drawing a picture as layers.
//
// Split from `src/present.h` so that `tools/test_present.c`, which drives
// that header against a software renderer, need not know a PPU exists.

#ifndef ZAMN_PRESENT_LAYERS_H
#define ZAMN_PRESENT_LAYERS_H

#include "layers.h"
#include "present.h"

// ---------------------------------------------------------------------------
// Drawing a picture as layers -- the renderer's half of `src/layers.h`.
// ---------------------------------------------------------------------------
//
// The draw list puts planes and sprites on a target several times the
// console's size, so that a quarter of a game pixel is a whole pixel of the
// target. The target then goes to the screen the way the frame texture does:
// shrunk with one bilinear step, or copied exactly when it fits.
//
// How big the target is decides how finely things can move: `sx` game pixels
// across is the smallest step across. It is chosen from the same plan `sharp`
// uses, as the whole multiple of the *console* that covers the picture on
// screen -- six at 1080p in 4:3, eight at 1440p -- and never less than two,
// which is the frame texture's own doubling and what `linear` asks for.
//
// The sub screen, where the console adds it, is added only where the layer
// it is added to put a pixel. The renderer has no such blend, so it is done
// in two steps on a scratch target: the layer is drawn there alone, the sub
// screen is added with a blend that multiplies by the destination's alpha --
// zero where the layer left nothing -- and the scratch is then drawn over the
// picture with ordinary alpha. `layers_render` does the same arithmetic in
// software; the test that the two agree is that the list drawn unmoved is
// the PPU's frame.

typedef struct {
  SDL_Texture* plane[LAYERS_PLANES];
  SDL_Texture* atlas;
  SDL_Texture* target;
  SDL_Texture* scratch;
  int target_w, target_h;  // the two above, in pixels; 0 until first drawn
  int sx, sy;              // ...and the scale they were built at
  SDL_BlendMode subtract;  // dst - src
  SDL_BlendMode add_masked, sub_masked;  // src * dstA +/- dst, alpha kept
  bool blends_ok;          // could the renderer take the custom blends at all
} PresentLayers;

static inline bool present_layers_init(PresentLayers* L, SDL_Renderer* ren) {
  memset(L, 0, sizeof *L);
  for (int i = 0; i < LAYERS_PLANES; i++) {
    L->plane[i] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING, LAYERS_PLANE_W,
                                    LAYERS_PLANE_H);
    if (!L->plane[i]) return false;
    SDL_SetTextureScaleMode(L->plane[i], SDL_ScaleModeNearest);
  }
  L->atlas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                               SDL_TEXTUREACCESS_STREAMING, LAYERS_ATLAS_W,
                               LAYERS_ATLAS_H);
  if (!L->atlas) return false;
  SDL_SetTextureScaleMode(L->atlas, SDL_ScaleModeNearest);
  L->subtract = SDL_ComposeCustomBlendMode(
      SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
  L->add_masked = SDL_ComposeCustomBlendMode(
      SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
  L->sub_masked = SDL_ComposeCustomBlendMode(
      SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
  // Whether this renderer will take them. Asked once, here, so that a frame
  // needing one is shown as the PPU drew it rather than drawn wrong.
  L->blends_ok = SDL_SetTextureBlendMode(L->atlas, L->add_masked) == 0 &&
                 SDL_SetTextureBlendMode(L->atlas, L->sub_masked) == 0 &&
                 SDL_SetTextureBlendMode(L->atlas, L->subtract) == 0;
  SDL_SetTextureBlendMode(L->atlas, SDL_BLENDMODE_BLEND);
  return true;
}

static inline void present_layers_free(PresentLayers* L) {
  for (int i = 0; i < LAYERS_PLANES; i++)
    if (L->plane[i]) SDL_DestroyTexture(L->plane[i]);
  if (L->atlas) SDL_DestroyTexture(L->atlas);
  if (L->target) SDL_DestroyTexture(L->target);
  if (L->scratch) SDL_DestroyTexture(L->scratch);
  memset(L, 0, sizeof *L);
}

// This tick's planes and sprites, into the textures. Once per tick; the
// pictures between ticks only move them.
static inline void present_layers_upload(PresentLayers* L, const LayersFrame* f) {
  const SDL_Rect planeRect = {0, 0, f->width + 2 * LAYERS_MARGIN, LAYERS_PLANE_H};
  for (int i = 0; i < LAYERS_PLANES; i++) {
    if (!f->planeUsed[i]) continue;
    SDL_UpdateTexture(L->plane[i], &planeRect, &f->plane[i][0][0][0], LAYERS_PLANE_W * 4);
  }
  const SDL_Rect atlasRect = {0, 0, LAYERS_ATLAS_COLS * f->cell, LAYERS_ATLAS_ROWS * f->cell};
  SDL_UpdateTexture(L->atlas, &atlasRect, &f->atlas[0][0][0], LAYERS_ATLAS_W * 4);
}

// The scale to build a list at, for the picture `plan` puts on screen: see
// the note at the top of this section. `*exact` says whether the target then
// fits the screen without resampling.
static inline void present_layers_scale(const ScalePlan* plan, ScaleMode mode,
                                        int game_w, int* sx, int* sy, bool* exact) {
  int x = (plan->dst.w + game_w - 1) / game_w;
  int y = (plan->dst.h + LAYERS_LINES - 1) / LAYERS_LINES;
  if (mode == SCALE_LINEAR) x = y = 2;
  if (x < 2) x = 2;
  if (y < 2) y = 2;
  if (x > 2 * SCALE_MAX_STAGE) x = 2 * SCALE_MAX_STAGE;
  if (y > 2 * SCALE_MAX_STAGE) y = 2 * SCALE_MAX_STAGE;
  *sx = x;
  *sy = y;
  *exact = plan->dst.w == game_w * x && plan->dst.h == LAYERS_LINES * y;
}

static inline bool present_layers_targets(PresentLayers* L, SDL_Renderer* ren,
                                          int w, int h) {
  if (L->target && L->target_w == w && L->target_h == h) return true;
  if (L->target) SDL_DestroyTexture(L->target);
  if (L->scratch) SDL_DestroyTexture(L->scratch);
  L->target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
  L->scratch = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
  L->target_w = L->target ? w : 0;
  L->target_h = L->target ? h : 0;
  if (L->target) SDL_SetTextureBlendMode(L->target, SDL_BLENDMODE_NONE);
  if (L->scratch) SDL_SetTextureBlendMode(L->scratch, SDL_BLENDMODE_BLEND);
  return L->target && L->scratch;
}

static inline SDL_Texture* present_layers_texture(PresentLayers* L, const LayersOp* o) {
  return o->kind == LAYERS_OP_PLANE ? L->plane[o->plane] : L->atlas;
}

static inline void present_layers_op(PresentLayers* L, SDL_Renderer* ren,
                                     const LayersOp* o, SDL_BlendMode blend) {
  SDL_Texture* t = present_layers_texture(L, o);
  const SDL_Rect src = {o->sx, o->sy, o->sw, o->sh};
  const SDL_Rect dst = {o->dx, o->dy, o->dw, o->dh};
  if (SDL_SetTextureBlendMode(t, blend) != 0) {
    static bool said = false;
    if (!said) { printf("blend mode %#x refused: %s\n", (unsigned)blend, SDL_GetError()); fflush(stdout); said = true; }
  }
  SDL_RenderCopy(ren, t, &src, &dst);
}

// Where the picture goes and at what scale a list for it should be built:
// the plan for the window as it is now. False if there is nowhere to draw --
// a minimised window, or a renderer that cannot draw into a texture.
static inline bool present_layers_plan(Present* p, int game_w, ScalePlan* plan,
                                       int* sx, int* sy, bool* exact) {
  if (!p->can_target) return false;
  int ow = 0, oh = 0;
  SDL_GetRendererOutputSize(p->ren, &ow, &oh);
  int aw = 0, ah = 0;
  aspect_ratio(p->aspect, p->src.w, p->src.h, &aw, &ah);
  *plan = scale_plan(p->mode, p->src.w, p->src.h, aw, ah, ow, oh, p->can_target);
  if (plan->dst.w <= 0 || plan->dst.h <= 0) return false;
  present_layers_scale(plan, p->mode, game_w, sx, sy, exact);
  return true;
}

// Draw the list onto the target and the target onto the screen, as
// `present_layers_plan` laid it out. `false` if the list needs a blend this
// renderer has not got, or a target it could not make, in which case nothing
// has been drawn and the caller shows the PPU's picture instead. Does not
// present.
static inline bool present_layers_draw(Present* p, PresentLayers* L,
                                       const ScalePlan* plan, int sx, int sy,
                                       bool exact, const LayersOp* ops, int n,
                                       int game_w) {
  if (!present_layers_targets(L, p->ren, game_w * sx, LAYERS_LINES * sy))
    return false;
  for (int i = 0; i < n; i++)
    if (ops[i].blend != LAYERS_BLEND_COPY && !L->blends_ok) return false;

  SDL_Texture* was = SDL_GetRenderTarget(p->ren);
  if (SDL_SetRenderTarget(p->ren, L->target) != 0) return false;
  SDL_SetRenderDrawBlendMode(p->ren, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(p->ren, 0, 0, 0, 255);
  SDL_RenderClear(p->ren);
  for (int i = 0; i < n; i++) {
    const LayersOp* o = &ops[i];
    if (o->blend == LAYERS_BLEND_COPY) {
      present_layers_op(L, p->ren, o, SDL_BLENDMODE_BLEND);
    } else if (o->mask < 0) {
      present_layers_op(L, p->ren, o, o->blend == LAYERS_BLEND_ADD ? SDL_BLENDMODE_ADD : L->subtract);
    } else {
      // The masked add: the layer alone on the scratch, the sub screen added
      // where the scratch has alpha, the scratch over the picture. Every op
      // confined to the same layer goes onto the same scratch before it is
      // copied back, so that a sub screen in two planes adds both.
      SDL_SetRenderTarget(p->ren, L->scratch);
      SDL_SetRenderDrawColor(p->ren, 0, 0, 0, 0);
      SDL_RenderClear(p->ren);
      present_layers_op(L, p->ren, &ops[o->mask], SDL_BLENDMODE_NONE);
      for (; i < n && ops[i].mask == o->mask; i++)
        present_layers_op(L, p->ren, &ops[i],
                          ops[i].blend == LAYERS_BLEND_ADD ? L->add_masked : L->sub_masked);
      i--;
      SDL_SetRenderTarget(p->ren, L->target);
      SDL_RenderCopy(p->ren, L->scratch, NULL, NULL);
    }
  }
  SDL_SetRenderTarget(p->ren, was);

  const SDL_Rect dst = {plan->dst.x, plan->dst.y, plan->dst.w, plan->dst.h};
  SDL_SetRenderDrawColor(p->ren, 0, 0, 0, 255);
  SDL_RenderClear(p->ren);
  SDL_SetTextureScaleMode(L->target, exact ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
  SDL_RenderCopy(p->ren, L->target, NULL, &dst);
  return true;
}

#endif
