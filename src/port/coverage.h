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
  X(span_cleared,     "game_clear",        "a stretch of WRAM cleared") \
  X(threads_cleared,  "threads_clear",     "the threads' pages and stacks cleared") \
  X(reset_cold,       "reset_clear",       "no top-scores table in WRAM: every byte cleared") \
  X(reset_warm,       "reset_clear",       "the four magic words found, so $7E:2000-$2127 survive") \
  /* Thread bodies, between one yield and the next. */                        \
  X(victims_retired,   "victims_resume",     "a victim retired for good, stepped over") \
  X(victims_list_end,  "victims_resume",     "the victim list ran out: next frame starts at the top") \
  X(victims_start_one, "victims_resume",     "a victim came into range and its thread is started") \
  X(victims_stop_one,  "victims_resume",     "a live victim went out of range and its thread is stopped") \
  X(pictures_shown,   "pictures_play",      "a picture put in the sprite's record, and its ticks slept") \
  X(pictures_ended,   "pictures_play",      "the zero that ends a list of pictures") \
  X(squirt_unfired,   "squirt_launch",      "fired from a tile that stops water: no shot") \
  X(squirt_launched,  "squirt_launch",      "one more shot in the air, and its sound") \
  X(squirt_dressed,   "squirt_dress",       "a record taken and given its picture, id, step and muzzle") \
  X(squirt_first_flew, "squirt_first_frame", "the first frame's step, and on") \
  X(squirt_first_stopped, "squirt_first_frame", "...into a tile that stops it") \
  X(squirt_second_flew, "squirt_second_frame", "the second frame's step, and the picture of flight") \
  X(squirt_second_stopped, "squirt_second_frame", "...into a tile that stops it") \
  X(squirt_splashed,  "squirt_splash",      "the handler taken away and the first splash shown") \
  X(squirt_splashed_2, "squirt_splash_2",   "the second splash shown") \
  X(squirt_gone,      "squirt_gone",        "one shot fewer, and its record freed") \
  /* $80:9515 the wobble's thread, after each build -- see port/trig.h. */      \
  X(wave_thread_start, "wave_thread_tests", "Start alone on a pad: the wobble ends") \
  X(wave_thread_over, "wave_thread_tests",  "the table's length gone negative: it ends") \
  X(wave_thread_again, "wave_thread_tests", "neither, and a frame's sleep") \
  X(wave_thread_called, "wave_thread_call", "the call of the build") \
  /* The game over's thread and two screens' starts -- see port/frontend.h. */ \
  X(game_over_frame,  "game_over_frame",    "a frame of the first loop: the layer up, the sprites down") \
  X(game_over_frame_2, "game_over_frame_2", "a frame of the second: the sprites, then the layer") \
  X(game_over_frame_last, "game_over_frame", "the layer as far up as a loop takes it") \
  X(portrait_copy,    "portrait_copy",      "a portrait's 208 tiles copied into the text map") \
  X(intro_fill,       "intro_fill",         "the logo screens' tilemap made in the scratch buffer") \
  X(intro_colours_copy, "intro_colours_copy", "colours for them copied from the cartridge") \
  /* $82:AD5A the big letters -- see port/text.h. */                            \
  X(text_big_begin,   "text_big_begin",     "the first place of a string of big letters") \
  X(text_big_glyph,   "text_big_glyph",     "a big letter's six rows of tiles written") \
  X(text_big_multiply, "text_big_multiply", "a big letter's number and its set's width, to the multiplier") \
  X(text_big_found,   "text_big_begin",     "a character that draws: its set's place and width, and off to multiply") \
  X(text_big_skipped, "text_big_begin",     "a character the table draws nothing for") \
  X(text_big_new_place, "text_big_begin",   "a byte of $FF: a new place") \
  X(text_big_ended,   "text_big_begin",     "the string's zero, and on to send the map") \
  /* $82:D8FD the radar's thread -- see port/radar_thread.h. */                 \
  X(radar_went_down,  "radar_frame",        "the player's radar is no longer up: the ROM's") \
  X(radar_recount,    "radar_frame",        "a neighbour fewer than the panel shows: the ROM's") \
  X(radar_none_near,  "radar_frame",        "round the whole list and none near: the square off the screen") \
  X(radar_wrapped,    "radar_frame",        "past the last neighbour, and back to the first") \
  X(radar_skipped_gone, "radar_frame",      "a neighbour saved or gone, passed over") \
  X(radar_skipped_far_x, "radar_frame",     "...or too far across") \
  X(radar_skipped_far_y, "radar_frame",     "...or too far up or down") \
  X(radar_shown,      "radar_frame",        "the square put where the next near neighbour is") \
  /* $83:9F11, $83:9F2E the neighbour who jumps -- see port/jumper.h. */        \
  X(jumper_rose,      "jumper_up",          "a pixel up") \
  X(jumper_turned,    "jumper_up",          "the top of the jump, and the first pixel down") \
  X(jumper_fell,      "jumper_down",        "a pixel down") \
  X(jumper_landed,    "jumper_down",        "on the ground, and its collision id back") \
  X(jumper_ended,     "jumper_up",          "its event gone negative: the ROM's") \
  X(jumper_stood,     "jumper_stand",       "on the ground: sixty frames asked for") \
  X(jumper_jumped,    "jumper_rested",      "the sixty over: off the ground, a pixel up") \
  X(jumper_disturbed, "jumper_rested",      "...or an event in them: the ROM's") \
  /* The logo screens, between their waits -- see port/logo.h. */               \
  X(logo_tile_stepped, "logo_slide",        "an other frame: the tile two on") \
  X(logo_tile_wrapped, "logo_slide",        "...and past twelve, back to six") \
  X(logo_slid,        "logo_slide",         "the first layer all the way across") \
  X(logo_cycled,      "logo_cycle",         "a fourth frame: sixteen colours down a place") \
  X(logo_risen,       "logo_rise",          "the first layer as far up as it goes") \
  X(logo_swept,       "logo_sweep",         "the two rows' tenth move") \
  X(logo_swept_2,     "logo_sweep_2",       "the third row's tenth move") \
  X(logo_flashed,     "logo_flash",         "the first colour white") \
  X(logo_held,        "logo_hold",          "the 255th frame") \
  X(logo_faded,       "logo_fade",          "the brightness at zero") \
  /* $80:AB8F a block put into the map -- see port/tile_rows.h. */              \
  X(tile_block_begun, "tile_block_begin",   "a block's tiles found, and where its first row goes") \
  X(tile_block_swap_asked, "tile_block_swap_ask", "the block at a place read, for its pair") \
  X(tile_rows_off_screen, "tile_block_rows", "a row the camera does not show") \
  X(tile_rows_whole,  "tile_block_rows",    "a row it shows all of") \
  X(tile_rows_left_end, "tile_block_rows",  "a row cut by the right of the screen") \
  X(tile_rows_right_end, "tile_block_rows", "...or by the left") \
  X(tile_rows_one_run, "tile_block_rows",   "a row queued as one transfer") \
  X(tile_rows_two_runs, "tile_block_rows",  "a row across the seam: two") \
  X(tile_rows_two_runs_kept, "tile_block_rows", "...with an earlier row's longer first run") \
  /* $82:AE76, $82:AEA2, $82:BA26 a level's name -- see port/card.h. */         \
  X(card_queue_full,  "card_drop",          "the queue full: a frame waited") \
  X(card_dropped,     "card_drop",          "sixteen lines down") \
  X(card_down,        "card_drop",          "the sixteenth frame") \
  X(card_bounced,     "card_bounce",        "a step of the bounce") \
  X(card_still,       "card_bounce",        "the table's zero") \
  X(card_wait_waited, "card_wait",          "no button, and frames left") \
  X(card_wait_pressed, "card_wait",         "a button") \
  X(card_wait_over,   "card_wait",          "six seconds and none") \
  /* $81:8294 the figure that rises -- see port/riser.h. */                     \
  X(riser_began,      "riser_begin",        "in front of every layer, and the first of five pictures") \
  X(riser_showed,     "riser_shown",        "the next of the five") \
  X(riser_lifted,     "riser_shown",        "...or after the fifth, the first pixel up") \
  X(riser_rose,       "riser_frame",        "a pixel up") \
  X(riser_next_picture, "riser_frame",      "a fourth step: the next picture") \
  X(riser_went_round, "riser_frame",        "past the fifth picture, and the first again") \
  X(riser_gone,       "riser_frame",        "the sixtieth step") \
  /* $82:E7C7, $82:E807 the thing that comes at a player -- see port/seeker.h. */\
  X(seeker_stepped,   "seeker_step",        "a pixel the way it was told") \
  X(seeker_flap_waited, "seeker_flap",      "the same picture") \
  X(seeker_flapped,   "seeker_flap",        "a fifth call: the other picture") \
  /* $82:B267 a cursor a pad moves about a screen -- see port/cursor.h. */      \
  X(cursor_same,      "cursor_frame",       "the direction it was last turn") \
  X(cursor_moved,     "cursor_frame",       "a direction newly held: a step that way") \
  X(cursor_let_go,    "cursor_frame",       "...or newly let go") \
  X(cursor_past_left, "cursor_frame",       "past the left edge, so in at the right") \
  X(cursor_past_right,"cursor_frame",       "...or the right, so in at the left") \
  X(cursor_past_top,  "cursor_frame",       "past the top, so in at the bottom") \
  X(cursor_past_bottom,"cursor_frame",      "...or the bottom, so in at the top") \
  X(cursor_button,    "cursor_frame",       "a button newly down: the ROM's") \
  X(cursor_button_held,"cursor_frame",      "...or still down from before") \
  X(cursor_start,     "cursor_frame",       "Start") \
  X(cursor_time_up,   "cursor_frame",       "three hundred turns with nothing pressed") \
  X(cursor_pick_letter,"cursor_pick",       "a character entered, and the place moved on") \
  X(cursor_pick_last, "cursor_pick",        "...or entered at the last place, which stays") \
  X(cursor_pick_space,"cursor_pick",        "the grid's `$3C`, entered as `$2F`") \
  X(cursor_pick_end,  "cursor_pick",        "the grid's `$3B`: the entry is over") \
  X(cursor_after_pick,"cursor_after",       "the clock set again after a pick") \
  X(cursor_after_move,"cursor_after",       "...or after a move's sound") \
  X(password_cheat,   "password_check",     "the one password tested by its letters") \
  X(password_good,    "password_check",     "a level, and one to nine neighbours") \
  X(password_ten,     "password_check",     "...or all ten") \
  X(password_no_level,"password_check",     "the middle two letters are no level's") \
  X(password_no_count,"password_check",     "the outer two are no count's on that level") \
  X(dim_darker,       "dim_frame",          "a screen a step darker") \
  X(dim_dark,         "dim_frame",          "...and dark") \
  X(dim_lighter,      "dim_frame",          "a screen a step lighter") \
  X(dim_light,        "dim_frame",          "...and as light as it goes") \
  /* $81:807E something started from a list -- see port/spawner.h. */           \
  X(spawn_at_place,   "spawn_entry",        "started at its place") \
  X(spawn_scattered,  "spawn_entry",        "...or near it, on clear ground") \
  X(spawn_ground_in_the_way,"spawn_entry",  "not started: something solid there") \
  X(spawn_off_the_level,"spawn_entry",      "...or the place is off the level") \
  /* The slime's attack between its sleeps, and four small things -- see     \
     port/slime.h, stepper.h, bolt.h, wander.h, vblank.h, dma.h. */          \
  X(slime_frame_ended_for_the_rom,"slime_frame_end", "the picture and the touch, after a state that was the ROM's") \
  X(slime_threw,      "slime_attack_throw", "the glob's thread started") \
  X(slime_rose,       "slime_attack_rise",  "its handler put back, and off a random way") \
  X(slime_glob_dressed,"slime_glob_dress",  "a record for the glob, where the slime is") \
  X(slime_glob_aimed, "slime_glob_aim",     "where it comes down, near whom the slime found") \
  X(slime_glob_splashed,"slime_glob_splash","the box it tells of when it lands") \
  X(slime_glob_began, "slime_glob_begin",   "the glob's thread begun: its weight, to its sound") \
  X(slime_glob_heard_landing, "slime_glob_landed", "down: to its second sound") \
  X(slime_glob_told,  "slime_glob_told",    "its last pictures' list") \
  X(slime_glob_done,  "slime_glob_done",    "back from the landing, and its end") \
  X(stepper_stepped,  "stepper_frame",      "a twentieth frame: on to the next place") \
  X(stepper_went_round,"stepper_frame",     "...which was the first again") \
  X(stepper_turned_picture,"stepper_frame", "the other picture, and its frames") \
  X(stepper_told,     "stepper_frame",      "something has set its word: the ROM's") \
  X(bolt_flew,        "bolt_frame",         "a step the way it goes") \
  X(bolt_turned_picture,"bolt_frame",       "...and a ninth frame: the other picture") \
  X(bolt_met_ground,  "bolt_frame",         "the ground is in the way, and it ends") \
  X(bolt_past_the_leash,"bolt_frame",       "too far from a player, and it ends") \
  X(bolt_ran_out,     "bolt_frame",         "sixty frames, and it ends") \
  X(bolt_told,        "bolt_frame",         "something has set its word, and it ends") \
  X(wander_content,   "wander_pick",        "the two words are the same: nothing to look for") \
  X(wander_found,     "wander_pick",        "a spot that will do") \
  X(wander_off_the_level,"wander_pick",     "a spot off the level") \
  X(wander_tile_wrong,"wander_pick",        "...or on a tile without the bit") \
  X(wander_tile_below_wrong,"wander_pick",  "...or above one") \
  X(wander_gave_up,   "wander_pick",        "three spots, and none would do") \
  /* Four threads' frames, a step and a clear -- see port/tracker.h, follower.h, */ \
  /* walker.h, carried.h, pursuer.h and clears.h. */                            \
  X(tracker_flapped,  "tracker_frame",      "a third frame: the other picture") \
  X(tracker_steered,  "tracker_frame",      "a speed takes one more towards them") \
  X(tracker_fast_enough,"tracker_frame",    "...or would be six, and stays") \
  X(tracker_flew,     "tracker_frame",      "it goes where its speeds take it") \
  X(tracker_met_ground,"tracker_frame",     "the ground is in the way, and it ends") \
  X(tracker_ran_out,  "tracker_frame",      "its frames are up, and it ends") \
  X(tracker_ended,    "tracker_frame",      "the loop is over") \
  X(tracker_began,    "tracker_begin",      "a record, a place and a speed the way it was sent") \
  X(slime_began,      "slime_begin",        "a record where it was started, facing down") \
  X(game_over_sprite_begun,"game_over_sprite_begin", "one of the game over's four sprites, above the screen") \
  X(portrait_sprites_begun,"portrait_sprites_begin", "the two sprites of the portraits' screen") \
  X(title_sprites_begun, "title_sprites_begin", "the two sprites of the title") \
  X(frontend_reset,     "frontend_reset",     "what every pass of the front end zeroes") \
  X(opening_job_over,   "opening_job",        "the screen before the title is over: the job ends") \
  X(opening_job_waited, "opening_job",        "not a fourth frame") \
  X(opening_job_moved,  "opening_job",        "its third layer a pixel down and across") \
  X(opening_wait_on,    "opening_wait",       "Start alone on neither pad") \
  X(opening_wait_ended, "opening_wait",       "...on one: the screen is over") \
  X(opening_counted,    "opening_count",      "a frame counted, and another to come") \
  X(opening_count_done, "opening_count",      "fifteen counted") \
  X(tile_job_busy,    "tile_put_job",       "the list is being added to: left queued") \
  X(tile_job_empty,   "tile_put_job",       "...or has nothing on it") \
  X(tile_job_sent,    "tile_put_job",       "...or each word sent to its place in VRAM") \
  X(layers_set_up,    "layers_setup",       "the mode, and where three layers' maps and tiles are") \
  X(layers_reset,     "layers_reset",       "...and every layer at its corner first") \
  X(hud_side_1_blanked,"hud_side_1_blank",  "the first player's side of the HUD's shadow blanked") \
  X(hud_side_2_blanked,"hud_side_2_blank",  "the second player's") \
  X(mainloop_leaving_on,"mainloop_leaving_frame", "not everyone playing has left yet") \
  X(mainloop_all_out, "mainloop_leaving_frame", "...or they have: the level is done") \
  X(mainloop_none_rescued,"mainloop_leaving_frame", "...or neither has a neighbour: the game is over") \
  X(mainloop_leaving_nobody,"mainloop_leaving_frame", "...or nobody is playing") \
  X(follower_frame_ended,"follower_frame",  "the word says it is over") \
  X(follower_moved_on,"follower_frame",     "a seventh frame: the next place") \
  X(follower_frame_looks,"follower_frame",  "a sixteenth frame: out by the sleep, a return on the stack") \
  X(follower_next_picture,"follower_frame", "a seventh frame: the next of four pictures") \
  X(follower_shown,   "follower_frame",     "an odd count: drawn again") \
  X(follower_hidden,  "follower_frame",     "...or not drawn") \
  X(follower_frame_hit,"follower_frame",    "its handler has said something touched it") \
  X(walker_saw_someone,"walker_frame",      "someone is near: at them from the next frame") \
  X(walker_alone_but_watched,"walker_frame","nobody near, but a player is") \
  X(walker_left_alone,"walker_frame",       "...and no player either: it ends") \
  X(walker_stepped,   "walker_frame",       "a pixel the way it faces") \
  X(walker_turned,    "walker_frame",       "something in the way: a quarter turn") \
  X(walker_took_the_side,"walker_frame",    "the quarter turn back is clear, and it faces it") \
  X(walker_lost_them, "walker_frame",       "going at someone who is far now: it walks again") \
  X(walker_one_step,  "walker_frame",       "at them, one step") \
  X(walker_two_steps, "walker_frame",       "...or two") \
  X(walker_went_across,"walker_frame",      "the step across was clear") \
  X(walker_went_down, "walker_frame",       "the step down was clear") \
  X(walker_faces_right,"walker_frame",      "a fifth frame: a picture as drawn") \
  X(walker_faces_left,"walker_frame",       "...or turned over") \
  X(walker_ended,     "walker_frame",       "the loop is over") \
  X(carried_on,       "carried_frame",      "carried a step further") \
  X(carried_met_ground,"carried_frame",     "the ground stops them") \
  X(carried_met_a_thing,"carried_frame",    "a thing stops them") \
  X(carried_past_the_leash,"carried_frame", "the other player is too far") \
  X(carried_off_the_level,"carried_frame",  "the level's edge stops them") \
  X(carried_ran_out,  "carried_frame",      "the count is up") \
  X(pursuer_done,     "pursuer_step",       "the count is at its end: no step") \
  X(pursuer_on_top_of_them,"pursuer_step",  "it faces nowhere: no step") \
  X(pursuer_went_across,"pursuer_step",     "the step across was clear") \
  X(pursuer_went_down,"pursuer_step",       "the step down was clear") \
  X(pursuer_ground_in_the_way,"pursuer_step","ground stops half a step") \
  X(pursuer_thing_in_the_way,"pursuer_step","a thing stops half a step") \
  X(actor_slots_cleared,"actor_slots_clear","every display record freed") \
  /* A tile changed in the map, and the swipe that changes them -- see */      \
  /* port/tile_put.h and port/swipe.h. */                                       \
  X(tile_put_over_sprites,"map_tile_put",   "a tile numbered low: drawn over the sprites") \
  X(tile_put_on_screen,"map_tile_put",      "in the camera's window: on the list for VRAM") \
  X(tile_put_off_screen,"map_tile_put",     "...or out of it: the map only") \
  X(tile_put_asked,   "map_tile_put",       "the list's first: the job is queued") \
  X(swipe_began,      "swipe_begin",        "a record in front of its owner") \
  X(swipe_as_drawn,   "swipe_begin",        "a way whose picture is as drawn") \
  X(swipe_turned_over,"swipe_begin",        "...or turned over") \
  X(swipe_tile_left,  "swipe_cut",          "a tile with neither bit: left") \
  X(swipe_cut_first,  "swipe_cut",          "a tile with bit 14: cut to $0097") \
  X(swipe_cut_second, "swipe_cut",          "...or with bit 15: cut to $01DB") \
  X(swipe_cut_nothing,"swipe_cut",          "nothing there to cut") \
  X(swipe_cut_counted,"swipe_cut",          "the cuts counted for the player, and heard") \
  X(swipe_thread_began,"swipe_thread_begin", "the thread's start: both calls, and off to be heard") \
  X(swipe_thread_waited,"swipe_thread_wait", "its owner where they were: one more tick") \
  X(swipe_thread_owner_moved,"swipe_thread_wait", "...or moved, and it ends a tick early") \
  X(vram_send_more,   "vram_send_job",      "a kilobyte sent, and more to send") \
  X(vram_send_last,   "vram_send_job",      "the last of it sent") \
  X(bg1_vscroll_job,  "bg1_vscroll_job",    "BG1's scroll down, from its shadow") \
  X(mosaic_off_job,   "mosaic_off_job",     "the mosaic off")   X(boss_shake_job,   "boss_shake_job",     "the ground under the big figure, a few lines down") \
  X(brightness_up,    "brightness_up_job",  "a step brighter, and more to go") \
  X(brightness_full,  "brightness_up_job",  "...and the fifteenth") \
  X(brightness_down,  "brightness_down_job","a step darker") \
  X(brightness_blanked,"brightness_down_job","...and past nothing: the screen blanked") \
  /* What a level leaves on the ground -- see port/objects.h. */                \
  X(object_parsed,    "object_list_parse",  "an entry of the level's list kept") \
  X(object_list_ended,"object_list_parse",  "the list's end, and the handler named") \
  X(object_spawned,   "object_spawn",       "an entry given a record") \
  X(object_freed,     "object_free",        "an entry's record taken away") \
  X(object_collected, "object_collect",     "the picked up retired, to the last") \
  X(object_collect_another,"object_collect","...and another after it") \
  X(victim_parsed,    "victim_list_parse",  "a neighbour's place kept") \
  X(victim_list_ended,"victim_list_parse",  "the list ended at its end") \
  X(victim_list_gated,"victim_list_parse",  "...or at a neighbour the level has not") \
  X(victim_tables_cleared,"victim_tables_clear","nobody started") \
  X(victim_ungated,   "victim_start",       "an entry with no gate") \
  X(victim_at_the_gate,"victim_start",      "...or the last the level has") \
  X(victim_within_the_gate,"victim_start",  "...or one before the last") \
  /* The loops the game waits in -- see port/hold.h. */                         \
  X(hold_over,        "hold_turn",          "what a loop waited for has happened") \
  X(hold_goes_on,     "hold_turn",          "...or not yet, and it goes round") \
  /* The big monster's thread -- see port/monster_thread.h. */                  \
  X(monster_goes_on,  "monster_turned",     "nothing in `$2A`: another frame") \
  X(monster_leaves,   "monster_turned",     "`$2A` positive: nobody near, and it goes quietly") \
  X(monster_killed,   "monster_turned",     "a fatal thing held, or `$2A` negative") \
  X(monster_frees_held, "monster_freed",    "it ends holding a record, which is freed too") \
  /* The big monster's states -- see port/monster_states.h. */                  \
  X(monster_no_room_ground, "monster_asks_for_room", "solid ground at the top of the screen") \
  X(monster_no_room_edge, "monster_asks_for_room", "...or that is past the level's edge") \
  X(monster_picks_up, "monster_picks_up",   "somebody caught: a record for them, by their kind") \
  X(monster_puts_down, "monster_puts_down", "a killed one puts down who it holds") \
  X(monster_walks,    "monster_walking",    "a step the way it faces") \
  X(monster_walk_refused, "monster_walking", "...refused, so it goes round") \
  X(monster_goes_round, "monster_going_round", "a step along what stopped it") \
  X(monster_turns_left, "monster_going_round", "nothing on its left any more: it turns that way") \
  X(monster_turns_right, "monster_going_round", "the step refused: a quarter turn to its right") \
  X(monster_circling, "monster_going_round", "five left turns running") \
  X(monster_wanders_off, "monster_wanders_off", "one of the four straight ways, by a draw") \
  X(monster_marches,  "monster_marching",   "a step the way it was sent") \
  X(monster_march_met_edge, "monster_marching", "...refused by the level's edge") \
  X(monster_march_waits, "monster_marching", "...or by ground with nothing to leap") \
  X(monster_nothing_leapable, "monster_marching", "no tile to leap, one ahead or two") \
  X(monster_no_landing, "monster_marching", "a tile to leap, and solid ground past it") \
  X(monster_leaps,    "monster_leaps",      "a leap begins") \
  X(monster_leaps_plain, "monster_leaps",   "...with no pictures of its own") \
  X(monster_lands_further, "monster_lands", "solid where it would land: 8 pixels on") \
  /* The big figure's thread on level 25 -- see port/boss_thread.h. */          \
  X(boss_strides,     "boss_turn",          "its stride's next picture, every ninth frame") \
  X(boss_stride_waits, "boss_turn",         "...due, but the last picture has not gone up") \
  X(boss_picture_mirrored, "boss_turn",     "a picture drawn facing the other way") \
  X(boss_flashes,     "boss_turn_ends",     "hit since the last flash: three frames of other colours") \
  X(boss_flash_over,  "boss_turn_ends",     "...and its own colours back") \
  X(boss_paces,       "boss_choose",        "nothing in mind: east or west for up to 63 frames") \
  X(boss_rampages,    "boss_choose",        "a rampage begun, from whichever state") \
  X(boss_lines_up,    "boss_choose",        "it turns to a player, to go and spit at them") \
  X(boss_goes_to_point, "boss_going",       "a point to go to, put on its own grid") \
  X(boss_stamps,      "boss_stamping",      "36 frames of stamping begun") \
  X(boss_goes_home,   "boss_home",          "north or south to the row it started on") \
  X(boss_saw_no_player, "boss_pacing",      "what answered within reach was no player") \
  X(boss_turns_away,  "boss_rampaging",     "stuck on a step, so another way by a draw") \
  X(boss_stung_rampaging, "boss_rampaging", "hit while it rampaged: it goes for the nearer player") \
  X(boss_stuck_going, "boss_going",         "stuck on the way to its point") \
  X(boss_hops,        "boss_stamping",      "somebody right under it: a point a hop away") \
  X(boss_closes_in,   "boss_stamping",      "a player 104 to 119 away: it goes to them") \
  X(boss_stung_stamping, "boss_stamping",   "hit while it stamped: a rampage") \
  X(boss_stung_going_home, "boss_home",     "hit on the way home: a rampage") \
  X(boss_got_home,    "boss_home",          "on its row, and it chooses again") \
  X(boss_home_gives_up, "boss_home",        "too long getting home with a player within 120") \
  X(boss_goes_to_player, "boss_lining_up",  "stung, or the player is too close beside it to spit at") \
  X(boss_in_place,    "boss_lining_up",     "on the spot beside the player: the spit begins") \
  X(boss_lost_player, "boss_lining_up",     "no player in sight any more: a rampage") \
  X(boss_stuck_lining_up, "boss_lining_up", "no way to the spot: a rampage") \
  X(boss_heard,       "boss_spit_begins",   "its cry, on a level that has the sound") \
  X(boss_spits_one,   "boss_spitting",      "a spit aimed, and a record asked for") \
  X(boss_spit_no_slot, "boss_spitting",     "...but all four are out") \
  X(boss_spit_out_of_reach, "boss_spitting", "no player within 128 of its mouth") \
  X(boss_spits_all_out, "boss_spitting",    "four out already, counted before it looks for one") \
  X(boss_spits_again, "boss_spit_shows",    "the bottle's pictures done, and another to come") \
  X(boss_stops_spitting, "boss_stop_spitting", "the bottle put away and its first picture back") \
  X(boss_spit_bursts, "boss_spit_moves",    "a spit's flight ends and it bursts") \
  X(boss_spit_gone,   "boss_spit_moves",    "the burst's last picture shown: its record is freed") \
  /* A record begun, and the neighbours -- see port/begin.h, neighbours.h. */   \
  X(record_begun,     "record_begin",       "a record at the page's place") \
  X(zombie_begun,     "zombie_begin",       "...and a zombie's made of it") \
  X(neighbour_begun,  "neighbour_begin",    "a neighbour's record, and their handler") \
  X(record_ended,     "record_end",         "a thread's end: its weight given back and its record freed") \
  X(kill_scored,      "kill_scored",        "a thing a player killed: what it was worth, for them") \
  X(kill_by_nobody,   "kill_scored",        "...or nobody did, and it goes on to its end") \
  X(kill_counted,     "kill_counted",       "one more of its kind killed, and its last pictures") \
  X(death_pictures_begun,"death_pictures",  "a killed thing, off to be heard") \
  X(death_pictures_heard,"death_pictures_heard", "...and no longer touchable") \
  X(zombie_leave_unkilled,"zombie_leave",   "a zombie leaving that nobody killed") \
  X(zombie_leave_killed,"zombie_leave",     "...or one a player killed, and counted") \
  X(neighbour_cycle_shown, "neighbour_cycle", "the next of its pictures") \
  X(neighbour_cycle_went_round, "neighbour_cycle", "...which was the first again") \
  X(neighbour_cycle_told, "neighbour_cycle", "something has set its word: the ROM's") \
  X(tourists_stayed, "tourists_frame", "no room, or the word not set: they stay") \
  X(tourists_turned, "tourists_frame", "room in the level, and the word set: the ROM's") \
  X(neighbour_watch_at_ease, "neighbour_watch", "nothing near: the next picture") \
  X(neighbour_watch_cried, "neighbour_watch", "one of them within a hundred: the cry") \
  X(neighbour_watch_alarmed, "neighbour_watch", "...or alarm with no cry") \
  X(neighbour_watch_told, "neighbour_watch", "something has set its word: the ROM's") \
  X(neighbour_alarm_shown, "neighbour_alarm", "the next picture of the alarm") \
  X(neighbour_alarm_over, "neighbour_alarm", "all shown: back to looking") \
  X(neighbour_alarm_told, "neighbour_alarm", "something has set its word: the ROM's") \
  X(neighbour_sign_shown,"neighbour_sign_frame","the sign's other picture") \
  X(neighbour_sign_over,"neighbour_sign_frame","...and its turns are up") \
  X(neighbour_sign_told,"neighbour_sign_frame","something has set the word: the ROM's") \
  X(neighbour_rising, "neighbour_rise_frame","up a pixel, and more to go") \
  X(neighbour_risen,  "neighbour_rise_frame","...and the twentieth") \
  /* The weeds -- see port/weeds.h. */                                          \
  X(weed_grew,        "weed_frame",         "nobody near: an arm's turn") \
  X(weed_on_guard,    "weed_frame",         "a player near the root: on its guard") \
  X(weed_stood_down,  "weed_frame",         "on its guard, and the draw said grow again") \
  X(weed_resting,     "weed_frame",         "a pass of its rest") \
  X(weed_rested,      "weed_frame",         "...and the last") \
  X(weed_step_refused,"weed_frame",         "a step a player is near, or off the level") \
  X(weed_tip_moved,   "weed_frame",         "a step onto ground it cannot take: the tip moves") \
  X(weed_planted,     "weed_frame",         "a good step, and a tile planted") \
  X(weed_marked,      "weed_frame",         "the spot marked") \
  X(weed_clump,       "weed_frame",         "room for a clump") \
  X(weed_clump_tile,  "weed_frame",         "...and a tile of it planted") \
  X(weed_seed_flew,   "weed_seed_frame",    "a pass of its arc") \
  X(weed_seed_landed, "weed_seed_frame",    "...and below the ground: landed") \
  X(weed_seed_began,  "weed_seed_begin",    "its record, and where it is to come down") \
  X(weed_seed_came_down, "weed_seed_landed", "on the ground, and the box round it made") \
  X(weed_seed_told,   "weed_seed_told",     "its last pictures' list") \
  X(weed_seed_ended,  "weed_seed_end",      "its record freed, and the thread's end") \
  /* A player knocked back, and the decoy -- see port/lunge.h, decoy.h. */      \
  X(lunge_on,         "lunge_frame",        "a step back, against the way they face") \
  X(lunge_ran_out,    "lunge_frame",        "the fifteenth frame") \
  X(lunge_met_ground, "lunge_frame",        "ground in the way") \
  X(lunge_past_the_leash,"lunge_frame",     "the other player too far behind") \
  X(lunge_off_the_level,"lunge_frame",      "the level's edge") \
  X(lunge_met_a_thing,"lunge_frame",        "a thing in the way") \
  X(decoy_stood,      "decoy_frame",        "another ten frames of it") \
  X(decoy_sounded,    "decoy_frame",        "its four pictures came round: the sound") \
  X(decoy_hit,        "decoy_frame",        "its word has gone negative: the ROM ends it") \
  X(decoy_ran_out,    "decoy_frame",        "its hundredth turn") \
  X(decoy_on_an_ending_tile,"decoy_frame",  "a tile that ends it") \
  X(colours_112_asked,"colours_112_ask",    "the sixteen colours' job queued") \
  /* Five small things -- see port/line.h, flinch.h, blinker.h, follower.h. */  \
  X(line_moved,       "line_step",          "a frame along a line") \
  X(line_stepped,     "line_step",          "...and a whole in a sum: a step that way") \
  X(flinch_next,      "flinch_frame",       "the next picture of the list, and its frames") \
  X(flinch_done,      "flinch_frame",       "...which was the last") \
  X(flinch_other_state,"flinch_frame",      "state $0C, whose pictures are further on") \
  X(flinch_begun,     "flinch_begin",       "the list for the way they face, and its first picture") \
  X(flinch_lone,      "flinch_begin",       "the pictures at $FD72: one of them, twelve frames") \
  X(hit_shrugged,     "player_hit",         "pictures in which nothing is taken") \
  X(hit_no_health,    "player_hit",         "no health to take") \
  X(hit_flinched,     "player_hit",         "one from their health, and on to the pictures") \
  X(hit_shown_the_other_way,"player_hit",   "...with the next bit set: the ROM's") \
  X(player_page_begun,"player_page_begin",  "a player's page cleared, and whose it is") \
  X(knock_busy,       "knock_look",         "a tile is being knocked down already") \
  X(knock_nothing,    "knock_look",         "the tile the fist is at is not one to knock down") \
  X(knock_begun,      "knock_look",         "...or is, and the thread that does it is asked for") \
  X(knock_thread_began, "knock_thread_begin", "a record under the place, and its sound") \
  X(knock_thread_swapping, "knock_thread_swap", "the block there, and its pair asked for") \
  X(knock_thread_swapped, "knock_thread_swapped", "the swap counted, and another may begin") \
  X(knock_thread_ended, "knock_thread_end",   "its load given back and its record freed") \
  /* $80:F366-$80:F451 small things a player's states do -- see port/player_small.h. */ \
  X(player_untouchable, "player_untouchable", "the record's collide id cleared") \
  X(player_touchable, "player_touchable",   "...and put back") \
  X(player_unhandled, "player_unhandled",   "the thread's handler taken away") \
  X(player_handled,   "player_handled",     "...and put back") \
  X(player_hands_swapped, "player_hands_swap", "what is in hand kept, and something else there") \
  X(player_hands_back, "player_hands_back", "...and what was kept in hand again") \
  X(player_spanned,   "player_span",        "how far a point is from the player, and which way") \
  X(weapon_away_kept, "weapon_away",        "state $0C: the weapon in hand stays drawn") \
  X(weapon_away_hidden, "weapon_away",      "...any other: not drawn") \
  X(fired_wait_moved, "fired_wait",         "the pad is held another way, and the wait is over") \
  X(fired_wait_done,  "fired_wait",         "...or the eighth tick is") \
  X(fired_wait_on,    "fired_wait",         "...or neither: another tick") \
  X(tile_search_resting, "tile_search",     "a player in state $06: not looked for") \
  X(tile_search_none, "tile_search",        "no tile in the list has the bit") \
  X(tile_search_found, "tile_search",       "one has, and its middle is the point") \
  X(blinker_turned,   "blinker_frame",      "the other picture, and its frames") \
  X(blinker_told,     "blinker_frame",      "...and something has set its word: the ROM's") \
  X(follower_this_side,"follower_place",    "put beside the point") \
  X(follower_other_side,"follower_place",   "...the other side of it") \
  X(follower_looks,   "follower_place",     "a sixteenth frame: the tile is looked at next") \
  X(follower_ended,   "follower_place",     "past the last place") \
  /* $82:EF4F a frame of level 37's thing -- see port/seeker.h. */              \
  X(seeker_came_on,   "seeker_frame",       "a pixel toward whoever is nearest") \
  X(seeker_backed_off,"seeker_frame",       "...or away, from too near") \
  X(seeker_circle_began,"seeker_frame",     "...or in between, and it starts to circle") \
  X(seeker_circled,   "seeker_frame",       "a place round the circle") \
  X(seeker_lapped,    "seeker_frame",       "...past the last, and round again") \
  X(seeker_lapped_back,"seeker_frame",      "...or past the first, going the other way") \
  X(seeker_circle_asks,"seeker_frame",      "a fourth frame: next it asks who is nearest") \
  X(seeker_circle_stayed,"seeker_frame",    "a lap done, and the draw says go on") \
  X(seeker_circle_off_level,"seeker_frame", "...or says stop, from a place off the level") \
  X(seeker_circle_bad_ground,"seeker_frame","...or over ground that will not do") \
  X(seeker_circle_left,"seeker_frame",      "...or from a place that will: it stops circling") \
  X(seeker_ask_same,  "seeker_frame",       "still nearest to the one it circles") \
  X(seeker_ask_other_far,"seeker_frame",    "...or someone else is, not near enough to matter") \
  X(seeker_watched,   "seeker_frame",       "still, facing whoever is nearest") \
  X(seeker_watched_b, "seeker_frame",       "...and the same from the other state") \
  X(seeker_faced_left,"seeker_frame",       "...to the left of it, so drawn as it is") \
  X(seeker_faced_right,"seeker_frame",      "...to the right, so flipped") \
  X(seeker_frame_ended,"seeker_frame",      "the loop's end") \
  X(seeker_held,      "seeker_frame",       "facing them from a third state, and set to be drawn") \
  X(seeker_blinked_on,"seeker_frame",       "at one of two places, drawn: an even frame") \
  X(seeker_blinked_off,"seeker_frame",      "...and not drawn: an odd one") \
  X(seeker_went_on,   "seeker_frame",       "watching: the draw sends it on to $82:EE0E") \
  X(seeker_dart_began,"seeker_frame",       "held: the draw says dart, and for how many hops") \
  X(seeker_dart_arrived,"seeker_frame",     "darting: near enough, so it watches again") \
  X(seeker_dart_spent,"seeker_frame",       "...or out of hops, and the same") \
  X(seeker_hop_across,"seeker_frame",       "a hop across: they are further off that way") \
  X(seeker_hop_down,  "seeker_frame",       "...or up or down") \
  X(seeker_hop_began, "seeker_frame",       "clear ground where the hop lands") \
  X(seeker_hopped,    "seeker_frame",       "the third frame of a hop over: its place moved") \
  X(seeker_place_bad_ground,"seeker_frame", "hidden: the ground at the place drawn will not do") \
  X(seeker_place_off_level,"seeker_frame",  "...or the place is off the level") \
  /* $81:F98A the thing thrown in an arc -- see port/lob.h. */                  \
  X(lob_flew,         "lob_frame",          "a frame in the air") \
  X(lob_slowed,       "lob_frame",          "a fourth frame: the rise one less") \
  X(lob_picture_low,  "lob_frame",          "a ninth frame: the next picture") \
  X(lob_picture_high, "lob_frame",          "...of the four for high up") \
  X(lob_landed,       "lob_frame",          "below the ground: the ROM's") \
  X(lob_began,        "lob_begin",          "its record, thirty pixels up") \
  X(lob_came_down,    "lob_landed",         "on the ground, and off to be heard") \
  X(lob_burst,        "lob_burst",          "everything within forty pixels told") \
  X(lob_burst_again,  "lob_burst",          "...and told again a tick later") \
  X(lob_ended,        "lob_end",            "its weight given back") \
  /* $81:EC03, $81:EC30 -- weapon 5's shot. */                                    \
  X(shot5_began,      "shot5_begin",        "its record, in front of whoever fired it") \
  X(shot5_aimed,      "shot5_aim",          "...at the weapon's mouth, with the picture for its way") \
  X(shot5_flew,       "shot5_frame",        "a frame over a tile without bit 1") \
  X(shot5_flew_over,  "shot5_frame",        "...or over one with it and none of the three: one of sixty") \
  X(shot5_spent,      "shot5_frame",        "...the sixtieth, and it is gone") \
  X(shot5_struck,     "shot5_frame",        "a tile with bit 1 and one of the three: it stops") \
  X(shot5_blocked,    "shot5_frame",        "carry from the test: it stops, the tile left alone") \
  X(shot5_burst,      "shot5_burst",        "its record where it bursts") \
  X(shot5_broke,      "shot5_break",        "the block it struck, and its pair asked for") \
  /* $80:F300 a pose's picture, called on its own -- see port/pose.h. */        \
  X(pose_show_set,    "pose_show",          "the entry's word set in the record's flags") \
  X(pose_show_masked, "pose_show",          "...or a mask, and its bits cleared") \
  /* $80:A4D9 a whole screen of tiles -- see port/screen_tiles.h. */            \
  X(screen_tiles_fill, "screen_tiles_fill", "31 rows of 32 tiles and a column of 32 copied to a buffer") \
  /* $82:B84A, $82:B8FB the text printer -- see port/text.h. */                 \
  X(text_print,       "text_print",         "a string at one place") \
  X(text_print_lines, "text_print_lines",   "strings each with its own place") \
  X(text_new_place,   "text_print_lines",   "a byte of $FF: a new place, and the string goes on") \
  X(text_char_drawn,  "text_print",         "a character's four tiles written") \
  X(text_char_skipped, "text_print",        "a byte under $2F, which draws nothing") \
  /* A level's loads -- see port/loads.h. */                                    \
  X(tile_attrs_load,  "tile_attrs_load",    "a kilobyte of tile attributes copied") \
  X(palette_load,     "palette_load",       "the background's colours copied, twice") \
  X(palette_sprites_load, "palette_sprites_load", "the sprites' colours copied") \
  X(hud_reset,        "hud_reset",          "the HUD's shadow emptied") \
  /* $80:C34A, $82:8308, $80:9F62 three more jobs -- see port/dma.h. */        \
  X(hud_upload_job,   "hud_upload_job",     "the HUD's shadow sent to VRAM") \
  X(text_map_job,     "text_map_job",       "a screen's printed words sent to VRAM") \
  X(colours_112_job,  "colours_112_job",    "colours 112 to 127 sent") \
  X(vram_clear_more,  "vram_clear_job",     "a kilobyte zeroed, and more to do") \
  X(vram_clear_done,  "vram_clear_job",     "...or the last, at the top of VRAM") \
  X(vram_wiped,       "vram_wipe",          "three quarters of VRAM zeroed at once") \
  X(colours_job,      "colours_job",        "the level's colours sent, in two transfers") \
  /* $81:F380 the bubble gun's bubble -- see port/bubble.h. */                  \
  X(bubble_unfired,   "bubble_launch",      "fired from a tile that stops it: no bubble") \
  X(bubble_launched,  "bubble_launch",      "two of the budget, and on to ask for a record") \
  X(bubble_of_player, "bubble_dress",       "a player's: that side's muzzles and id") \
  X(bubble_of_other,  "bubble_dress",       "anyone else's: the first player's muzzles, and id $0B") \
  X(bubble_first_rose, "bubble_first",      "four frames as water to come") \
  X(bubble_first_stopped, "bubble_first",   "...or the muzzle is in a tile that stops it") \
  X(bubble_rose,      "bubble_rising",      "a step as water") \
  X(bubble_rising_stopped, "bubble_rising", "...into a tile that stops it") \
  X(bubble_formed,    "bubble_rising",      "the fourth step: a bubble, with a handler") \
  X(bubble_flew,      "bubble_flying",      "a step as a bubble") \
  X(bubble_stopped,   "bubble_flying",      "...into a tile that stops it") \
  X(bubble_spent,     "bubble_flying",      "...or its thirtieth") \
  X(bubble_hit,       "bubble_flying",      "it hit something: gone with no burst") \
  X(bubble_gone,      "bubble_gone",        "the burst shown: the budget back, and its record") \
  /* $81:B4EA the axe a doll throws -- see port/axe.h. */                       \
  X(axe_launched,     "axe_launch",         "two more of the budget, and on to ask for a record") \
  X(axe_dressed,      "axe_dress",          "the record given its place, picture, step and handler") \
  X(axe_goes_across,  "axe_dress",          "it has a step across, which adds to the set of pictures") \
  X(axe_goes_down,    "axe_dress",          "...and one down") \
  X(axe_turned,       "axe_frame",          "the turn's wait ran out: the next of four pictures") \
  X(axe_turned_round, "axe_frame",          "...and after the fourth, the first") \
  X(axe_flew,         "axe_frame",          "a step, with no tile in the way and inside the leash") \
  X(axe_stopped_ground, "axe_frame",        "a tile with bit 2, or the edge of the level") \
  X(axe_stopped_leash, "axe_frame",         "too far from the first player") \
  X(axe_shown_masked, "axe_frame",          "a frame whose mask is ANDed into the record's flags") \
  X(axe_shown_ored,   "axe_frame",          "...or ORed in") \
  X(axe_ended_stopped, "axe_frame",         "leaving because it was stopped") \
  X(axe_ended_hit,    "axe_frame",          "...or because it hit a second thing") \
  /* $81:B2B0 the doll's thread round its loop -- see port/doll.h. */          \
  X(doll_launched,    "doll_launch",        "24 of the budget, and on to ask for a record") \
  X(doll_dressed,     "doll_dress",         "both records and the page set up") \
  X(doll_arrived_unseen, "doll_dress",      "off the screen: no sound") \
  X(doll_arrived_unheard, "doll_dress",     "on it, but $1F52 is not 3: no sound") \
  X(doll_arrived_heard, "doll_dress",       "on it, and heard") \
  X(doll_opening,     "doll_frame",         "a frame in the opening state: on to play its pictures") \
  X(doll_opened,      "doll_opened",        "the pictures played: the leap out set up") \
  X(doll_destroyed,   "doll_end",           "leaving because it was hit: on to the score") \
  X(doll_left_quietly, "doll_end",          "...or because nobody was in reach") \
  X(doll_burst_plain, "doll_burst",         "176 times in 256 it leaves nothing") \
  X(doll_burst_flame, "doll_burst",         "...and 80 times the thread at $81:B664") \
  X(doll_gone,        "doll_gone",          "the budget back, and on to free the axe's record") \
  /* $81:B664 what a destroyed doll can leave -- see port/flame.h. */           \
  X(flame_launched,   "flame_launch",       "24 of the budget, and on to ask for a record") \
  X(flame_dressed,    "flame_dress",        "the record, the page and the handler") \
  X(flame_began,      "flame_begin",        "its first point to wander to") \
  X(flame_wandered_off, "flame_frame",      "a new point picked, 8 to 24 pixels off on each axis") \
  X(flame_goes_across, "flame_frame",       "the leg across is the longer, or as long: it is kept") \
  X(flame_goes_down,  "flame_frame",        "...or the leg down is") \
  X(flame_goes_nowhere, "flame_frame",      "both legs nothing, which only a chase's can be") \
  X(flame_wander_rested, "flame_frame",     "an even frame of the game: a wander does nothing") \
  X(flame_wander_stepped, "flame_frame",    "a pixel along the leg") \
  X(flame_wander_blocked, "flame_frame",    "...or solid ground there, and none") \
  X(flame_gave_up,    "flame_frame",        "no player within $D0: it ends") \
  X(flame_chase_began, "flame_frame",       "something within $30: after it") \
  X(flame_chase_stepped, "flame_frame",     "a pixel towards the target") \
  X(flame_chase_blocked, "flame_frame",     "...or solid ground there") \
  X(flame_chase_held_off, "flame_frame",    "...or the step would end within 9 pixels of it") \
  X(flame_turned,     "flame_frame",        "the picture's wait ran out: the next of four") \
  X(flame_shown_masked, "flame_frame",      "a frame whose mask is ANDed into the record's flags") \
  X(flame_shown_ored, "flame_frame",        "...or ORed in") \
  X(flame_trailed,    "flame_frame",        "a picture left behind it, in a free place") \
  X(flame_trail_full, "flame_frame",        "...or both places in use, and none") \
  X(flame_trail_changed, "flame_frame",     "a trail's picture changed, on its fourth frame") \
  X(flame_trail_gone, "flame_frame",        "a trail's 24 frames out, and its record given back") \
  X(flame_burned_out, "flame_frame",        "its 800 frames out: it ends") \
  X(flame_stayed,     "flame_frame",        "on to the next frame") \
  X(flame_trail_run_out, "flame_frame",     "leaving with a trail still showing: another frame off it at once") \
  X(flame_destroyed,  "flame_frame",        "leaving because it was hit: on to the score") \
  X(flame_left,       "flame_frame",        "...or for any other reason: the budget back, and its record") \
  X(flame_scored,     "flame_scored",       "one more destroyed, the budget back, and its record") \
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
  X(player_out_show,  "player_out_show",   "a player with none left: the list of pictures, once more") \
  X(walk_twice,       "player_walk",       "$54 bit 15: the ROM walks twice") \
  X(walk_solid,       "player_walk",       "solid ground where the step lands") \
  X(walk_reaction,    "player_walk",       "...with a reaction of its own, so the ROM walks") \
  X(walk_door_no_reach, "player_walk",     "a door's tile, and no tile to look at the way the player faces") \
  X(walk_door_not_there, "player_walk",    "...or one to look at, which is not a door") \
  X(walk_door_locked, "player_walk",       "...or a door, and nothing to open it: a thread begun") \
  X(walk_door_opened, "player_walk",       "...or a door and something to open it, so the ROM walks") \
  X(walk_square_known, "player_walk",      "the fifth reaction: a square it has already") \
  X(walk_square_begun, "player_walk",      "...or a new one, and a thread begun for it") \
  X(walk_square_full, "player_walk",       "...or a new one, and five others in the way") \
  X(walk_tethered,    "player_walk",       "too far from the other player to step there") \
  X(walk_obstructed,  "player_walk",       "someone standing where the step lands") \
  X(walk_overlapping, "player_walk",       "...and where the player stands too, so the step goes ahead") \
  X(walk_off_map,     "player_walk",       "a step off the map") \
  X(monster_walked,   "monster_walk",      "the potion's monster walked") \
  X(monster_walk_obstructed, "monster_walk", "someone standing where its step lands") \
  X(monster_walk_broke_wall, "monster_walk", "a wall that breaks under it, so the ROM walks") \
  X(stuck_walked,     "stuck_walk",        "a player stuck in slime walked") \
  X(stuck_walk_solid, "stuck_walk",        "solid ground it cannot cross") \
  X(stuck_walk_crossed, "stuck_walk",      "solid ground it can") \
  X(stuck_walk_obstructed, "stuck_walk",   "someone standing where its step lands") \
  X(swim_walked,      "swim_walk",         "a player in water swam") \
  X(swim_not_water,   "swim_walk",         "no water where the step lands") \
  X(swim_bank_solid,  "swim_walk",         "...and the bank the way they face is solid") \
  X(swim_reached_bank, "swim_walk",        "...or clear, so the ROM ends the swim") \
  X(swim_tethered,    "swim_walk",         "too far from the other player to swim there") \
  X(swim_off_map,     "swim_walk",         "a stroke off the map") \
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
  /* $80:CC13, $80:CC27 -- a command with its argument built in. */               \
  X(apu_fixed_1,      "apu_fixed_command",   "sound effect $32, by command 1") \
  X(apu_fixed_2,      "apu_fixed_command",   "...and $32 by command 2") \
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
  X(chase_leapt,      "monster_chase",       "...or something to leap, so it leaps") \
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
  X(zombie_spawned,   "zombie_*_spawn",      "charged to the level's load, and a record made a zombie's") \
  X(zombie_risen,     "zombie_*_risen",      "up out of the ground: touchable, off a random way, and asleep") \
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
  X(pose_band_b_fired, "pose_walk_band_b", "the step that starts the cycle again: a shot") \
  X(pose_band_b_stepped, "pose_walk_band_b", "...and any other step") \
  X(pose_6c_ended, "pose_walk_6c", "the fourth picture shown: the pose starts again") \
  X(pose_punched, "pose_walk_6c", "a picture of the swing: those in the fist's box told") \
  X(pose_reached, "pose_walk_6c", "...and the tile the fist is at looked at") \
  X(pose_reach_busy, "pose_walk_6c", "...or not, with a wall already coming down") \
  X(pose_swung, "pose_walk_6c", "the swing's first picture, which makes a sound") \
  X(pose_fired, "pose_*", "a round taken and the shot's thread started") \
  X(pose_fire_empty, "pose_*", "...or no rounds left, and nothing happens") \
  X(pose_fire_recoiled, "pose_fire", "...weapon 5's shot, which goes on to a pose of its own") \
  X(pose_fire_waited, "pose_fire", "asked on its own, with the last shot's delay still running") \
  X(pose_recoil_began, "pose_recoil_begin", "weapon 5's kick: the picture, and the step back set") \
  X(pose_band_b_aimed, "pose_band_b_aim", "a second-band weapon raised, standing") \
  X(pose_band_b_shot, "pose_band_b_shoot", "...and its shot") \
  X(pose_band_b_lowered, "pose_band_b_lower", "...and the picture after it") \
  X(pose_weapon_shown, "pose_*", "the hand weapon shown, the way the player faces") \
  X(pose_arc_ready, "pose_arc_ready", "the picture before a leap, its time up") \
  X(pose_arc_flew, "pose_arc", "a frame through the air") \
  X(pose_arc_fell, "pose_arc", "...high up and on the way down") \
  X(pose_bounce_rose, "pose_bounce", "up from a trampoline") \
  X(pose_bounce_fell, "pose_bounce", "...and down onto it again") \
  X(pose_bounce_waited, "pose_bounce_wait", "on it, between two bounces") \
  X(pose_bounced_off, "pose_bounce_off", "off it, across the ground") \
  X(pose_swim_waited, "pose_swim", "a frame of a picture's ten") \
  X(pose_swim_stroked, "pose_swim", "the next picture of the stroke") \
  X(pose_swim_wrapped, "pose_swim", "...which is the first again") \
  X(pose_swim_still, "pose_swim", "facing nowhere: no movement") \
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
  X(slime_attacked, "slime_frame", "...or begun: its handler taken away, and off to its pictures") \
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
  X(martian_fired, "martian_frame", "...or with nothing to wait for, arriving: the ROM's, so the pass was declined") \
  X(martian_walker_fired, "martian_frame", "...or walking: it fired, and the frame ends at the shot's spawn") \
  X(martian_woke, "martian_wake", "the rest of a walking pass that fired") \
  X(martian_began, "martian_begin", "a martian's record and its page set up") \
  X(martian_shot_begun, "martian_shoot", "the picture it fires in, and the shot's thread asked for") \
  X(martian_shot_cooling, "martian_shoot", "called while it cools: one counted off") \
  X(martian_shown, "martian_show", "the walking picture, called by the ROM") \
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
  X(stuck_freed, "stuck", "the countdown over: free, and their hands as they were") \
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
  X(chainsaw_swing_began, "chainsaw_frame", "a pass in the swing's state: the saw's record, and the first picture") \
  /* $81:9598 chainsaw_swing_next -- where a swing wakes. */              \
  X(chainsaw_swing_turned, "chainsaw_swing_next", "the next picture of the turn") \
  X(chainsaw_swing_wrapped, "chainsaw_swing_next", "...past the table's first way, so on from its last") \
  X(chainsaw_swing_ended, "chainsaw_swing_next", "the turn made: the saw's record freed, and a chase") \
                                                                                   \
  /* $81:E4B2, $81:E558 fishman_frame -- the fishman's states.      */             \
  X(fishman_swam, "fishman_frame", "a pass the way it faces") \
  X(fishman_swam_turned, "fishman_frame", "...and one turning at every opening") \
  X(fishman_turned_at_wall, "fishman_frame", "it could not swim on: a quarter turn") \
  X(fishman_turned_at_opening, "fishman_frame", "it could swim a quarter turn round, so it turned") \
  X(fishman_closed_in, "fishman_frame", "somebody within 128: it closes in") \
  X(fishman_looked_about, "fishman_frame", "the draw said stop and look about, which is the ROM's") \
  X(fishman_leapt, "fishman_frame", "it is to leap, and sleeps a few ticks first") \
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
  X(fishman_stalked, "fishman_frame", "a pass on land, at whoever is nearest") \
  X(fishman_walk_stopped, "fishman_frame", "...stopped by the ground one way, so it looks for water past it") \
  X(fishman_found_water, "fishman_frame", "...and finds it, which is the dive and the ROM's") \
  X(fishman_stalk_found_water, "fishman_frame", "on land with water to go back to: it sleeps a few ticks first") \
  X(fishman_stalk_sweeps, "fishman_frame", "...somebody within 28 and to one side: it sweeps again") \
  X(fishman_stalk_beside, "fishman_frame", "...or under 8 across: a step all the same") \
  X(fishman_stalk_left, "fishman_frame", "...nobody within 175 and no player near: it leaves") \
  X(fishman_stalk_goes_about, "fishman_frame", "...or a player is: it goes about as the other one does") \
  X(fishman_sweep_turned, "fishman_frame", "landed: it turns to them, and stops at three pictures") \
  X(fishman_dive_began, "fishman_frame", "off the ground, and the first pass of the dive") \
  X(fishman_splashed, "fishman_frame", "back in the water: it stops at two pictures") \
  X(fishman_leap_too_short, "fishman_leap_wake", "the spot is under 32 across: no leap after all") \
  X(fishman_leap_began, "fishman_leap_wake", "the leap set up, and a splash left behind it") \
  X(fishman_dive_straight, "fishman_dive_wake", "the water is straight above or below: no dive") \
  X(fishman_dive_set, "fishman_dive_wake", "the dive set up, from its next pass") \
  X(fishman_sweep_began, "fishman_sweep_begin", "its second record, at the first place in the ring") \
  X(fishman_wants_water, "fishman_sweep_after", "the sweep over: the one that keeps to its pool wants water") \
  X(fishman_swept_ashore, "fishman_sweep_after", "...and the other walks on") \
  X(fishman_swims_again, "fishman_splash_after", "in the water again, some way by a draw") \
  X(fishman_splash_began, "fishman_splash_begin", "the splash a leap leaves: its record, to its pictures") \
  X(fishman_left, "fishman_frame", "neither player within 208: it leaves") \
  X(sched_tables_cleared, "sched_tables_clear", "the threads' tables and the vblank queues cleared") \
  X(top_scores_defaulted, "top_scores_default", "the cartridge's top scores copied in") \
  X(fishman_landing_good, "fishman_landing", "all six tiles are somewhere to land") \
  X(fishman_landing_bad, "fishman_landing", "one of them is not") \
  X(fishman_begin_no_room, "fishman_patrol_begin", "the level has no room for it") \
  X(fishman_begin_not_water, "fishman_patrol_begin", "it was not put on deep water") \
  X(fishman_begun, "fishman_patrol_begin", "it begins, to its first sleep") \
  X(fishman_ended, "fishman_patrol_end", "one that left of itself: its record given back") \
  X(fishman_left_unshown, "fishman_frame", "...found when it came to show itself") \
  X(fishman_swept, "fishman_sweep_tick", "its second record at the next place in the ring") \
  X(fishman_sweep_over, "fishman_sweep_tick", "...or the five ticks are up, and the record is freed") \
                                                                                   \
  /* $81:AC1E werewolf_frame -- the werewolf's three states. */                    \
  X(werewolf_ran, "werewolf_frame", "a pass running") \
  X(werewolf_left, "werewolf_frame", "its target exactly 360 away: it leaves") \
  X(werewolf_on_them, "werewolf_frame", "on the same spot as its target: no step") \
  X(werewolf_hopped_hurt, "werewolf_frame", "hurt since it last looked: it chooses somewhere near to hop to") \
  X(werewolf_pounce_too_far, "werewolf_frame", "the draw said pounce, with its target 325 or more away") \
  X(werewolf_pounce_too_near, "werewolf_frame", "...or under 70 by the sum of the gaps") \
  X(werewolf_pounced, "werewolf_frame", "...or placed for it: it chooses where to come down") \
  X(werewolf_spot_nineteen, "werewolf_frame", "the spot is exactly 19 across from it, which is never taken") \
  X(werewolf_spot_no_ground, "werewolf_frame", "...or is not all ground to land on") \
  X(werewolf_spot_taken, "werewolf_frame", "...or has somebody on it") \
  X(werewolf_spot_off_level, "werewolf_frame", "...or is off the level") \
  X(werewolf_crouched, "werewolf_frame", "...or is good, and it crouches") \
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
  X(footballer_shown, "footballer_show", "its picture, called by the ROM") \
  X(footballer_from_left, "footballer_enter", "one coming on from the left of the screen") \
  X(footballer_from_right, "footballer_enter", "...or from the right") \
  X(footballer_began, "footballer_begin", "its record, and its page cleared") \
  X(scores_line,      "scores_line",        "a line of the top scores to print") \
  X(scores_printed,   "scores_line",        "...and all ten printed") \
  X(footballer_sent_off, "footballer_frame", "a pass sent off") \
  X(footballer_off_turned, "footballer_frame", "...stopped by the ground: it turns") \
  X(footballer_ran_off, "footballer_frame", "...and its picture is off the screen: it ends") \
                                                                                   \
  /* $80:C872, $80:C8B8, $80:A084, $80:A09E, $82:D88C -- DMA and its jobs. */       \
  X(dma_to_cgram, "dma_to_cgram", "colours sent") \
  X(dma_to_vram, "dma_to_vram", "bytes sent to VRAM") \
  X(dma_to_cgram_at, "dma_to_cgram_at", "bytes of colours sent, from a colour the caller names") \
  X(hud_tiles_job, "hud_tiles_job", "the third layer's tiles sent to VRAM") \
  X(hud_layer_set, "hud_layer_set", "where the third layer's map and tiles are") \
  X(send_args, "send_args", "what a screen's next send is called with") \
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
  /* $82:B1F9, $80:9A1B -- two more, and $80:8A00's, the game over. */             \
  X(backdrop_slide_waited, "backdrop_slide_job", "not a fourth frame") \
  X(backdrop_slide_moved, "backdrop_slide_job", "the third layer moved a pixel and was sent") \
  X(portrait_scroll_waited, "portrait_scroll_job", "the first layer alone") \
  X(portrait_scroll_fifth, "portrait_scroll_job", "a fifth frame: the third layer too") \
  X(game_over_scroll_job, "game_over_scroll_job", "the third layer's height sent") \
  X(game_over_colours_job, "game_over_colours_job", "three colours sent") \
  X(game_over_fell_one, "game_over_fall", "four sprites a pixel down") \
  X(game_over_fell_two, "game_over_fall", "an odd frame: two pixels") \
                                                                                   \
  /* $81:810F -- a frame of the level's spawn list. */                             \
  X(spawnlist_held, "spawnlist_frame", "no room on the board") \
  X(spawnlist_rested, "spawnlist_frame", "the place was resting, and counted down") \
  X(spawnlist_nearer, "spawnlist_frame", "a ready place, the nearest so far") \
  X(spawnlist_farther, "spawnlist_frame", "...or no nearer than the nearest") \
  X(spawnlist_none_near, "spawnlist_frame", "the end of the list, with nothing within 256") \
  X(spawnlist_started, "spawnlist_frame", "...or the nearest set resting, to be started") \
  X(spawnlist_began, "spawnlist_begin", "the thread's start: every place ready, the list's bank installed") \
  X(react_flash_on, "enemy_flash_begin", "a survivor's thread woken to flash") \
  X(react_flash_off, "enemy_flash_end", "...and the flash over, two ticks on") \
  X(clone_began, "clone_begin", "a clone's start, as far as its sound") \
  X(clone_grew_first, "clone_grow_first", "the first picture of the growing") \
  X(clone_began_drawn, "clone_begin", "...with both players in the game, so whose double is a draw") \
  X(clone_grew, "clone_grow", "the next picture of the growing") \
  X(clone_grown, "clone_grow", "...or the last shown, and it can be hit") \
                                                                                   \
  /* $80:CDFE -- a frame of a player. */                                           \
  X(player_frame_normal, "player_frame", "the ordinary state, which reads the pad") \
  X(player_frame_stuck, "player_frame", "...or stuck in slime") \
  X(player_frame_turning, "player_frame", "...or one of the two the pad only turns the player in") \
  X(player_monster_state_alone, "player_monster_state", "the monster's state, in a frame that is the ROM's") \
  X(player_frame_monster, "player_frame", "...or the potion's monster") \
  X(player_frame_flashing_normal, "player_frame", "...or flashing, over the ordinary state") \
  X(player_frame_flashing_turning, "player_frame", "......or over the turning one") \
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
