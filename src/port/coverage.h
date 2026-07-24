// Which of the port's branches has any input actually taken?
//
// `zamn_cosim verify` answers one question very well: does the port compute
// what the ROM computes, on the calls this movie made. It cannot answer the
// question underneath it — *did this movie ever make a call that reaches this
// line*. A branch no input takes is transcribed from the listing and nothing
// more, and the diff will agree with the ROM about it forever, because neither
// one runs it.
//
// `docs/cosim.md` records four such branches. Every one of them was found by
// hand: change the port on purpose, run `verify`, and notice that it still
// passes. That method works and it found real gaps, but it is manual, it is
// destructive, and nobody is going to remember to redo all of it each time a
// movie is added — which is exactly when the answer changes.
//
// So the port marks its decision points and the harness counts them. The rule
// is the one the shims' flag masks already follow, one step further out: an
// unclaimed output is an unchecked output, and **an untaken branch is an
// unverified branch**. Both should be visible rather than assumed.
//
// A mark is not an assertion and never changes what the port computes. It says
// "reaching here is a distinct thing the game can do, and if no input ever does
// it, the code below is unproven". Sites are deliberately sparse: one per
// decision the *diff* would have to run to check, not one per `if`.
//
// This is an instrument, not game logic. With `PORT_COVERAGE` undefined every
// mark compiles to nothing at all — no counters, no storage, no code — which is
// how the finished game builds. The harness's CMake target defines it.
//
// Port code: libc only. This header does not even need that.

#ifndef PORT_COVERAGE_H
#define PORT_COVERAGE_H

// The site table. Each row is: identifier, the routine it lives in, and what
// it means for an input to have taken it — phrased so that the report reads as
// a list of things the game can do, because that is what a missing one is.
//
// Keeping them in one table rather than registering them at their use sites is
// what makes a *never-taken* site reportable at all: a site that registers
// itself when first hit is invisible precisely when it matters.
#define PORT_COVER_SITES(X)                                                    \
  /* $80:BA51 and its three flipped twins — the OAM composition. */            \
  X(emit_flip_x,      "sprite_emit",        "a piece mirrored horizontally")    \
  X(emit_flip_y,      "sprite_emit",        "a piece mirrored vertically")      \
  X(emit_drop_y,      "sprite_emit",        "a piece dropped off the top or bottom") \
  X(emit_drop_x,      "sprite_emit",        "a piece dropped off the right")    \
  X(emit_wrap_x,      "sprite_emit",        "a piece hanging off the left edge") \
  X(emit_oam_full,    "sprite_emit",        "OAM filled up mid-metasprite")     \
                                                                                \
  /* $80:B9D6 sprite_frame_tile — the 128-slot VRAM frame cache. */            \
  X(cache_hit,        "sprite_frame_tile",  "a frame already resident in VRAM") \
  X(cache_miss,       "sprite_frame_tile",  "a frame uploaded into a slot")     \
  X(cache_scan,       "sprite_frame_tile",  "the eviction scan skipped a slot drawn this frame") \
  X(cache_evict,      "sprite_frame_tile",  "an eviction threw out a frame that was resident") \
                                                                                \
  /* $80:8398 thread_tick_waits, and the two vblank-queue adders. */           \
  X(wait_tick,        "thread_tick_waits",  "a sleeping thread's counter stepped down") \
  X(wait_expired,     "thread_tick_waits",  "a thread already at its wake tick")\
  X(wait_empty,       "thread_tick_waits",  "an unused scheduler slot")         \
  X(queue_full,       "vbl_queue_add",      "a vblank queue rejected a job (carry set)") \
                                                                                \
  /* $80:BC7F actor_depth_sort. */                                             \
  X(sort_key_first,   "actor_depth_sort",   "ACTOR_SORT_FIRST decided the order") \
  X(sort_key_y,       "actor_depth_sort",   "the Y key decided the order")      \
  X(sort_swap_head,   "actor_depth_sort",   "the list head itself moved")       \
  X(sort_swap_mid,    "actor_depth_sort",   "two records swapped mid-list")     \
                                                                                \
  /* $80:BCE2 actor_cull. */                                                   \
  X(cull_undrawn,     "actor_cull",         "a record with ACTOR_DRAW clear")   \
  X(cull_screen,      "actor_cull",         "a screen-space record, exempt from the camera window") \
  X(cull_offscreen,   "actor_cull",         "a record culled by the camera window") \
                                                                                \
  /* $80:BEC9 actor_overlap_pass. */                                           \
  X(overlap_no_id,    "actor_overlap_pass", "a visible record with no collision id") \
  X(overlap_same_id,  "actor_overlap_pass", "a pair sharing a collision id, skipped") \
  X(overlap_near_x,   "actor_overlap_pass", "a pair within 8 px on X")          \
  X(overlap_hit,      "actor_overlap_pass", "a pair inside the box on both axes — a real collision") \
                                                                                \
  /* $80:BE8F actor_collide_notify — the dispatch a hit ends with. */           \
  X(collide_none,     "actor_collide_notify", "a collision neither actor had registered a handler for") \
  X(collide_handler,  "actor_collide_notify", "a collision that entered at least one actor's handler") \
  X(collide_unported, "actor_collide_notify", "a collision whose handler the port does not have — declined") \
                                                                                \
  /* $80:8480 thread_call_handler — the door into actor behaviour. */          \
  X(handler_none,     "thread_call_handler", "a dispatch to a slot with no handler registered") \
  X(handler_ported,   "thread_call_handler", "a dispatch the port entered itself") \
  X(handler_unported, "thread_call_handler", "a dispatch to a handler address the port does not have") \
  X(handler_park,     "thread_call_handler", "a handler returned carry set and its thread was parked") \
                                                                                \
  /* $80:F7F7 player_collide, and the hit path at $80:F950. */                  \
  X(player_ignore,    "player_collide",      "the player told about an id of its own side, which it ignores") \
  X(player_no_effect, "player_collide",      "an id whose jump-table entry is a bare RTS") \
  X(player_hurt_entry,"player_collide",      "an id that dispatched to $80:F950, the hit path") \
  X(player_unported,  "player_collide",      "an id whose jump-table entry is not ported — declined") \
  X(hurt_state_immune,"player_collide",      "the player's state already ignores collisions") \
  X(hurt_weapon_immune,"player_collide",     "the one weapon that turns a hit aside, with $1E set") \
  X(hurt_iframes,     "player_collide",      "a hit inside the recovery window, ignored") \
  X(hurt_taken,       "player_collide",      "a hit the player actually took")   \
                                                                                \
  /* $81:8888 enemy_collide. */                                                 \
  X(enemy_ignore,     "enemy_collide",       "an enemy told about an id of the other side, which it ignores") \
  X(enemy_act,        "enemy_collide",       "an enemy taking damage — the branch nobody has ported") \
                                                                                \
  /* $80:BD1F sprite_build_oam. */                                             \
  X(draw_priority_top,"sprite_build_oam",   "ACTOR_PRIORITY_TOP raised a record's priority") \
  X(draw_attr_set,    "sprite_build_oam",   "ACTOR_ATTR_SET overrode a record's palette") \
  X(draw_screen,      "sprite_build_oam",   "a screen-space record drawn without the camera") \
  X(draw_no_meta,     "sprite_build_oam",   "a drawable record whose metasprite pointer is not in ROM") \
  X(draw_bad_bank,    "sprite_build_oam",   "a drawable record whose metasprite bank is not $8F/$90") \
  X(draw_empty_meta,  "sprite_build_oam",   "a metasprite with no pieces")      \
  X(draw_oam_full,    "sprite_build_oam",   "the pass stopped without writing the OAM terminator") \
                                                                                \
  /* $80:891A fade_in. */                                                      \
  X(fade_in_step,     "fade_in",            "a brightness step short of full")  \
  X(fade_in_done,     "fade_in",            "the ramp reached full brightness and returned")

typedef enum {
#define PORT_COVER_ENUM_(id, routine, what) PORT_COVER_##id,
  PORT_COVER_SITES(PORT_COVER_ENUM_)
#undef PORT_COVER_ENUM_
  PORT_COVER_COUNT
} PortCoverId;

#ifdef PORT_COVERAGE

// One counter per site, in enum order. Exposed directly because the whole point
// is that a harness can read it, snapshot it and put it back.
extern unsigned long port_cover_hits[PORT_COVER_COUNT];

#define PORT_COVER(id) ((void)(port_cover_hits[PORT_COVER_##id]++))

// A two-way decision whose branches are not otherwise separate statements. Both
// sides get a site, because "this never went the other way" is the finding.
#define PORT_COVER_IF(cond, yes, no) \
  ((void)((cond) ? port_cover_hits[PORT_COVER_##yes]++ \
                 : port_cover_hits[PORT_COVER_##no]++))

// The table's three columns, for whoever prints the report.
const char* port_cover_name(int id);     // "emit_flip_y"
const char* port_cover_routine(int id);  // "sprite_emit"
const char* port_cover_what(int id);     // "a piece mirrored vertically"

void port_cover_reset(void);

// Snapshot and restore, so a caller can run the port speculatively without the
// dry run showing up as coverage. `buf` holds `PORT_COVER_COUNT` counters.
void port_cover_save(unsigned long* buf);
void port_cover_restore(const unsigned long* buf);

#else

#define PORT_COVER(id) ((void)0)
#define PORT_COVER_IF(cond, yes, no) ((void)0)

#endif  // PORT_COVERAGE

#endif
