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
  X(boss_stuck,       "boss_step",           "neither moved: carry set, and the caller reaches for the RNG")

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
