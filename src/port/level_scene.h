// A level's scene begun: three of the things `$80:8632` calls before a level
// is loaded, and two that a game's thread calls before that.
//
//   $80:A64F  camera_reset        the camera at the map's corner, and its jobs
//   $82:F577  walk_squares_clear  the walk's five squares forgotten
//   $80:8803  players_begin       a thread for each player in the game
//   $80:8618  players_seed        what each player starts a game with
//   $80:885B  level_record_find   where the level's record is
//
// ## The camera
//
// `camera_reset` is two stretches, with `camera_window_update` called
// between them.
//
// The first, as far as the `JSR` at `$80:A673`: the camera and its fractions
// are zero, and so are the two cursors into the tilemap. The arena the
// tilemap's rows are cut from is whole again, `$0900` bytes from `$4B28`.
// Nothing waits to go to VRAM, and the render flags are clear.
//
// The second, from `$80:A676`: `vram_queue_flush` goes on the first vblank
// queue, and the job at `$80:A937` on the second. That add is jumped to, so
// its `RTL` is the routine's.
//
// ## The walk's squares
//
// `port/walk.h` has what the five words at `$7E:572C` are for. Each is `$FFFF`
// while it is free.
//
// ## The players
//
// A player is in the game while their half of the status panel is on. For
// each, in turn, the page is told where they start, from the level's record,
// and which player they are, twice over. Then a thread is begun at `$80:CDF4`
// with `thread_spawn_low`, which copies those words to the new page.
//
// ## What a player starts with
//
// `players_seed` gives each player in the game ten of health, two lives to
// lose, the first weapon and the eighth item chosen, 150 shots of the first
// weapon and one first-aid kit. The first player's turn gives the second
// their shots and their kit as well, whether or not the second is playing.
// Then the two words at `$7E:6054` are 4. I have not found what reads those.
//
// ## The level's record
//
// `level_record_find` looks the level up in the table of records at
// `$9F:8002` and keeps the record's address on the caller's page, at `$10`.
// The level is `$1E7C`. But with `$1F50` set it clears that, counts `$1E7C`
// down one, and looks up the word that was in `$1F50`: A still holds it.
// No session I have run sets `$1F50`.
//
// Port code: libc only.

#ifndef PORT_LEVEL_SCENE_H
#define PORT_LEVEL_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define CAMERA_RESET_PC 0x80a64fu
#define CAMERA_RESET_WINDOW_PC 0x80a673u  // `JSR camera_window_update`
#define CAMERA_RESET_JOBS_PC 0x80a676u
#define WALK_SQUARES_CLEAR_PC 0x82f577u
#define WALK_SQUARES_CLEAR_RTL_PC 0x82f592u
#define PLAYERS_BEGIN_PC 0x808803u
#define PLAYERS_BEGIN_RTL_PC 0x808848u
#define PLAYERS_SEED_PC 0x808618u
#define PLAYERS_SEED_RTL_PC 0x808631u
#define LEVEL_RECORD_FIND_PC 0x80885bu
#define LEVEL_RECORD_FIND_RTL_PC 0x808873u

#define TILEMAP_ARENA_AT 0x4b28u
#define TILEMAP_ARENA_BYTES 0x0900u
#define VRAM_QUEUE_FLUSH_JOB 0x9e7bu
#define CAMERA_SECOND_JOB 0xa937u
#define CAMERA_JOBS_BANK 0x0080u

// The level's record: where each player starts, four bytes a player.
#define LEVEL_RECORDS_BANK 0x9f0000u
#define LEVEL_PLAYER_START_X 0x2a
#define LEVEL_PLAYER_START_Y 0x2c
#define PLAYERS_DP_LEVEL 0x10  // the level's record, on the caller's page
#define PLAYER_THREAD 0xcdf4u
#define PLAYER_THREAD_BANK 0x0080u

// The first stretch of `camera_reset`.
void camera_reset(Wram* w);

// The second. Each queue's answer is the slot the job took, or -1 with the
// queue full.
typedef struct {
  int flush_slot;
  int second_slot;
} CameraJobs;
void camera_jobs_queue(Wram* w, CameraJobs* did);

void walk_squares_clear(Wram* w);

// What `players_begin` did for each player: whether they are in the game,
// and the slot their thread took, or -1 with none free.
typedef struct {
  bool playing[2];
  int slot[2];
} PlayersBegun;

// The level's record has to be in the cartridge.
bool players_begin_supported(const Wram* w, uint16_t page);
void players_begin(Wram* w, const Rom* rom, uint16_t page, PlayersBegun* did);

// A game's first health, lives, item, shots and kits.
#define PLAYER_START_HEALTH 10
#define PLAYER_START_LIVES 2
#define PLAYER_START_ITEM 7
#define PLAYER_START_SHOTS 0x0150u  // BCD
#define W_PLAYER_KITS 0x1d1au       // a player's first-aid kits, `$20` apart
#define W_SEEDED_FOURS 0x6054u

// `seeded[player]` is whether that player was in the game.
void players_seed(Wram* w, bool seeded[2]);

#define W_LEVEL_NUMBER 0x1e7cu
#define W_LEVEL_AGAIN 0x1f50u
#define LEVEL_RECORD_TABLE 0x9f8002u

// The table is read at twice the level, and has to be in the cartridge.
bool level_record_find_supported(const Wram* w);

// The address kept, the doubled level in `*index`, and in `*again` whether
// `$1F50` was set.
uint16_t level_record_find(Wram* w, const Rom* rom, uint16_t page,
                           uint16_t* index, bool* again);

#endif  // PORT_LEVEL_SCENE_H
