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
  X(queue_full,       "vbl_queue_add",      "a vblank queue rejected a job (carry set)")   X(spawn_took,       "thread_spawn",       "a free scheduler slot turned into a running thread")   X(spawn_full,       "thread_spawn",       "all 24 slots live, so the spawn failed") \
                                                                                \
  /* $80:8353 the scheduler, and the two vblank dispatchers. */                 \
  X(sched_switch,     "thread_yield",      "a ready thread found, and its stack switched to") \
  X(sched_idle,       "thread_yield",      "no thread ready: the scan ran off the end to the WAI") \
  X(sched_thread_ended, "thread_exit",       "a thread body's RTL freed its slot") \
  X(sched_tick_carry, "sched_wake",        "the tick at $20 wrapped into $22")  \
  X(vbl_run_idle,     "vbl_queue_run",     "a queue with nothing in it")        \
  X(vbl_run_job,      "vbl_queue_run",     "a job reached by RTL")              \
  X(vbl_run_kept,     "vbl_queue_run",     "a job came back with carry set and stays") \
  X(vbl_run_dropped,  "vbl_queue_run",     "a one-shot came back and its slot was freed") \
  X(nmi_reentered,    "nmi_enter",         "an NMI while one was already running: straight back out") \
  X(nmi_rng_tick,     "nmi_leave",         "$1EB4 clear, so the random number state stepped") \
  X(nmi_rng_held,     "nmi_leave",         "...or set, and it did not")         \
  X(nmi_returned,     "nmi_leave",         "out by the RTL and the trampoline's PLB, to the RTI") \
  X(nmi_vectored,     "nmi_vector",        "the trampoline at the vector, through to the handler") \
  X(nmi_unblanked,    "nmi_unblank",       "the brightness put back and the joypads waited for") \
  X(vbl_run_last_dropped, "vbl_queue_run",     "...and it was the last one, so the walk stopped") \
  X(text_map_cleared, "text_map_clear",    "the text layer's map blanked") \
  X(reset_cold,       "reset_clear",       "no top-scores table in WRAM: every byte cleared") \
  X(reset_warm,       "reset_clear",       "the four magic words found, so $7E:2000-$2127 survive") \
  /* Thread bodies, between one yield and the next. */                        \
  X(victims_retired,   "victims_resume",     "a victim retired for good, stepped over") \
  X(victims_list_end,  "victims_resume",     "the victim list ran out: next frame starts at the top") \
  X(victims_start_one, "victims_resume",     "a victim came into range and its thread is started") \
  X(victims_stop_one,  "victims_resume",     "a live victim went out of range and its thread is stopped") \
  X(object_requests,  "object_resume",     "the object thread's handler left requests to serve") \
  X(object_list_end,  "object_polled",     "the object list ran out: next walk starts at the top") \
  X(object_give,      "object_polled",     "an object came into range and is given an actor") \
  X(object_free,      "object_polled",     "an object went out of range and its actor is freed") \
  X(actors_held,      "actors_checked",    "$80:9D5B said wait, so no entry this frame") \
  X(actors_nearer,    "actors_measured",   "an entry nearer than any this pass") \
  X(actors_none_near, "actors_checked",    "a pass ended with nobody within $100") \
  X(actors_pick,      "actors_checked",    "a pass ended and the nearest entry is started") \
  X(tile_anim_stepped, "tile_anim_resume", "a slot's count ran out and its next frame was read") \
  X(tile_anim_looped, "tile_anim_resume",  "a sequence ran out with $FFFF and started over") \
  X(tile_anim_stopped, "tile_anim_resume", "a sequence ran out with $FFFE and its slot stopped") \
  X(tile_anim_upload, "tile_anim_resume",  "a frame changed, so the upload job is queued") \
  X(tile_anim_ended,  "tile_anim_resume",  "every slot stopped, and the thread ends by RTL") \
  X(player_still,     "player_move",       "no movement handler at $2A this frame") \
  X(player_event,     "player_hurt",       "an event request in $50, handed to the ROM") \
  X(player_no_recovery, "player_hurt",     "$6A set, so the hit recovery count held") \
  X(player_recovered, "player_hurt",       "the hit recovery count ran out and was held at $FFFF") \
  X(player_level_won, "player_won",        "no neighbours left, and the ROM ends the level") \
  X(player_died,      "player_dead",       "no health left, and the ROM has the player die") \
  X(walk_twice,       "player_walk",       "$54 bit 15: the ROM walks twice") \
  X(walk_solid,       "player_walk",       "solid ground where the step lands") \
  X(walk_reaction,    "player_walk",       "...with a reaction of its own, so the ROM walks") \
  X(walk_tethered,    "player_walk",       "too far from the other player to step there") \
  X(walk_obstructed,  "player_walk",       "someone standing where the step lands") \
  X(walk_overlapping, "player_walk",       "...and where the player stands too, so the step goes ahead") \
  X(walk_off_map,     "player_walk",       "a step off the map") \
  /* Vblank jobs, which write the PPU. */                                      \
  X(vram_flush_held,  "vram_queue_flush",  "$26 bit 14 set: the queue held back a frame") \
  X(vram_flush_empty, "vram_queue_flush",  "nothing queued")                   \
  X(vram_flush_sent,  "vram_queue_flush",  "the queue sent and emptied")       \
  X(upload_frames,    "sprite_upload_flush", "sprite frames sent ahead of OAM") \
  X(upload_oam_only,  "sprite_upload_flush", "no frames queued: OAM alone")    \
  X(boss_plane_shown, "camera_scroll_job", "the big figure's plane in reach: both scrolls written") \
  X(boss_plane_x_out, "camera_scroll_job", "x out of reach: both scrolls parked") \
  X(boss_plane_y_out, "camera_scroll_job", "y out of reach: x written, then both parked") \
  X(boss_bg_dma_empty, "boss_bg_dma",      "queued with no rows left to send") \
  X(boss_bg_dma_sent, "boss_bg_dma",       "the big figure's rows sent and the queue emptied") \
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
  X(player_sfx_only,  "player_collide",      "an id whose entire reaction is a sound effect") \
  X(player_pickup_entry,"player_collide",    "an id that dispatched to $80:F87B, a pickup") \
  X(player_item_entry,"player_collide",      "an id that dispatched to $80:F8D6, the other pickup") \
  X(player_spawn_0,   "player_collide",      "id $2D: spawn $82:E0B4 kind 0, and bump a counter") \
  X(player_spawn_1,   "player_collide",      "id $2E: kind 1, and a counter that stops at five") \
  X(spawn_count_capped,"player_collide",     "...that counter already at five, so nothing stored") \
  X(player_spawn_2,   "player_collide",      "id $2F: kind 2, and $0500 of score") \
  X(player_spawn_3,   "player_collide",      "id $30: kind 3, and $1000 of score") \
  X(player_heal_entry,"player_collide",      "id $27: three health back") \
  X(player_state_gate,"player_collide",      "$80:F9AE: the entry that gates on the player's state") \
  X(player_state_ignored,"player_collide",   "...state 2 or 4, which ignore collisions outright") \
  X(state_tail_recovering,"player_state_tail","$80:DC09 declined: still inside the hurt window") \
  X(state_tail_busy,  "player_state_tail",   "...declined: the player is in a state other than 0") \
  X(state_tail_sentinel,"player_state_tail", "...declined: $10 held the $FD72 sentinel") \
  X(state_tail_queued,"player_state_tail",   "...and the one path that writes: $80:DC1E queued") \
  X(heal_at_full,     "player_collide",      "...refused, because health was already ten") \
  X(heal_capped,      "player_collide",      "...ceilinged, because three would have overshot") \
                                                                                \
  /* $80:F87B player_pickup — the player's side of taking an item. */            \
  X(pickup_taken,     "player_pickup",       "an item added to a player's inventory") \
  X(pickup_capped,    "player_pickup",       "a counter that hit the $0999 ceiling") \
  X(pickup_digit_carry,"player_pickup",      "a pickup whose BCD addition needed a decimal adjust") \
  X(pickup_autoselect,"player_pickup",       "a pickup by a player holding no weapon, which selects one") \
                                                                                \
  /* $80:F8D6 player_item_pickup — the same four decisions over the items. */    \
  X(item_taken,       "player_item_pickup",  "an item added to a player's item array") \
  X(item_pickup_capped,"player_item_pickup", "a counter that hit the $0099 ceiling") \
  X(item_digit_carry, "player_item_pickup",  "an item whose BCD addition needed a decimal adjust") \
  X(item_autoselect,  "player_item_pickup",  "a pickup by a player holding no item, which selects one") \
                                                                                \
  /* $80:EA63 weapon_select_next, and $80:EA4B under it. */                      \
  X(weapon_none_held, "weapon_select_next",  "a search that began with nothing selected, so from slot 0") \
  X(weapon_scan_empty,"weapon_select_next",  "an inventory slot the search found empty") \
  X(weapon_scan_wrap, "weapon_select_next",  "a search that ran off the end of the inventory and wrapped") \
  X(weapon_none_found,"weapon_select_next",  "fifteen tries and every slot empty") \
  X(weapon_unchanged, "weapon_select_next",  "the search settled on the weapon already held — B with one weapon") \
  X(weapon_changed,   "weapon_select_next",  "a different weapon selected")      \
  X(weapon_no_data,   "weapon_select_next",  "...to nothing at all, so $80:EA4B returned at once") \
                                                                                \
  /* $80:EAA8 item_select_next — the same search over the shorter array. */      \
  X(item_none_held,   "item_select_next",    "a search that began with nothing selected, so from slot 0") \
  X(item_scan_empty,  "item_select_next",    "an item slot the search found empty") \
  X(item_scan_wrap,   "item_select_next",    "a search that ran off the end of the items and wrapped") \
  X(item_none_found,  "item_select_next",    "thirteen tries and every slot empty") \
  X(item_unchanged,   "item_select_next",    "the search settled on the item already held — A with one item") \
  X(item_changed,     "item_select_next",    "a different item selected")        \
                                                                                \
  /* $81:8888 enemy_collide, and the death at $81:8727. */                      \
  X(enemy_ignore,     "enemy_collide",       "an enemy told about an id of the other side, which it ignores") \
  X(enemy_act,        "enemy_collide",       "an enemy told about a hit of its own side") \
  X(enemy_hit_special,"enemy_collide",       "one of the two ids with a routine of its own — declined") \
  X(enemy_died,       "enemy_collide",       "a hit that took an enemy's last health") \
  X(enemy_no_damage,  "enemy_collide",       "a hit whose damage-table entry is zero — no shot can carry one here") \
  X(enemy_survived,   "enemy_collide",       "an enemy that lived through a hit")                                                                                   /* $81:8506 enemy_survived_react — the splice into a parked stack. */           X(react_already,    "enemy_survived_react","a second hit while the flash from the first is still on")   X(react_splice,     "enemy_survived_react","a JSL frame written into a suspended thread's stack") \
                                                                               \
  X(shot_expire,      "shot_collide",        "a shot that hit something and is ending") \
  X(shot_expire_zero, "shot_collide",        "...on id 0, the one path that runs no CMP at all") \
  X(shot_pass,        "shot_collide",        "an id a shot flies straight through") \
                                                                                \
  /* $83:A364 victim_collide — eight ids, eight endings. */                      \
  X(victim_latched,   "victim_collide",      "a victim whose fate was already settled, so this is ignored") \
  X(victim_claim_a,   "victim_collide",      "one side claimed a victim — $18 latched with bit 15 clear") \
  X(victim_claim_b,   "victim_collide",      "the other side claimed one — $18 latched $8000") \
  X(victim_event_2,   "victim_collide",      "id $0B, which has an ending to itself") \
  X(victim_event_3,   "victim_collide",      "one of the three ids that share the give-up code") \
  X(victim_keep_id,   "victim_collide",      "...with $26 set, the one path that leaves the collision id alone") \
  X(victim_event_4,   "victim_collide",      "id $34, which has an ending to itself") \
  X(victim_event_ff,  "victim_collide",      "id $FF, the only ending that is not a small integer") \
  X(victim_ignore,    "victim_collide",      "an id a victim has no reaction to at all") \
                                                                                \
  /* $80:CAEE object_collide — the object manager's side of a pickup. */         \
  X(object_spent,     "object_collide",      "an object already taken this frame, so its id is gone") \
  X(object_taken,     "object_collide",      "one of the three ids that pick an object up") \
  X(object_ignore,    "object_collide",      "an id that touches an object without taking it") \
                                                                                \
  /* $81:C4A6 monster_collide — the second enemy subsystem. */                  \
  X(monster_ignore_low,  "monster_collide",  "an id below the object range, which it ignores") \
  X(monster_ignore_high, "monster_collide",  "an id above the object range and below a shot's") \
  X(monster_take,     "monster_collide",     "an object taken out from under whoever else wanted it") \
  X(monster_latched,  "monster_collide",     "...refused, because something already happened to this one") \
  X(monster_latch_alt,"monster_collide",     "...latching $46 rather than $42, on the one value that redirects") \
  X(monster_hit,      "monster_collide",     "a weapon shot, which is the only id it takes damage from") \
  X(monster_special,  "monster_collide",     "id $5D, which has a routine of its own — declined") \
  X(monster_died,     "monster_collide",     "a hit that took its last health") \
  X(monster_fatal_id, "monster_collide",     "...id $5E, which skips the subtraction and dies outright") \
  X(monster_no_damage,"monster_collide",     "a hit whose damage-table entry is zero — no shot can carry one here") \
  X(monster_survived, "monster_collide",     "one that lived through a hit")   X(monster_react_already,"monster_survived_react","a second hit while ACTOR_ATTR still holds the first")   X(monster_react_splice,"monster_survived_react","a JSL frame written into a suspended thread's stack") \
  X(monster_kill_award,"monster_collide",    "$81:BBEB paid out; its guard is the parked id being zero") \
  X(monster_kill_free,"monster_collide",     "...and that guard refusing — killed by something with no id, so worth nothing") \
                                                                                \
  /* $81:B41C enemy_b41c_collide — the third copy of the same subsystem. */      \
  X(b41c_ignore,      "enemy_b41c_collide",  "an id below a weapon shot's, which it ignores") \
  X(b41c_act,         "enemy_b41c_collide",  "a weapon shot, the only id it reacts to") \
  X(b41c_hit_special, "enemy_b41c_collide",  "id $5E or $5D, each with a routine of its own — declined") \
  X(b41c_special_grounded,"enemy_b41c_collide","id $61 while on the ground, which queues $81:B16E instead of damage") \
  X(b41c_special_airborne,"enemy_b41c_collide","...id $61 in the air, which falls through and takes the damage") \
  X(b41c_hit,         "enemy_b41c_collide",  "the damage path, which raises the hit flag its body reads") \
  X(b41c_died,        "enemy_b41c_collide",  "a hit that took its last health — and awards nothing") \
  X(b41c_no_damage,   "enemy_b41c_collide",  "a hit whose damage-table entry is zero — no shot can carry one here") \
  X(b41c_survived,    "enemy_b41c_collide",  "one that lived through a hit")    \
                                                                                \
  /* $81:CDDE enemy_cdde_collide — a fourth handler, and a different shape. */   \
  X(cdde_ignore,      "enemy_cdde_collide",  "an id below a weapon shot's — which still clears the parked id") \
  X(cdde_unmatched,   "enemy_cdde_collide",  "a shot it does not recognise, off the end of the three comparisons") \
  X(cdde_counted,     "enemy_cdde_collide",  "id $64 or $6F, which it only tallies") \
  X(cdde_survived,    "enemy_cdde_collide",  "a damaging hit its countdown absorbed") \
  X(cdde_killed_reacting,"enemy_cdde_collide","...the last one, but it was already reacting, so it is only tallied") \
  X(cdde_react_begin, "enemy_cdde_react",    "the flash: its next-routine pointer swapped for $81:CC2F") \
                                                                                \
  /* $81:B592 enemy_b592_collide — twenty-four bytes, and no weapon in sight. */ \
  X(b592_ignore,      "enemy_b592_collide",  "an id that is neither $07 nor $08") \
  X(b592_survived,    "enemy_b592_collide",  "a touch its countdown absorbed")   \
  X(b592_exhausted,   "enemy_b592_collide",  "the touch that took the last of it") \
                                                                                \
  /* $81:C440 — the same routine one stage earlier. Only the two branches that \
     differ from $81:C4A6's are marked; everything else is one shared body. */   \
  X(c440_special,     "monster_c440_collide","id $5D, which goes to $81:847E rather than $81:BB05 — declined") \
  X(c440_survived,    "monster_c440_collide","a survivor, which flashes through $81:8506 rather than $81:BAB3") \
                                                                                \
  /* $80:F9BE and $80:F979 — two more of the player table's state-gate group. */ \
  X(player_queue_entry,"player_collide",     "id $0A: $80:F9BE, the state gate with its store inlined") \
  X(player_queue_ignored,"player_collide",   "...state 2 or 4, so nothing is queued") \
  X(player_queue_next,"player_collide",      "...and the store: $80:F9D0 queued, which costs a point of health") \
  X(player_hurt_alt,  "player_collide",      "id $0B: $80:F979, the second kind of hit") \
  X(player_hurt_alt_recovering,"player_collide","...inside the invulnerability window, so it does not land") \
  X(player_hurt_alt_ignored,"player_collide","...state 2, 4 or $0E, the only entry with a third") \
  X(player_hurt_alt_taken,"player_collide",  "...and the hit itself: ACTOR_DP_EVENT $C000, timer back to $30") \
                                                                                \
  /* $81:D7F6 enemy_d7f6_collide — level 17's, and the fifth copy of $81:8888. */ \
  X(d7f6_ignore,      "enemy_d7f6_collide",  "an id below a weapon shot's, which it ignores outright") \
  X(d7f6_hit,         "enemy_d7f6_collide",  "a weapon shot, and the id parked for the body's award to read") \
  X(d7f6_fatal_id,    "enemy_d7f6_collide",  "id $5E, which reaches the death tail without subtracting") \
  X(d7f6_special,     "enemy_d7f6_collide",  "id $5D, which JMLs to $81:847E — declined") \
  X(d7f6_died,        "enemy_d7f6_collide",  "a hit that took its last health, which is usually the first") \
  X(d7f6_no_damage,   "enemy_d7f6_collide",  "a hit whose damage-table entry is zero — no shot can carry one here") \
  X(d7f6_survived,    "enemy_d7f6_collide",  "one that lived through a hit — on one health, so it took no damage") \
  /* $81:E6E4 enemy_e6e4_collide — the tenth copy, shared by two behaviours. */ \
  X(e6e4_airborne,    "enemy_e6e4_collide",  "ACTOR_Z non-zero: off the ground, so every collision is refused") \
  X(e6e4_ignore,      "enemy_e6e4_collide",  "an id below a weapon shot's, which it ignores outright") \
  X(e6e4_hit,         "enemy_e6e4_collide",  "a weapon shot, and the id parked at $22 for the body's award") \
  X(e6e4_bubble,      "enemy_e6e4_collide",  "id $5E, which JMLs to $81:83C6 — the bubble tail") \
  X(e6e4_freeze,      "enemy_e6e4_collide",  "id $5D, which JMLs to $81:847E") \
  X(e6e4_died,        "enemy_e6e4_collide",  "a hit that took its last health, of the three it starts with") \
  X(e6e4_no_damage,   "enemy_e6e4_collide",  "a hit whose damage-table entry is zero") \
  X(e6e4_survived,    "enemy_e6e4_collide",  "one it lived through, and the only path that stores health") \
                                                                                \
  /* $81:990B enemy_990b_collide — the eleventh copy, and the two-pool one. */   \
  X(d990b_ignore,     "enemy_990b_collide",  "an id below a weapon shot's, which it ignores outright") \
  X(d990b_hit,        "enemy_990b_collide",  "a weapon shot, with the id parked at $2E") \
  X(d990b_bubble,     "enemy_990b_collide",  "id $5E, which JMLs to $81:83C6 with nothing in front of it") \
  X(d990b_freeze_peer,"enemy_990b_collide",  "id $5D with a companion record at $40, whose collision id is cleared") \
  X(d990b_freeze_alone,"enemy_990b_collide", "...or $40 is $FFFF, and the freeze touches only this page") \
  X(d990b_died,       "enemy_990b_collide",  "a hit that took its last health — the one death in the family that decrements nothing") \
  X(d990b_no_damage,  "enemy_990b_collide",  "a hit whose damage-table entry is zero, so neither pool moves") \
  X(d990b_staggered,  "enemy_990b_collide",  "it lived, and the same damage emptied the second pool at $4A") \
  X(d990b_survived,   "enemy_990b_collide",  "...or it did not, and the ordinary survivor's splice runs") \
  X(d990b_stagger_already, "enemy_990b_stagger", "already flashing, so the emptied pool posts nothing at all") \
  X(d990b_stagger_posted,  "enemy_990b_stagger", "...or it is not, and $81:9643 is parked in $12 for the body to find") \
  X(d990b_spin_ignore,"enemy_990b_spin_collide", "the same id test again, from the handler that answers during the spin") \
  X(d990b_spin_hit,   "enemy_990b_spin_collide", "...or a weapon shot, which flashes it and takes nothing off either pool") \
                                                                                \
  /* $81:9B6B enemy_9b6b_collide — level 21's, and the sixth copy of $81:8888. */ \
  X(d9b6b_ignore,     "enemy_9b6b_collide",  "an id below a weapon shot's, which it ignores outright") \
  X(d9b6b_hit,        "enemy_9b6b_collide",  "a weapon shot, with the id parked at $30") \
  X(d9b6b_fatal_id,   "enemy_9b6b_collide",  "id $5E, the bubble gun, counted at $7E:1FDC before the splice") \
  X(d9b6b_bubble_slot_0, "enemy_9b6b_bubble", "...credited to the first player's MARTIAN BUBBLED tally") \
  X(d9b6b_bubble_slot_1, "enemy_9b6b_bubble", "...or to the second's")            \
  X(d9b6b_special,    "enemy_9b6b_collide",  "id $5D, which JMLs to $81:847E — declined") \
  X(d9b6b_died,       "enemy_9b6b_collide",  "a hit that took its last health, which awards nothing") \
  X(d9b6b_no_damage,  "enemy_9b6b_collide",  "a hit whose damage-table entry is zero — no shot can carry one here") \
  X(d9b6b_survived,   "enemy_9b6b_collide",  "one that lived through a hit") \
                                                                                \
  /* $81:F534 actor_f534_collide — five ids, one store, and a guard on two. */    \
  X(f534_latch,       "actor_f534_collide",  "id $01, $03 or $04, latched unconditionally") \
  X(f534_latch_guarded,"actor_f534_collide", "id $05 or $06 with the guard word at four, so also latched") \
  X(f534_guard_refused,"actor_f534_collide", "...and the guard refusing, which is the store being skipped") \
  X(f534_ignore,      "actor_f534_collide",  "an id it has no reaction to at all") \
                                                                                \
  /* $83:A264 victim_a264_collide — victim_collide's sibling one page over. */    \
  X(a264_give_up,     "victim_a264_collide", "id $FF, $03 or $04: the ending that is not a rescue") \
  X(a264_claim_a,     "victim_a264_collide", "id $05 — one side claimed it, $18 latched with bit 15 clear") \
  X(a264_claim_b,     "victim_a264_collide", "id $06 — the other side, $18 latched $8000") \
  X(a264_ignore_named,"victim_a264_collide", "id $02 or $5E, ignored by name rather than by falling through") \
  X(a264_ignore_low,  "victim_a264_collide", "anything else below a weapon shot") \
  X(a264_shot_clears, "victim_a264_collide", "a weapon shot, which *clears* the event word instead of setting one") \
  X(a264_flag_set,    "victim_a264_collide", "$81:8191 setting its byte in the array at $7E:605A") \
  X(a264_flag_none,   "victim_a264_collide", "...refused, because the index word held $FFFF") \
                                                                                \
  /* $81:9063 enemy_9063_collide — level 5's, the seventh copy of $81:8888. */    \
  X(d9063_ignore,     "enemy_9063_collide",  "an id below a weapon shot's, which it ignores outright") \
  X(d9063_hit,        "enemy_9063_collide",  "a weapon shot, with the id parked at $30") \
  X(d9063_fatal_id,   "enemy_9063_collide",  "id $5E, a bare JML to $81:83C6 — declined") \
  X(d9063_special,    "enemy_9063_collide",  "id $5D, which JMLs to $81:847E — declined") \
  X(d9063_died,       "enemy_9063_collide",  "a hit that took its last health, which awards nothing") \
  X(d9063_no_damage,  "enemy_9063_collide",  "a hit whose damage-table entry is zero — no shot can carry one here") \
  X(d9063_survived,   "enemy_9063_collide",  "one that lived through a hit") \
                                                                                \
  /* $81:AC92 enemy_ac92_collide — level 49's, the ninth copy of $81:8888. */     \
  X(dac92_ignore,     "enemy_ac92_collide",  "an id below a weapon shot's, which it ignores outright") \
  X(dac92_hit,        "enemy_ac92_collide",  "a weapon shot, with the id parked at $3E") \
  X(dac92_special,    "enemy_ac92_collide",  "id $5E, a bare JML to $81:83C6 — declined") \
  X(dac92_freeze,     "enemy_ac92_collide",  "id $5D, which leaves through enemy_freeze") \
  X(dac92_fatal_id,   "enemy_ac92_collide",  "id $67, which dies outright with no subtraction") \
  X(dac92_died,       "enemy_ac92_collide",  "a hit that took its last health, which awards nothing") \
  X(dac92_no_damage,  "enemy_ac92_collide",  "a hit whose damage-table entry is zero — no shot can carry one here") \
  X(dac92_survived,   "enemy_ac92_collide",  "one that lived through a hit") \
                                                                                \
  /* $81:845E actor_845e_collide — thirty-two bytes and no stores at all. */      \
  X(d845e_park_named, "actor_845e_collide",  "id $03, $05 or $06, matched unmasked — the thread parks") \
  X(d845e_ignore,     "actor_845e_collide",  "an id below a weapon shot's, which leaves it running") \
  X(d845e_pass,       "actor_845e_collide",  "id $5E, the one weapon it does not stop for") \
  X(d845e_park,       "actor_845e_collide",  "any other weapon, which parks the thread") \
                                                                                \
  /* $81:F6A3 shot_f6a3_collide — the shot handler four weapons share. */        \
  X(f6a3_record,      "shot_f6a3_collide",   "id $01, $03 or $04, the three the shot's body is told about") \
  X(f6a3_ignore,      "shot_f6a3_collide",   "any other id, which the shot flies straight through") \
  X(f4ef_player,      "actor_f4ef_collide",  "id $05 or $06 — one of the two players, and nothing else counts") \
  X(f4ef_ignore,      "actor_f4ef_collide",  "any other id, which this actor does not notice at all") \
  X(player_gate_e331, "player_collide",      "id $35: $80:F999, the state gate before $80:E331") \
  X(player_gate_e331_ignored, "player_collide", "...in state 2, 4 or $0C, where it does nothing") \
  X(player_gate_e331_taken, "player_collide", "...in any other: the pose at $80:E33E, and no more hits") \
  X(player_exit_queued, "player_collide",    "id $37, the exit door: the pose that leaves by it is queued") \
  X(exit_door_ignore, "exit_door_collide",   "not a player: the door takes no notice") \
  X(exit_door_same_player, "exit_door_collide", "the player who touched a door last, again") \
  X(exit_door_new_player, "exit_door_collide", "another player: counted, and the door's thread woken") \
  X(f25d_outright,    "actor_f25d_collide",  "id $61, which counts its state down whatever hits are left") \
  X(f25d_ignore,      "actor_f25d_collide",  "an id below $5C that is not 3 or 4") \
  X(f25d_hit,         "actor_f25d_collide",  "a hit, and one fewer it can take") \
  X(f25d_last_hit,    "actor_f25d_collide",  "...the one that took the count below zero") \
  X(f8fc_touched,     "actor_f8fc_collide",  "id 1, 3, 4 or $35, remembered at $1C") \
  X(f8fc_ignore,      "actor_f8fc_collide",  "any other id") \
                                                                                \
  /* $82:DEEB and $82:F1C2 — seven bytes and thirty-six. */                      \
  X(deeb_stop,        "actor_deeb_collide",  "id $FF, which latches itself and parks the thread") \
  X(deeb_ignore,      "actor_deeb_collide",  "any other id, which it does nothing about") \
  X(f1c2_act_id,      "actor_f1c2_collide",  "id $01, $05 or $06 — one of the three it answers by name") \
  X(f1c2_act_shot,    "actor_f1c2_collide",  "...or anything at or above a weapon shot, once masked") \
  X(f1c2_ignore,      "actor_f1c2_collide",  "an id below a shot's — the one exit of the four that clears carry") \
                                                                                \
  /* $82:9660 boss_9660_collide — level 25's, and the first in bank $82. */      \
  X(boss_invulnerable,"boss_9660_collide",   "its own record wearing id $09, which refuses every hit") \
  X(boss_flashing,    "boss_9660_collide",   "...or the flash from the last hit still running") \
  X(boss_ignore,      "boss_9660_collide",   "an id below a weapon shot's, which it ignores") \
  X(boss_alt_cheap,   "boss_9660_collide",   "id $62 or $70 answered as $5C — the scheduler clock's coin toss") \
  X(boss_alt_dear,    "boss_9660_collide",   "...and the same two ids answered as $5D") \
  X(boss_remap_61,    "boss_9660_collide",   "id $61, which is always answered as $60") \
  X(boss_remap_6f,    "boss_9660_collide",   "id $6F, which is always answered as $63") \
  X(boss_hit,         "boss_9660_collide",   "a hit that reached the damage path, whatever it then did") \
  X(boss_died,        "boss_9660_collide",   "the hit that took its last health, which ends its thread's loop") \
  X(boss_no_damage,   "boss_9660_collide",   "a hit whose damage-table entry is zero") \
  X(boss_survived,    "boss_9660_collide",   "one it lived through, and the only path that stores health") \
  /* $82:AA2E boss_aa2e_collide — records 20, 40 and 47. */                      \
  X(aa2e_invulnerable,"boss_aa2e_collide",   "its own record wearing id $00 or $09, which refuses every hit") \
  X(aa2e_flashing,    "boss_aa2e_collide",   "...or $4C still running from the last hit") \
  X(aa2e_ignore,      "boss_aa2e_collide",   "an id below a weapon shot's, which it ignores") \
  X(aa2e_immune,      "boss_aa2e_collide",   "id $60, parked and then refused") \
  X(aa2e_toss_cheap,  "boss_aa2e_collide",   "id $70 or $5F on a tick with the low two bits clear, answered as $5C") \
  X(aa2e_toss_dear,   "boss_aa2e_collide",   "...and on any other tick, answered as $5D") \
  X(aa2e_as_5c,       "boss_aa2e_collide",   "id $6F or $62, always answered as $5C") \
  X(aa2e_remap_61,    "boss_aa2e_collide",   "id $61, always answered as $66") \
  X(aa2e_hit,         "boss_aa2e_collide",   "a hit that reached the damage path, whatever it then did") \
  X(aa2e_died,        "boss_aa2e_collide",   "the hit that took its last health: DEC $46, health not stored") \
  X(aa2e_no_damage,   "boss_aa2e_collide",   "a hit whose damage-table entry is zero") \
  X(aa2e_survived,    "boss_aa2e_collide",   "one it lived through, and the only path that stores health") \
  /* The last six handlers the dispatcher declined on. */                         \
  X(f330_counted,     "actor_f330_collide",  "id $5D, $62, $5C or $65, which DECs $10 and sets carry") \
  X(f330_park,        "actor_f330_collide",  "id $FF, which sets carry and nothing else") \
  X(f330_ignore,      "actor_f330_collide",  "anything else") \
  X(a638_park,        "actor_a638_collide",  "id $FF, which DECs $2A and sets carry") \
  X(a638_ignore,      "actor_a638_collide",  "anything else") \
  X(a84ac_ignore,     "actor_84ac_collide",  "an id below a weapon shot's") \
  X(a84ac_other,      "actor_84ac_collide",  "a shot other than $62, whose parked id is cleared again") \
  X(a84ac_running,    "actor_84ac_collide",  "id $62 while $32 is not negative") \
  X(a84ac_took,       "actor_84ac_collide",  "id $62 with $32 negative: $32 = 4, DEC $30, carry set") \
  X(b95f_ignore,      "enemy_b95f_collide",  "an id below a weapon shot's") \
  X(b95f_fatal_id,    "enemy_b95f_collide",  "id $5D, which reaches the death tail without subtracting") \
  X(b95f_died,        "enemy_b95f_collide",  "a hit that took its last health") \
  X(b95f_no_damage,   "enemy_b95f_collide",  "a hit whose damage-table entry is zero") \
  X(b95f_survived,    "enemy_b95f_collide",  "one it lived through, into $81:8506") \
  X(eff0_ignore,      "enemy_eff0_collide",  "an id below a weapon shot's") \
  X(eff0_bubble,      "enemy_eff0_collide",  "id $5E, into $81:83C6") \
  X(eff0_freeze,      "enemy_eff0_collide",  "id $5D, into $81:847E") \
  X(eff0_toss_5c,     "enemy_eff0_collide",  "id $70 on an odd tick, answered as $5C") \
  X(eff0_toss_5d,     "enemy_eff0_collide",  "...and on an even one, answered as $5D") \
  X(eff0_hit,         "enemy_eff0_collide",  "a hit, stored however low it goes, into $81:8506") \
  X(c8c3_player,      "actor_c8c3_collide",  "raw id $05 or $06, a player, into $81:C6EC") \
  X(c8c3_bubble,      "actor_c8c3_collide",  "id $5E, into $81:83C6") \
  X(c8c3_freeze,      "actor_c8c3_collide",  "id $5D, into $81:847E") \
  X(c8c3_61,          "actor_c8c3_collide",  "id $61, which DECs $22") \
  X(c8c3_68,          "actor_c8c3_collide",  "id $68, which counts at $1FC4 and turns to face the shot") \
  X(c8c3_face_left,   "actor_c8c3_collide",  "...with the other record left of $0C, so the mirror bit is set") \
  X(c8c3_face_right,  "actor_c8c3_collide",  "...or not, so it is cleared") \
  X(c8c3_ignore,      "actor_c8c3_collide",  "a masked id below a weapon shot's") \
  X(c8c3_shot,        "actor_c8c3_collide",  "any other shot, into $81:C6EC") \
                                                                                \
  /* $81:847E enemy_freeze — where every copy of $81:8888 sends id $5D. */       \
  X(enemy_hit_freeze, "enemy_collide",       "id $5D, the ice weapon — served by enemy_freeze") \
  X(b41c_hit_freeze,  "enemy_b41c_collide",  "...the same id, one page over")    \
  X(freeze_counting,  "enemy_freeze",        "a hit short of the fifth, which only bumps the counter") \
  X(freeze_already,   "enemy_freeze",        "the fifth or later, refused because it is still flashing") \
  X(freeze_took,      "enemy_freeze",        "a hit that actually froze something") \
  X(freeze_slot_0,    "enemy_freeze",        "...credited to the first player's MONSTER FROZEN tally") \
  X(freeze_slot_1,    "enemy_freeze",        "...or to the second's")            \
                                                                                \
  /* $81:83C6 enemy_bubble_react — where the same copies send id $5E. */          \
  X(bubble_already,   "enemy_bubble_react",  "a second hit while the flash from the first is still on") \
  X(bubble_splice,    "enemy_bubble_react",  "a JSL frame written into a suspended thread's stack") \
                                                                                \
  /* $81:D301 enemy_d301_collide — level 9's, the eighth copy of $81:8888. */    \
  X(d301_stop,        "enemy_d301_collide",  "id $FF, the positive verdict — the only branch any input takes") \
  X(d301_ignore,      "enemy_d301_collide",  "an id below a weapon shot's, which it ignores outright") \
  X(d301_shot_immune, "enemy_d301_collide",  "the ordinary shot $5C, parked at $36 and then thrown away") \
  X(d301_special,     "enemy_d301_collide",  "id $5D, which leaves through enemy_freeze") \
  X(d301_died,        "enemy_d301_collide",  "a hit that took its last health, leaving the negative verdict") \
  X(d301_no_damage,   "enemy_d301_collide",  "a hit whose damage-table entry is zero") \
  X(d301_survived,    "enemy_d301_collide",  "one it lived through, which returns from the reaction rather than tail-calling it") \
  X(d301_reseed,      "enemy_d301_collide",  "the 25-in-256 draw that rewrites its trail and its next routine") \
  X(d301_no_reseed,   "enemy_d301_collide",  "...and the 231 that do not") \
                                                                                \
  /* $80:9D39 rng_next — the generator's one branch. */                         \
  X(rng_counter_twice,"rng_next",            "an addition that overflowed as signed, so the counter advanced twice") \
  X(rng_counter_once, "rng_next",            "...and one that did not")          \
                                                                                \
  /* $80:C7D9 score_add. */                                                     \
  X(score_slot_0,     "score_add",           "points credited to the first score slot") \
  X(score_slot_1,     "score_add",           "points credited to the second — two players") \
  X(score_discard,    "score_add",           "points earned by a side no score slot owns") \
  X(score_carry,      "score_add",           "a score that carried past four BCD digits") \
  X(score_digit_carry,"score_add",           "an award whose BCD addition needed a decimal adjust") \
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
  X(fade_in_done,     "fade_in",            "the ramp reached full brightness and returned") \
                                                                                \
  /* $80:B123 actor_nearest — the four ways a slot is dismissed, and the two \
     that keep it. */                                                          \
  X(nearest_undrawn,  "actor_nearest",      "a slot with ACTOR_DRAW clear, which is most of them") \
  X(nearest_inactive, "actor_nearest",      "...drawn, but without flag bit 0") \
  X(nearest_wrong_id, "actor_nearest",      "a live record wearing none of the four ids it looks for") \
  X(nearest_candidate,"actor_nearest",      "one it measured the distance to")  \
  X(nearest_closer,   "actor_nearest",      "...and one that beat the best so far, so the search moved") \
                                                                                \
  /* $80:B379 actor_aligned — the same three dismissals as its sibling above, \
     then the four directions it can answer with and the two ways to find \
     nothing. */                                                                \
  X(aligned_undrawn,  "actor_aligned",      "a slot with ACTOR_DRAW clear, which is most of them") \
  X(aligned_inactive, "actor_aligned",      "...drawn, but without flag bit 0 — untaken for actor_nearest too") \
  X(aligned_wrong_id, "actor_aligned",      "a live record wearing none of the three ids it looks for") \
  X(aligned_up,       "actor_aligned",      "within a tile on X, and above the point") \
  X(aligned_down,     "actor_aligned",      "...or below it") \
  X(aligned_left,     "actor_aligned",      "not on X but within a tile on Y, and left of the point") \
  X(aligned_right,    "actor_aligned",      "...or right of it") \
  X(aligned_off,      "actor_aligned",      "a candidate lined up on neither axis, so the walk carried on") \
  X(aligned_none,     "actor_aligned",      "all 32 slots looked at and nothing lined up — the zero exit") \
                                                                                \
  /* $80:B093 actor_gap — no record, or which axis was the wider one. */        \
  X(gap_empty,        "actor_gap",          "asked about record zero, which is what $D4 reads in one-player mode") \
  X(gap_y_wider,      "actor_gap",          "the Y gap was at least the X gap, so it is the answer") \
  X(gap_x_wider,      "actor_gap",          "...or was smaller, so the X gap is")  \
                                                                                \
  /* $80:B18F actor_nearest_id3 — actor_nearest's five sites, over one id. */   \
  X(nearest3_undrawn, "actor_nearest_id3",  "a slot with ACTOR_DRAW clear, which is most of them") \
  X(nearest3_inactive,"actor_nearest_id3",  "...drawn, but without flag bit 0") \
  X(nearest3_wrong_id,"actor_nearest_id3",  "a live record not wearing collision id $03") \
  X(nearest3_candidate,"actor_nearest_id3", "one it measured the distance to")  \
  X(nearest3_closer,  "actor_nearest_id3",  "...and one that beat the best so far") \
                                                                                \
  /* $80:B1EC, $80:B22A actor_bearing_point, actor_bearing — the two halves of \
     the table index, and whether the dropped half of $80:B1EC's was ever \
     anything but zero, which is what makes its bug a bug and not a shape. */   \
  X(bearing_level,    "bearing_lookup",     "the record shares the point's Y")  \
  X(bearing_above,    "bearing_lookup",     "...or sits above it")              \
  X(bearing_below,    "bearing_lookup",     "...or below it")                   \
  X(bearing_column,   "bearing_lookup",     "the record shares the point's X")  \
  X(bearing_left,     "bearing_lookup",     "...or sits left of it")            \
  X(bearing_right,    "bearing_lookup",     "...or right of it")                \
  X(bearing_v_dropped,"actor_bearing_point","a call whose vertical answer $80:B208 discarded — the ROM bug, reached") \
  X(bearing_v_zero,   "actor_bearing_point","...or one already level, where the bug changes nothing") \
                                                                                \
  /* $80:B26B, $80:B2A5 player_in_range, player_bearing — the four exits of \
     the selection they share, and the direction table's first entry. */        \
  X(pick_b_nearer,    "player_pick",        "player B in range and strictly nearer than A") \
  X(pick_a_nearer,    "player_pick",        "...or in range and no nearer, so A wins without being re-checked") \
  X(pick_a_only,      "player_pick",        "player B out of range and A inside it") \
  X(pick_neither,     "player_pick",        "neither player close enough — the zero exit") \
  X(player_bearing_same,"player_bearing",   "the player is on exactly the same pixel, which answers UP rather than zero") \
  X(player_bearing_off,"player_bearing",    "...or is somewhere, which is every other call") \
                                                                                \
  /* $80:BF1B actor_notify_box — four bounds, two refusals, and the five ways      a visible record is dismissed before its handler is entered. */             X(notify_bound_clamped,"actor_notify_box",  "a rectangle bound was negative, so it was zeroed")   X(notify_bound_kept,  "actor_notify_box",  "...or was not, and stood")   X(notify_no_actors,   "actor_notify_box",  "an empty visible list — the LDY $9C exit")   X(notify_one_actor,   "actor_notify_box",  "exactly one visible record, which the DEY DEY refuses to test")   X(notify_no_id,       "actor_notify_box",  "a visible record with no collision id")   X(notify_self_id,     "actor_notify_box",  "...or wearing the caller's own, so nothing blasts itself")   X(notify_left_of,     "actor_notify_box",  "left of the box")   X(notify_right_of,    "actor_notify_box",  "...or right of it")   X(notify_above,       "actor_notify_box",  "inside on X but above the box")   X(notify_below,       "actor_notify_box",  "...or below it")   X(notify_hit,         "actor_notify_box",  "inside the box, so its handler was entered")                                                                                   /* $80:B3F1 actor_snap_to — two independent axes, each snapped or left. */    \
  X(snap_x_took,      "actor_snap_to",      "within a pixel on X, so X was snapped exactly onto the target") \
  X(snap_x_left,      "actor_snap_to",      "...or two or more away, so it was left where it was") \
  X(snap_y_took,      "actor_snap_to",      "the same on Y, which is the axis whose flags the caller gets") \
  X(snap_y_left,      "actor_snap_to",      "...and the same again") \
                                                                                \
  /* $80:BF67 actor_at_point — six ways to dismiss an entry and two endings. */  \
  X(at_point_empty,   "actor_at_point",     "nothing visible at all, so it never entered the walk") \
  X(at_point_self,    "actor_at_point",     "the record the caller asked to be ignored") \
  X(at_point_inactive,"actor_at_point",     "a visible record without flag bit 0") \
  X(at_point_no_id,   "actor_at_point",     "a record with no collision id")     \
  X(at_point_id_band, "actor_at_point",     "an id in the $0C..$33 band, which it steps over wholesale") \
  X(at_point_id_named,"actor_at_point",     "id $07 or $08, the two named exceptions below that band") \
  X(at_point_far_x,   "actor_at_point",     "close enough to look at, but more than six pixels away on X") \
  X(at_point_far_y,   "actor_at_point",     "...or on Y")                        \
  X(at_point_hit,     "actor_at_point",     "something inside the window, so it returns carry set") \
  X(at_point_none,    "actor_at_point",     "the walk ran out, which is the empty answer") \
  X(obstacle_empty,   "actor_obstacle_at_point", "nothing visible at all, so it never entered the walk") \
  X(obstacle_player_a,"actor_obstacle_at_point", "player A's record, which a walker is allowed through") \
  X(obstacle_player_b,"actor_obstacle_at_point", "...and player B's, so this one needs two players on the board") \
  X(obstacle_inactive,"actor_obstacle_at_point", "a visible record without flag bit 0") \
  X(obstacle_no_id,   "actor_obstacle_at_point", "a record with no collision id")     \
  X(obstacle_id_below_band, "actor_obstacle_at_point", "an id under $0C, which skips the range tests entirely") \
  X(obstacle_id_band, "actor_obstacle_at_point", "an id in the $0C..$33 band, stepped over wholesale") \
  X(obstacle_id_high, "actor_obstacle_at_point", "an id of $5C or above, over the ceiling the band test ends at") \
  X(obstacle_id_above_band, "actor_obstacle_at_point", "$34..$5B: past the band, under the ceiling, and still made to face the named chain") \
  X(obstacle_id_named,"actor_obstacle_at_point", "one of the seven singletons, six of them below the band and $37 above it") \
  X(obstacle_far_x,   "actor_obstacle_at_point", "close enough to look at, but more than six pixels away on X") \
  X(obstacle_far_y,   "actor_obstacle_at_point", "...or on Y")                        \
  X(obstacle_hit,     "actor_obstacle_at_point", "something in the way, so the step the caller proposed is refused") \
  X(obstacle_none,    "actor_obstacle_at_point", "the walk ran out, so the step is allowed") \
  X(terrain_attrs_bank_7e, "terrain_blocked*",   "the attribute table in WRAM bank $7E, which is everywhere it has been looked at") \
  X(terrain_attrs_bank_7f, "terrain_blocked*",   "...and in bank $7F, which nothing has yet been seen to do") \
  X(terrain_hit_upper,"terrain_blocked*",        "one of the three tiles across the top of the footprint blocks") \
  X(terrain_hit_lower,"terrain_blocked*",        "the row below does, which needs the whole top row clear first") \
  X(terrain_hit_last, "terrain_blocked*",        "the sixth and last tile does — the probe the ROM leaves without a branch") \
  X(terrain_clear,    "terrain_blocked*",        "all six tiles are clear, which is falling out of the loop") \
  X(psn_weapon_none,  "player_state_normal",     "the held weapon index is negative, so the ammunition check is skipped whole") \
  X(psn_not_firing,   "player_state_normal",     "...or it is a real weapon and the fire button is not down") \
  X(psn_weapon_empty, "player_state_normal",     "the held weapon has run out, and the routine writes bit 15 back over the pad word") \
  X(psn_weapon_ready, "player_state_normal",     "...or it has not, which clears that bit again") \
  X(psn_fire_low,     "player_state_normal",     "weapon index below 6: the fire flag files in $1E") \
  X(psn_fire_band,    "player_state_normal",     "6 through 12: it files in $20 instead") \
  X(psn_fire_high,    "player_state_normal",     "13 or above: back to $1E, which is the same exit by a different branch") \
  X(psn_dir_moving,   "player_state_normal",     "a direction this frame, latched into $26 as well as $24") \
  X(psn_dir_still,    "player_state_normal",     "...or none, and $26 keeps the last one") \
  X(psn_press_weapon, "player_state_normal",     "B on the edge: cycle to the next non-empty weapon") \
  X(psn_press_item,   "player_state_normal",     "A on the edge: cycle to the next item") \
  X(psn_spawn_swallowed, "player_state_normal",  "L or R with the flag already set: the press is eaten and the flag cleared") \
  X(psn_spawn,        "player_state_normal",     "...or the flag was clear, so a thread starts at $82:D8DB and a sound plays") \
  X(psn_press_none,   "player_state_normal",     "no button edge this frame, which is nearly all of them") \
  X(psn_t3_expired,   "player_state_normal",     "the fourth countdown reached zero, which clears $54 as well") \
  X(floor_plain,      "floor_effect",            "an ordinary floor: no bit 3, none of the three special words, and the routine returns") \
  X(floor_clear_2a,   "floor_effect",            "the $8000 tile, whose whole effect is `STZ $2A`") \
  X(floor_harm_plain, "floor_effect",            "the $0400 floor, which harms whoever stands on it unconditionally") \
  X(floor_gate_other_weapon, "floor_effect",     "the $4000 floor with any weapon but 3, which harms the same way") \
  X(floor_gate_open,  "floor_effect",            "...weapon 3, but the guard word is clear, so it harms after all") \
  X(floor_gate_shut,  "floor_effect",            "...weapon 3 with the guard set: the one floor a player can be immune to") \
  X(floor_mode_off,   "floor_effect",            "$70 is one of the two values that suppress the harm entirely") \
  X(floor_harm_cooling, "floor_effect",          "the cooldown has not expired, so the last one is still running") \
  X(floor_harm_start, "floor_effect",            "...it has, so $50 starts the effect and $52 begins counting again") \
  X(floor_belt,       "floor_effect",            "attribute bit 3: a conveyor, and the four directions follow") \
  X(floor_belt_up,    "floor_effect",            "$0108 — one pixel up, with no terrain test at all") \
  X(floor_belt_down,  "floor_effect",            "$0408 — one pixel down, likewise") \
  X(floor_belt_left,  "floor_effect",            "$0208 — one pixel left, likewise") \
  X(floor_belt_right, "floor_effect",            "$0028 — one pixel right, and the way is clear") \
  X(floor_belt_right_blocked, "floor_effect",    "...or it is not, which is the only direction that can be refused") \
  X(floor_belt_none,  "floor_effect",            "none of the four matched — every return from the harm path lands here") \
  X(bounds_x_negative,"terrain_out_of_bounds",   "a negative X, rejected before any arithmetic") \
  X(bounds_x_low,     "terrain_out_of_bounds",   "X within four quarter-tiles of the left edge") \
  X(bounds_x_high,    "terrain_out_of_bounds",   "X past the right edge — the one exit that keeps its own compare's carry") \
  X(bounds_y_negative,"terrain_out_of_bounds",   "a negative Y")                       \
  X(bounds_y_low,     "terrain_out_of_bounds",   "Y within two tiles of the top edge") \
  X(bounds_y_high,    "terrain_out_of_bounds",   "Y past the bottom edge")             \
  X(bounds_inside,    "terrain_out_of_bounds",   "the point is on the map, which is the only way out with carry clear") \
  X(speed_dir_still,  "step_propose",       "direction zero, so both deltas are zero and the candidate is where it already is") \
  X(speed_dir_moving, "step_propose",       "any of the eight real directions") \
  X(speed_single,     "step_propose",       "the mask and the tick disagree, so one pixel on each axis") \
  X(speed_double,     "step_propose",       "...and when they agree, two — which is the whole of how speed is expressed") \
  X(tether_mover_a,   "step_tether_blocked", "player A is asking, so the reference is player B") \
  X(tether_mover_b,   "step_tether_blocked", "...and anyone else asking is measured against player A") \
  X(tether_alone,     "step_tether_blocked", "the reference record is zero, which is every frame of a one-player game") \
  X(tether_inside,    "step_tether_blocked", "the candidate is inside the 224x176 window, the only exit that is not arithmetic") \
  X(tether_outside_x, "step_tether_blocked", "too far apart on X") \
  X(tether_outside_y, "step_tether_blocked", "...or on Y, which needs X to have passed first") \
  X(tether_closing,   "step_tether_blocked", "outside the window but strictly closing the gap, so allowed anyway") \
  X(tether_equal,     "step_tether_blocked", "outside, and exactly as far as the players already are — the BEQ that refuses a tie") \
  X(tether_leashed,   "step_tether_blocked", "outside, and not closing: the leash, and the only carry-set exit") \
  X(wide_floor_first, "terrain_blocked_wide", "the very first tile is below the priority threshold — the one exit that leaves Y as the caller passed it") \
  X(wide_floor_other, "terrain_blocked_wide", "...one of the other nine is, and that one has set Y itself") \
  X(wide_attr_upper,  "terrain_blocked_wide", "a tile in the top row of five carries attribute bit 1") \
  X(wide_attr_lower,  "terrain_blocked_wide", "...or one in the row below, which needs all five above it to pass") \
  X(wide_clear,       "terrain_blocked_wide", "all ten tiles pass both tests, which is falling off the end of the tenth LSR") \
  X(bit2_outside,     "terrain_point_bit2",  "the point is off the map, and the JSL answers before the routine reads a tile") \
  X(bit2_hit,         "terrain_point_bit2",  "...or it is on the map and its tile carries bit 2") \
  X(bit2_clear,       "terrain_point_bit2",  "...or it does not, the only way out with carry clear") \
  X(bit8_hit,         "terrain_point_bit8",  "the tile under the point carries bit 8") \
  X(bit8_clear,       "terrain_point_bit8",  "...or it does not") \
  X(bit3_hit,         "terrain_tile_bit3",   "the tile carries bit 3") \
  X(bit3_clear,       "terrain_tile_bit3",   "...or it does not") \
  X(bit12_gap_upper,  "terrain_footprint_bit12", "one of the three tiles across the top lacks bit 12, so the footprint is off the surface") \
  X(bit12_gap_lower,  "terrain_footprint_bit12", "...or one in the row below, which needs all three above it to carry it") \
  X(bit12_all,        "terrain_footprint_bit12", "all six carry it — the inverted answer, and the only carry-clear exit") \
  X(partner_no_a,     "partner_near",        "player A's record ANDs to zero with the caller's, which means there is no player A") \
  X(partner_no_b,     "partner_near",        "...or player B's does, which is every frame of a one-player game") \
  X(partner_mover_a,  "partner_near",        "the caller is player A, so the reference is B") \
  X(partner_mover_b,  "partner_near",        "...or it is anything else, and the reference is A") \
  X(partner_far_x,    "partner_near",        "more than $80 apart on X") \
  X(partner_far_y,    "partner_near",        "...or on Y, which needs X to have passed first") \
  X(partner_close,    "partner_near",        "inside the square on both axes, which is what the one caller is waiting to hear") \
  X(lzss_read_spent,  "lzss_read_byte",      "the stream is exhausted — the SEC exit, and the only end marker the format has") \
  X(column_priority,  "tilemap_copy_column", "a tile below the level's priority threshold, so bit 13 is forced on as it is copied") \
  X(column_plain,     "tilemap_copy_column", "...and one at or above it, copied across untouched") \
  X(row_priority,     "tilemap_copy_row",    "the same rule one axis over: a tile under the threshold, priority forced on") \
  X(row_plain,        "tilemap_copy_row",    "...and one at or above it") \
  X(request_pending,  "vram_queue_request",  "a transfer was already asked for -- the BIT/BMI exit, whose Z is the caller's A") \
  X(request_made,     "vram_queue_request",  "the frame's transfer actually requested") \
  X(split_x_left,     "camera_split_x",      "the tilemap cursor is in the left screen, so the overflow lands at $400") \
  X(split_x_right,    "camera_split_x",      "...or in the right one, so it wraps back to word 0 instead") \
  X(scroll_at_limit,  "camera_scroll",       "the camera is as far as the map goes, so nothing happens -- the CMP exit") \
  X(scroll_at_zero,   "camera_scroll",       "...or against the near edge, the one exit that hands back a carry it never set") \
  X(scroll_mid_tile_fwd,"camera_scroll",     "a pixel of movement that crossed no tile boundary, going towards the far edge") \
  X(scroll_mid_tile_back,"camera_scroll",    "...and the same going the other way, where the boundary is a remainder of 7") \
  X(scroll_x_split,   "camera_scroll_x",     "a new column whose two halves come from two places in the map") \
  X(scroll_x_whole,   "camera_scroll_x",     "...and one the tilemap's wrap falls exactly on, copied in a single call") \
  X(follow_held,      "camera_follow",       "bit 14 of $26 set -- the camera held still, which $80:AB8D does across a bulk tilemap blit") \
  X(follow_no_players,"camera_follow",       "neither player on the board, so there is nothing to centre on") \
  X(follow_midpoint,  "camera_follow",       "both players up, so the view centres on the midpoint of the two") \
  X(follow_player_a,  "camera_follow",       "player A alone, which is every frame of a one-player game") \
  X(follow_player_b,  "camera_follow",       "...and player B alone, which needs A gone from a two-player one") \
  X(follow_left,      "camera_follow",       "the target is behind the camera on X") \
  X(follow_right,     "camera_follow",       "...or ahead of it") \
  X(follow_x_still,   "camera_follow",       "...or exactly on it, the one X delta that scrolls nothing") \
  X(follow_up,        "camera_follow",       "the target is above the camera") \
  X(follow_down,      "camera_follow",       "...or below it") \
  X(follow_y_still,   "camera_follow",       "...or exactly on it")                \
                                                                                   \
  /* $82:8F93 boss_step — the speed the caller asked for, the two probes of each   \
     pass, the axis that pass commits, and whether anything moved at all. */       \
  X(boss_step_double, "boss_step",           "the caller passed #$6969, so the deltas double and $2C ticks") \
  X(boss_step_single, "boss_step",           "...or anything else, which is a single step") \
  X(boss_cardinal,    "boss_step",           "a cardinal direction: one axis, one pass") \
  X(boss_diagonal,    "boss_step",           "...or a diagonal, which gets a pass per axis and may take one of them") \
  X(boss_lead_blocked,"boss_step",           "the first of the pass's two probes is in terrain, so the axis is refused") \
  X(boss_lead_clear,  "boss_step",           "...or is clear, and the second is worth testing") \
  X(boss_trail_blocked,"boss_step",          "the second probe is in terrain -- one corner of the leading edge fits and the other does not") \
  X(boss_commit_x,    "boss_step",           "bit 3 of the probe index set, so this pass writes $1E62") \
  X(boss_commit_y,    "boss_step",           "...or clear, and it writes $1E64") \
  X(boss_moved_x,     "boss_step",           "X ended somewhere else, which is the first thing the exit compares") \
  X(boss_moved_y,     "boss_step",           "X held but Y moved -- a diagonal that took only its vertical half") \
  X(boss_stuck,       "boss_step",           "neither moved: carry set, and the caller reaches for the RNG") \
                                                                                   \
  /* $82:9265, $82:92D6 — the four hitboxes under the big figure, and the box   \
     it walks on things with. */                                                   \
  X(boss_parts_plain, "boss_place_parts",    "$36 is zero, so the four parts trail down and to the right") \
  X(boss_parts_mirrored,"boss_place_parts",  "...or it is not, and the same four X offsets come from the second half of the table") \
                                                                                   \
  /* $80:CCBF apu_next_byte — the 16-bit cursor incremented eight bits at a time. */ \
  X(apu_src_step,     "apu_next_byte",       "the low byte advanced without carrying, which is 255 calls in 256") \
  X(apu_src_wrap,     "apu_next_byte",       "...and the 256th, where the second INC runs and takes over the flags") \
                                                                                   \
  /* $80:CC7C apu_load_set -- a count, a block, and the bytes under it. */         \
  X(apu_set_end,      "apu_load_set",        "a count of zero, which ends the set") \
  X(apu_set_block,    "apu_load_set",        "...or not, and command $0A announces a block") \
  X(apu_set_count,    "apu_load_set",        "a byte sent with the count's low byte still nonzero") \
  X(apu_set_borrow,   "apu_load_set",        "...or zero, so the high byte is decremented first") \
                                                                                   \
  /* $80:CB1A apu_boot, and the IPL upload at $80:CB61 inside it. */              \
  X(ipl_block,        "apu_boot",            "a block of the driver goes over byte by byte") \
  X(ipl_done,         "apu_boot",            "the size of zero, and the jump into the driver") \
                                                                                   \
  /* $80:9D5B spawn_has_room — the two ceilings every spawn in the game clears. */ \
  X(spawn_load_full,  "spawn_has_room",      "the weighted census is at 138 or past it, so the spawner sleeps a frame") \
  X(spawn_load_ok,    "spawn_has_room",      "...or under it, and the thread count is worth asking about") \
  X(spawn_threads_full,"spawn_has_room",     "eighteen live threads: room on the board, none in the scheduler") \
  X(spawn_room,       "spawn_has_room",      "both ceilings clear -- the only answer that lets an actor exist") \
                                                                                   \
  /* $80:9C90 sin_deg — the table's three kinds of entry. */                       \
  X(sin_positive,     "sin_deg",             "a byte with bit 7 clear, masked to $00FF") \
  X(sin_negative,     "sin_deg",             "...or set, and sign-extended with ORA #$FF00") \
  X(sin_sentinel,     "sin_deg",             "the $FF at index 90, the one answer a signed byte could not hold") \
                                                                                   \
  /* $80:F327 actor_publish_pos — one record or two. */                            \
  X(publish_one,      "actor_publish_pos",   "a single record, written through $08 as an index") \
  X(publish_two,      "actor_publish_pos",   "...or a stacked pair, the upper one placed a pixel down and a Z in front") \
                                                                                   \
  /* $81:8024 nearest_player_dist — who is on the board, and who turned out nearer. */ \
  X(nearest_has_a,    "nearest_player_dist", "player A is up, so a real distance goes into $1E") \
  X(nearest_no_a,     "nearest_player_dist", "...or is not, and $1E keeps the $FFFF it was primed with") \
  X(nearest_has_b,    "nearest_player_dist", "player B is up") \
  X(nearest_no_b,     "nearest_player_dist", "...or is not, which is every call of a one-player game") \
  X(nearest_a_wins,   "nearest_player_dist", "A strictly nearer -- the BCC exit, whose flags are a discarded subtraction") \
  X(nearest_b_wins,   "nearest_player_dist", "B strictly nearer, which needs two players and cannot happen without them") \
  X(nearest_tie,      "nearest_player_dist", "the two are exactly equidistant, and the tie goes to B") \
                                                                                   \
  /* $80:9570 wave_hdma_build — one frame of the screen wobble. */                  \
  X(wave_over,        "wave_hdma_build",     "the length went negative: the effect is finished and nothing is written") \
  X(wave_building,    "wave_hdma_build",     "...or it has not, and the whole table is rebuilt") \
  X(wave_second_header,"wave_hdma_build",    "X reached $F1, so the second repeat header goes in over a parameter's high byte") \
  X(wave_off_axis,    "wave_hdma_build",     "the last parameter was not zero, so the table keeps its length this frame") \
  X(wave_hold,        "wave_hdma_build",     "...it was zero, but the hold counter has frames left") \
  X(wave_retract,     "wave_hdma_build",     "...and it has not: one scanline comes off, at the one moment it cannot show") \
                                                                                   \
  /* $81:9BF3 actor_step_bearing — a step, per axis, against three tests. */        \
  X(bearing_rest,     "actor_step_bearing",  "a rest frame: one in four, on which nothing is even tested") \
  X(bearing_move,     "actor_step_bearing",  "...or one of the other three") \
  X(bearing_terrain,  "actor_step_bearing",  "the candidate is on a tile an enemy may not stand on") \
  X(bearing_bounds,   "actor_step_bearing",  "...or off the edge of the level") \
  X(bearing_occupied, "actor_step_bearing",  "...or somebody else is already standing there") \
  X(bearing_clear,    "actor_step_bearing",  "...or all three agree and the axis is allowed") \
  X(bearing_took_x,   "actor_step_bearing",  "the X step was taken, so the Y step is tested from the new column") \
  X(bearing_took_y,   "actor_step_bearing",  "the Y step was taken") \
                                                                                   \
  /* $81:C16B, $81:C00B — the big monster's walk cycle and what it carries. */      \
  X(monster_anim_hold,"monster_anim",        "the leg timer has not expired, so $2C is last frame's facing") \
  X(monster_anim_advance,"monster_anim",     "...or it has, and the walk takes its next leg") \
  X(monster_anim_mirror,"monster_anim",      "a west-facing direction: set the flip bit and return without placing the load") \
  X(monster_anim_plain,"monster_anim",       "...or one of the other six, which clears it and does place it") \
  X(monster_empty_handed,"monster_place_carried","holding nothing -- $28 is $FFFF and the TAX never happens") \
  X(monster_carrying, "monster_place_carried","...or holding something, which gets put beside whichever way it faces") \
                                                                                   \
  /* $81:BB75, $81:BBA4 — the two states that decide where the monster goes. */     \
  X(monster_board_far,"monster_seek",        "nothing of the four ids within $D0, so ask about the two players instead") \
  X(monster_player_about,"monster_seek",     "...and one of them is close enough, so the creature stays") \
  X(monster_gives_up, "monster_seek",        "...or neither is, and INC $2A ends the thread at the bottom of its loop") \
  X(monster_dead_band,"monster_seek",        "between $B4 and $D0: too far to chase, too near to count as an empty board") \
  X(monster_hands_full,"monster_seek",       "inside $B4 but $26 is set, so it will not start a second chase") \
  X(monster_hands_free,"monster_seek",       "...or it is empty-handed, and the chase state is installed") \
  X(monster_drops_victim,"monster_deliver",  "home to within 16 pixels on both axes and holding something, which is freed") \
  X(monster_still_travelling,"monster_deliver","...or not home, or home with empty hands -- one instruction serves all three") \
  X(monster_nothing_near,"monster_deliver",  "the shared tail: nothing inside $B4, so the state stands") \
  X(monster_gives_chase,"monster_deliver",   "...or something is, and $12 gets $BEE3 whatever is still being carried") \
                                                                                   \
  /* $81:BEE3 monster_chase, in readable C. */                                     \
  X(chase_gave_up,    "monster_chase",       "nothing within $B4, so it wanders off in a random straight line") \
  X(chase_straight,   "monster_chase",       "the target straight up, down, left or right") \
  X(chase_diagonal,   "monster_chase",       "...or on a diagonal, so it closes the smaller gap first") \
  X(chase_stepped,    "monster_chase",       "the step taken") \
  X(chase_met_someone,"monster_chase",       "an actor where the step lands, so it waits") \
  X(chase_met_ground, "monster_chase",       "solid ground and nothing to leap, so it waits") \
  X(chase_leapt,      "monster_chase",       "...or something to leap, so the ROM chases") \
                                                                                   \
  /* The zombies, in readable C: port/zombie.h. Both kinds share each site. */      \
  X(zombie_walked,    "zombie_*_walk",       "a step straight ahead") \
  X(zombie_waited,    "zombie_*_walk",       "someone standing in the way, so it waits") \
  X(zombie_turned,    "zombie_*_walk",       "solid ground ahead, so it turns and follows the wall") \
  X(zombie_rounded_corner, "zombie_*_follow", "the wall gave way, so it turns back towards it") \
  X(zombie_followed_wall, "zombie_*_follow", "a step along the wall") \
  X(zombie_chased,    "zombie_*_chase",      "a step towards whoever is nearest") \
  X(zombie_lost_target, "zombie_*_chase",    "the target too far away, so it gives up") \
  X(zombie_on_top,    "zombie_*_chase",      "...or right on top of it, likewise") \
  X(zombie_chase_stuck, "zombie_fast_chase", "a fast chase's step that went nowhere") \
  X(zombie_wandered,  "zombie_*_chase",      "a random straight heading, and a walk along it") \
  X(zombie_noticed,   "zombie_*_decide",     "something near enough, so it gives chase") \
  X(zombie_left,      "zombie_*_decide",     "neither player within reach, so it leaves") \
  X(zombie_quiet,     "zombie_fast_decide",  "$24 not negative, so it does not look") \
  /* The evil dolls, in readable C: port/doll.h. */                              \
  X(doll_left, "doll_seek", "nobody within reach, so it leaves") \
  X(doll_swung, "doll_seek", "within 16 on both axes, so it swings its axe") \
  X(doll_swing_waited, "doll_seek", "...on a frame the step timer had not run out") \
  X(doll_on_diagonal, "doll_seek", "exactly on a diagonal, so it throws and steps") \
  X(doll_threw, "doll_seek", "an axe thrown") \
  X(doll_throw_waited, "doll_seek", "...or not, the last throw too recent") \
  X(doll_stepped_across, "doll_seek", "a step across") \
  X(doll_stepped_down, "doll_seek", "a step up or down") \
  X(doll_step_blocked, "doll_seek", "both ways blocked by solid ground") \
  X(doll_dashed, "doll_seek", "lined up, so it charges") \
  X(doll_dash_short, "doll_seek", "...too close to be worth a charge") \
  X(doll_dash_spent, "doll_dash", "a charge run out") \
  X(doll_knocked_back, "doll_seek", "hit, and knocked back") \
  X(doll_knock_faceless, "doll_seek", "...not, with no facing to be knocked back from") \
  X(doll_knock_ground, "doll_seek", "...not, solid ground where it would land") \
  X(doll_knock_edge, "doll_seek", "...not, the edge of the level there") \
  X(doll_landed, "doll_knocked", "down from a knock-back") \
  X(doll_threw_on_landing, "doll_knocked", "...and an axe thrown on landing") \
  X(doll_leapt_out, "doll_leap_out", "out of the toy box and on the floor") \
  /* The player's poses, in readable C: port/pose.h. */                          \
  X(pose_changed, "pose_*", "the buttons changed, so the pose starts again") \
  X(pose_stood, "pose_*", "a stand started") \
  X(pose_walked, "pose_*", "a walk started") \
  X(pose_stepped, "pose_walk*", "the walk cycle moved on") \
  X(pose_fired, "pose_*", "a round taken and the shot's thread started") \
  X(pose_fire_empty, "pose_*", "...or no rounds left, and nothing happens") \
  X(pose_weapon_shown, "pose_*", "the hand weapon shown, the way the player faces") \
  X(pose_arc_ready, "pose_arc_ready", "the picture before a leap, its time up") \
  X(pose_arc_flew, "pose_arc", "a frame through the air") \
  X(pose_arc_fell, "pose_arc", "...high up and on the way down") \
  /* The clones, in readable C: port/clone.h. */                                 \
  X(clone_copied, "clone_frame", "a frame of moving as its player moves") \
  X(clone_chased, "clone_frame", "a frame of coming for the nearer player") \
  X(clone_left, "clone_frame", "chasing with neither player near enough, so it leaves") \
  X(clone_new_picture, "clone_frame", "the walk cycle moved on") \
  X(clone_changed_mode, "clone_frame", "the mode ran out, and a random time of the other began") \
                                                                                   \
  /* $81:CCE8 -- the slimes' frame. */                                             \
  X(slime_crawled, "slime_frame", "a pass of going straight on") \
  X(slime_felt, "slime_frame", "a pass of feeling along what turned it") \
  X(slime_turned_back, "slime_frame", "...where the way back round was clear, and taken") \
  X(slime_met_ground, "slime_frame", "solid ground where the lunge would end") \
  X(slime_met_someone, "slime_frame", "...or somebody standing there") \
  X(slime_faced, "slime_frame", "a turn towards whoever is nearest") \
  X(slime_faced_diagonal, "slime_frame", "...who was on a diagonal, so the table chose the axis") \
  X(slime_chose_attack, "slime_frame", "thinking: the draw said attack") \
  X(slime_chose_face, "slime_frame", "...or turn to face") \
  X(slime_set_off, "slime_frame", "an attack thought better of: a random way instead") \
  X(slime_attacked, "slime_frame", "...or begun, which is the ROM's, so the pass was declined") \
  X(slime_left, "slime_frame", "...or carry on, with neither player near, so it leaves") \
  X(slime_flash_ended, "slime_frame", "the last pass of a hit's flash") \
  X(slime_lunge_ended, "slime_frame", "the fifth picture, and the record moved up") \
  X(slime_glob_turned, "slime_glob_frame", "the top of its rise: moved across, to fall") \
  X(slime_glob_landed, "slime_glob_frame", "on the ground, so the splash is next") \
                                                                                   \
  /* $81:99F6, $81:9A5A -- the martians' frame. */                                 \
  X(martian_walked, "martian_frame", "a pass of walking") \
  X(martian_arrived, "martian_frame", "a pass of coming in across the top") \
  X(martian_held_fire, "martian_frame", "something lined up with it while it cooled") \
  X(martian_fired, "martian_frame", "...or with nothing to wait for: the ROM's, so the pass was declined") \
  X(martian_backed_off, "martian_frame", "a look that found its target too close") \
  X(martian_lined_up, "martian_frame", "...or near enough to line up with") \
  X(martian_approached, "martian_frame", "...or far enough to come closer to") \
  X(martian_left, "martian_frame", "...or out of sight, with neither player near, so it leaves") \
  X(martian_rested, "martian_frame", "the pass in four on which it does not step") \
  X(martian_step_refused, "martian_frame", "a step whose second axis was not allowed") \
  X(martian_came_down, "martian_frame", "arriving: a player above it, so it walks from now") \
  X(martian_climbed, "martian_frame", "...too near above its target: up on a slant, two steps") \
  X(martian_dropped, "martian_frame", "...too far above: down on one") \
                                                                                   \
  /* $83:B299 -- the spiders' frame. */                                            \
  X(spider_wandered, "spider_frame", "a pass of going straight on") \
  X(spider_felt, "spider_frame", "a pass of feeling along what turned it") \
  X(spider_turned_back, "spider_frame", "...where the way back round was clear, and taken") \
  X(spider_turned, "spider_frame", "a step refused, so a quarter turn") \
  X(spider_took_aim, "spider_frame", "someone near enough to run at") \
  X(spider_gave_up, "spider_frame", "...or too far by the time it aimed again, so it wanders") \
  X(spider_ran, "spider_frame", "a pass of running at its target") \
  X(spider_ran_twice, "spider_frame", "...of two steps") \
  X(spider_left, "spider_frame", "nobody about and neither player near, so it leaves") \
                                                                                   \
  /* $82:ABBD -- the colour fade's frame. */                                        \
  X(palfade_row_moved, "palfade_frame", "a row with a colour not yet its target's") \
  X(palfade_background_spared, "palfade_frame", "the background's eighth row, which is left alone") \
  X(palfade_swept, "palfade_frame", "the eighth row of a sweep that moved something: send the colours") \
  X(palfade_over, "palfade_frame", "...or of one that moved nothing, and the thread ends") \
  X(palfade_slept_sooner, "palfade_frame", "it sleeps a tick less from now on") \
                                                                                   \
  /* $80:D468 -- a player stuck fast. */                                           \
  X(stuck_frame, "stuck", "a frame of being stuck") \
  X(stuck_hurt, "stuck", "the hurt timer had run out, so they are hit again") \
  X(stuck_shook, "stuck", "right after left or left after right: a shake") \
  X(stuck_turned, "stuck", "a direction held, which they turn to face") \
                                                                                   \
  /* $82:8138 -- the big figure's colours. */                                      \
  X(figure_colours_set, "figure_colours_set", "sixteen colours into the background's eighth row") \
                                                                                   \
  /* $82:DD5C -- the bystander's frame. */                                         \
  X(bystander_slept, "bystander_frame", "no player in the box") \
  X(bystander_met, "bystander_frame", "a player in the box") \
  X(bystander_one_drawn, "bystander_frame", "one record on screen, which is not looked at") \
  X(bystander_ended, "bystander_frame", "told to end") \
                                                                                   \
  /* $82:87B1, $82:83E3 -- the flying saucer's pass. */                            \
  X(saucer_hunted, "saucer_frame", "a pass hunting") \
  X(saucer_on_target, "saucer_frame", "its quarry under the spot: it opens over them") \
  X(saucer_far, "saucer_frame", "its quarry far off: it swoops") \
  X(saucer_spent_swooped, "saucer_frame", "out of shots, and the draw said swoop") \
  X(saucer_spent_opened, "saucer_frame", "...or said open") \
  X(saucer_shot, "saucer_frame", "a shot") \
  X(saucer_shot_waited, "saucer_frame", "the draw said shoot, too soon after the last") \
  X(saucer_changed_sides, "saucer_frame", "its quarry crossed it, and the spot changed sides") \
  X(saucer_held, "saucer_frame", "a slanted way, on the pass in four it does not move") \
  X(saucer_clamped, "saucer_frame", "a move from left of the left margin, brought to it") \
  X(saucer_edge_across, "saucer_frame", "the move across is off the level, and is not taken") \
  X(saucer_edge_down, "saucer_frame", "...and the move down") \
  X(saucer_lost_them, "saucer_frame", "on target with no player under the spot: it shuts and hunts") \
  X(saucer_on_target_wobbled, "saucer_frame", "on target, a step of the circle") \
  X(saucer_swooped, "saucer_frame", "a pass swooping") \
  X(saucer_swoop_stopped, "saucer_frame", "a swoop stopped by the level's edge") \
  X(saucer_swoop_over, "saucer_frame", "a swoop's passes ran out: it opens") \
  X(saucer_open_wobbled, "saucer_frame", "open, a step of the circle") \
  X(saucer_open_timed_out, "saucer_frame", "open for long enough: it shuts and hunts") \
  X(saucer_open_reached, "saucer_frame", "open with its quarry within reach: the same") \
  X(saucer_hatch_opened, "saucer_frame", "a record taken for the hatch") \
  X(saucer_hatch_shut, "saucer_frame", "...and given back") \
  X(saucer_hatch_turned, "saucer_frame", "the hatch's next picture") \
  X(saucer_flash_began, "saucer_frame", "hit since the last pass: the flash's colours and mosaic") \
  X(saucer_flash_over, "saucer_frame", "the flash ran out: its own colours again") \
  X(saucer_shown, "saucer_frame_shown", "the rest of a pass that slept showing the hatch") \
  X(saucer_lights_changed, "saucer_frame", "the fifth pass: the lights change") \
  X(saucer_shot_down, "saucer_frame", "its health gone: the thread goes on to its end") \
                                                                                   \
  /* $80:9748, $80:99C1 -- the two screens before a game. */                       \
  X(title_menu_waited, "title_menu_frame", "a pass with no press that counts") \
  X(title_menu_blinked, "title_menu_frame", "the tenth pass: the chosen one blinks") \
  X(title_menu_over, "title_menu_frame", "the count ran out, or a choice ran it out") \
  X(players_screen_waited, "players_screen_frame", "a pass with no press that counts") \
  X(players_screen_blinked, "players_screen_frame", "the tenth pass: who has not joined blinks") \
  X(players_screen_over, "players_screen_frame", "the count ran out") \
                                                                                   \
  /* $81:9878 chainsaw_frame -- the chainsaw maniac's four states. */              \
  X(chainsaw_charged, "chainsaw_frame", "a pass straight on") \
  X(chainsaw_charge_turned, "chainsaw_frame", "stopped by the ground: a quarter turn by a draw") \
  X(chainsaw_charge_wandered, "chainsaw_frame", "...or with no count left, a turn back and it wanders") \
  X(chainsaw_wandered, "chainsaw_frame", "a pass wandering") \
  X(chainsaw_gave_chase, "chainsaw_frame", "somebody within 300: it chases") \
  X(chainsaw_strode_turned, "chainsaw_frame", "a pass after a turn") \
  X(chainsaw_turn_over, "chainsaw_frame", "its passes after a turn ran out") \
  X(chainsaw_turned_at_opening, "chainsaw_frame", "the step a quarter turn round was clear, so it turned") \
  X(chainsaw_turned_back, "chainsaw_frame", "a step was refused, so it turned back") \
  X(chainsaw_chased, "chainsaw_frame", "a pass chasing") \
  X(chainsaw_swung, "chainsaw_frame", "somebody beside it and the draw let it: it swings") \
  X(chainsaw_lost_them, "chainsaw_frame", "nobody within 250: it charges") \
  X(chainsaw_on_them, "chainsaw_frame", "on the same spot as its quarry: the same") \
  X(chainsaw_chase_gave_up, "chainsaw_frame", "the 120th short step of a chase") \
  X(chainsaw_began_cutting, "chainsaw_frame", "a hedge beside it: it begins to cut") \
  X(chainsaw_died, "chainsaw_frame", "its health gone: the thread goes on to its end") \
                                                                                   \
  /* $81:E4B2, $81:E558 fishman_frame -- the fishman's nine states. */             \
  X(fishman_swam, "fishman_frame", "a pass the way it faces") \
  X(fishman_swam_turned, "fishman_frame", "...and one turning at every opening") \
  X(fishman_turned_at_wall, "fishman_frame", "it could not swim on: a quarter turn") \
  X(fishman_turned_at_opening, "fishman_frame", "it could swim a quarter turn round, so it turned") \
  X(fishman_closed_in, "fishman_frame", "somebody within 128: it closes in") \
  X(fishman_looked_about, "fishman_frame", "the draw said stop and look about, which is the ROM's") \
  X(fishman_leapt, "fishman_frame", "it leaps, which is the ROM's") \
  X(fishman_leap_off_level, "fishman_frame", "the spot a leap would come down is off the level") \
  X(fishman_leap_taken, "fishman_frame", "...or has somebody on it") \
  X(fishman_closing_in, "fishman_frame", "a pass closing in") \
  X(fishman_bit, "fishman_frame", "somebody within 24: it bites") \
  X(fishman_lost_them, "fishman_frame", "nobody within 175: it swims on, or leaves") \
  X(fishman_patrolled, "fishman_frame", "a pass of its patrol") \
  X(fishman_turned_about, "fishman_frame", "stopped on its patrol: it turns about") \
  X(fishman_patrol_bit, "fishman_frame", "patrolling with somebody within 24: it bites") \
  X(fishman_in_column, "fishman_frame", "somebody within 24 across: it lines up down") \
  X(fishman_in_row, "fishman_frame", "...or within 24 down: it lines up across") \
  X(fishman_lining_up_down, "fishman_frame", "a pass lining up down") \
  X(fishman_lining_up_across, "fishman_frame", "...and one across") \
  X(fishman_lined_up, "fishman_frame", "within 16: it patrols again") \
  X(fishman_flew, "fishman_frame", "a pass of a leap") \
  X(fishman_dived, "fishman_frame", "...and one of the dive back into the water") \
  X(fishman_came_down, "fishman_frame", "its height came to nothing: it has landed") \
  X(fishman_landed_ashore, "fishman_frame", "landed, and it sets off some way by a draw") \
  X(fishman_landed_lurking, "fishman_frame", "landed, the one that keeps to its pool") \
  X(fishman_left, "fishman_frame", "neither player within 208: it leaves") \
  X(fishman_left_unshown, "fishman_frame", "...found when it came to show itself") \
                                                                                   \
  /* $81:AC1E werewolf_frame -- the werewolf's three states. */                    \
  X(werewolf_ran, "werewolf_frame", "a pass running") \
  X(werewolf_left, "werewolf_frame", "its target exactly 360 away: it leaves") \
  X(werewolf_on_them, "werewolf_frame", "on the same spot as its target: no step") \
  X(werewolf_hopped_hurt, "werewolf_frame", "hurt since it last looked, which is the ROM's") \
  X(werewolf_pounce_too_far, "werewolf_frame", "the draw said pounce, with its target 325 or more away") \
  X(werewolf_pounce_too_near, "werewolf_frame", "...or under 70 by the sum of the gaps") \
  X(werewolf_pounced, "werewolf_frame", "...or placed for it, which is the ROM's") \
  X(werewolf_struck, "werewolf_frame", "its target beside it: a strike begins") \
  X(werewolf_striking, "werewolf_frame", "a pass of a strike") \
  X(werewolf_blow_shown, "werewolf_frame", "a picture from the sixth on: the blow is drawn") \
  X(werewolf_strike_over, "werewolf_frame", "a strike's pictures ran out, which is the ROM's") \
  X(werewolf_flew, "werewolf_frame", "a pass of a pounce") \
  X(werewolf_came_down, "werewolf_frame", "its height came to nothing exactly") \
  X(werewolf_landed_beside, "werewolf_frame", "...beside whoever it pounced at") \
                                                                                   \
  /* $81:C8A1 footballer_frame -- the footballers' four states. */                 \
  X(footballer_stood, "footballer_frame", "a pass standing") \
  X(footballer_set_off, "footballer_frame", "its stand is over: it runs") \
  X(footballer_ran_loose, "footballer_frame", "a pass running loose") \
  X(footballer_veered, "footballer_frame", "the draw said veer") \
  X(footballer_ran_veering, "footballer_frame", "a pass of a veer") \
  X(footballer_straightened, "footballer_frame", "a veer's passes are up, and the draw said straighten out") \
  X(footballer_veered_again, "footballer_frame", "...or veer on") \
  X(footballer_went_at_them, "footballer_frame", "a player within 32, to one side: it goes at them") \
  X(footballer_turned_back, "footballer_frame", "stopped by the ground: it turns") \
  X(footballer_left, "footballer_frame", "neither player within 320: it leaves") \
                                                                                   \
  /* $80:C872, $80:C8B8, $80:A084, $80:A09E, $82:D88C -- DMA and its jobs. */       \
  X(dma_to_cgram, "dma_to_cgram", "colours sent") \
  X(dma_to_vram, "dma_to_vram", "bytes sent to VRAM") \
  X(palette_job, "palette_job", "all 256 colours sent") \
  X(background_job, "background_job", "the background's second copy sent") \
  X(tile_anim_job_sent, "tile_anim_job", "an animated tile's frame sent") \
                                                                                   \
  /* $80:A0C1, $80:A236, $80:A27D, $80:A106, $80:A180 -- the colour animations. */  \
  X(palcycle_turn_stepped, "palcycle_turn_*", "the run turned a place") \
  X(palcycle_turn_wrapped, "palcycle_turn_*", "...and the counter started over") \
  X(palcycle_pulse_between, "palcycle_pulse", "the green between its ends") \
  X(palcycle_pulse_top, "palcycle_pulse", "at the top, so the step turns down") \
  X(palcycle_pulse_floor, "palcycle_pulse", "back at the bottom, so it turns up") \
  X(palcycle_figure_stepped, "palcycle_figure", "the next arrangement") \
  X(palcycle_figure_restarted, "palcycle_figure", "the tenth, and back to the first") \
                                                                                   \
  /* $80:9CB2 -- the demo's playback. */                                           \
  X(demo_job_held, "demo_job", "the recording's pad held another frame") \
  X(demo_job_read_pair, "demo_job", "...or its frames ran out, and the next pair read") \
  X(demo_job_ended, "demo_job", "a real pad pressed, and the demo over") \
                                                                                   \
  /* $83:8255, $80:953B -- two jobs of the screens outside a level. */             \
  X(intro_screen_job, "intro_screen_job", "the logo screens' colours, scrolls and tile sent") \
  X(backdrop_drift_waited, "backdrop_drift_job", "not a fourth frame") \
  X(backdrop_drift_stepped, "backdrop_drift_job", "the backdrop moved a step along its path") \
  X(backdrop_drift_wrapped, "backdrop_drift_job", "...the last, and back to the first") \
                                                                                   \
  /* $81:810F -- a frame of the level's spawn list. */                             \
  X(spawnlist_held, "spawnlist_frame", "no room on the board") \
  X(spawnlist_rested, "spawnlist_frame", "the place was resting, and counted down") \
  X(spawnlist_nearer, "spawnlist_frame", "a ready place, the nearest so far") \
  X(spawnlist_farther, "spawnlist_frame", "...or no nearer than the nearest") \
  X(spawnlist_none_near, "spawnlist_frame", "the end of the list, with nothing within 256") \
  X(spawnlist_started, "spawnlist_frame", "...or the nearest set resting, to be started") \
                                                                                   \
  /* $80:CDFE -- a frame of a player. */                                           \
  X(player_frame_normal, "player_frame", "the ordinary state, which reads the pad") \
  X(player_frame_stuck, "player_frame", "...or stuck in slime") \
  X(player_frame_turning, "player_frame", "...or one of the two the pad only turns the player in") \
  X(player_frame_nobody_left, "player_frame", "nobody left to rescue and somebody rescued: the level goes on") \
  X(player_frame_hurt_timer_running, "player_frame", "the hurt timer counted down") \
  X(player_frame_hurt_timer_out, "player_frame", "...or had run out, and stays at minus one") \
  X(player_frame_still, "player_frame", "no movement handler: the player stands still") \
  X(player_frame_walked, "player_frame", "...or the walk is it") \
                                                                                   \
  /* $80:BE0C, $80:BE41 — a record's two ends. */                                   \
  X(slot_alloc_scan,  "actor_slot_alloc",    "this slot is taken, so try the one below it") \
  X(slot_alloc_took,  "actor_slot_alloc",    "...or it is free, and gets $0001 and the head of the list") \
  X(slot_alloc_full,  "actor_slot_alloc",    "all 32 are taken: A comes back holding a flags word, not a zero") \
  X(slot_free_not_mine,"actor_slot_free",    "ACTOR_THREAD is not the running thread, so the free does nothing and says nothing") \
  X(slot_free_already,"actor_slot_free",     "...or the record is not allocated, which declines the same way") \
  X(slot_free_took,   "actor_slot_free",     "...or it is ours and live: clear the flags word and install page zero") \
  X(slot_free_head,   "actor_slot_free",     "it was the head, so the list head becomes its link") \
  X(slot_free_walk,   "actor_slot_free",     "...or it is further down, and this link is not it") \
  X(slot_free_unlink, "actor_slot_free",     "...this one is: the predecessor's link jumps over it") \
  X(slot_free_unlisted,"actor_slot_free",    "walked off the end without finding it -- cleared, and still on the list") \
                                                                                   \
  /* $80:C07F and $80:C0A3 / $80:C139 — which panel, and whether to upload. */      \
  X(hud_phase_p1,     "hud_refresh",         "the toggle came up zero, so this call looks at player 1") \
  X(hud_phase_p2,     "hud_refresh",         "...or nonzero, and it looks at player 2") \
  X(hud_upload_idle,  "hud_refresh",         "not one of the six tests fired: no job queued, nothing to upload") \
  X(hud_upload_queued,"hud_refresh",         "...or something changed, so $1E7A is cleared and the tilemap goes on queue A") \
  X(hud_upload_accepted,"hud_refresh",       "queue A had room, so X comes back as the slot the search stopped on") \
  X(hud_upload_refused,"hud_refresh",        "...or all 16 were busy, and the upload is silently dropped for this frame") \
  X(hud_panel_off,    "hud_panel",           "$1E88/$1E8A is zero -- this player is not in the game, and the panel is skipped") \
  X(hud_panel_on,     "hud_panel",           "...or they are, and all six tests run") \
                                                                                   \
  /* The six change tests, in the order the panel makes them. */                    \
  X(hud_health_changed,"hud_panel",          "health differs from its shadow, so the bar is redrawn") \
  X(hud_health_same,  "hud_panel",           "...or it does not, and nothing is") \
  X(hud_item_changed, "hud_panel",           "a different item is selected, so its icon is redrawn") \
  X(hud_item_same,    "hud_panel",           "...or the same one still is") \
  X(hud_score_changed,"hud_panel",           "the 32-bit score moved, so eight digits are redrawn") \
  X(hud_score_same,   "hud_panel",           "...or both halves match their shadows") \
  X(hud_score_high_only,"hud_panel",         "the low half matched and the high half did not -- a carry past $9999 since the last look") \
  X(hud_item_count_changed,"hud_panel",      "the selected item's count moved") \
  X(hud_item_count_same,"hud_panel",         "...or it did not") \
  X(hud_weapon_changed,"hud_panel",          "a different weapon is selected, so its icon is redrawn") \
  X(hud_weapon_same,  "hud_panel",           "...or the same one still is") \
  X(hud_weapon_count_changed,"hud_panel",    "the selected weapon's count moved") \
  X(hud_weapon_count_same,"hud_panel",       "...or it did not") \
                                                                                   \
  /* $80:C766 / $80:C77D and $80:C702 / $80:C71B — the adapters that can refuse. */ \
  X(hud_weapon_shown, "hud_icon_adapter",    "a weapon is selected, so its 2x2 icon is drawn") \
  X(hud_weapon_none,  "hud_icon_adapter",    "...or none is, and the icon and the count beside it are both cleared") \
  X(hud_item_shown,   "hud_icon_adapter",    "an item is selected, so its 2x2 icon is drawn") \
  X(hud_item_none,    "hud_icon_adapter",    "...or none is, and both blocks are cleared") \
  X(hud_count_shown,  "hud_count_adapter",   "the selection indexes a real inventory slot, so three digits are drawn") \
  X(hud_count_none,   "hud_count_adapter",   "nothing is selected, so the three columns are blanked instead") \
  X(hud_count_over,   "hud_count_adapter",   "...or the slot is past the end of the inventory, which blanks them the same way") \
                                                                                   \
  /* $80:C4EC and the coda both number renderers share. */                          \
  X(hud_digit_printed,"hud_digit",           "a digit worth showing: tile $3C07 plus it, and the rotate remembers") \
  X(hud_digit_blank,  "hud_digit",           "...or a leading zero, which writes an empty tile and leaves carry alone") \
  X(hud_digits_printed,"hud_digits_end",     "at least one digit printed, so the number stands as drawn") \
  X(hud_digits_all_zero,"hud_digits_end",    "...or none did, and the last column is forced to a literal 0")

// `$80:AD2B blockmap_expand` has none either, and for the same reason:
// `src/port/levelmap.c` is written, one call is about six frames long, and the
// harness abandons every one of them. Its helper `$80:ACF6` is registered and
// has no sites of its own because it has no branches at all -- seven
// instructions, straight through.
//
// The two "nothing selected" paths inside `$80:C5C2` and `$80:C666` have no
// sites, and that one is not about frames. Both are live 65816 code with a
// `BMI` in front of them, and both are unreachable: each is reached only from
// adapters that have already branched on the same word. `src/port/hud.c` does
// not implement either, so there is nothing to mark — and a mark would have sat
// in the untaken list forever describing a thing no input can do, which is the
// same trap `src/port/apu.c` fell into once and was pulled back out of.
//
// `$80:CD20 lzss_decompress` has no sites here on purpose. `src/port/lzss.c` is
// written and is not registered — the harness cannot check a routine that
// outlives a frame, and `src/cosim/routines.c` says why at length. A site for a
// routine nothing calls is untaken forever, and three of those would quietly
// inflate the one number this file exists to keep honest.

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
