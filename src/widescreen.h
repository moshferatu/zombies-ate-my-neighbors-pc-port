// What this game, specifically, wants done with a widened picture.
//
// The PPU knows how to draw margins and what the treatments of one are
// (`ppu_wideStretch`, `ppu_wideAnchor`, `ppu_wideClampEdge`, `ppu_wideClip`);
// it does not know which layer of *Zombies Ate My Neighbors* deserves which,
// or where this game's world stops. Those are facts about the game, and this
// header is the only place they are written down.
//
// There are seven of them.
//
// ## The status panel belongs on the edges
//
// In a level BG3 carries both players' panels -- health, weapon, item, score --
// laid out as player 1 in the left sixteen tilemap columns and player 2 in the
// right sixteen (`src/port/hud.h`). That is already two half-width halves, so
// widening should move them apart rather than stretch or repeat them, which is
// `ppu_wideAnchor` and needs no change to the shadow tilemap the co-simulation
// checks byte for byte.
//
// The same layer carries the title, the password screen and the story cards,
// and those must not be torn in half and flung at the edges. What tells the two
// apart is the layer next door: a level scrolls, so `$80:9E5C` sets BG2's
// tilemap to 64 tiles across, and a fixed screen does not. No symbol, no WRAM
// read, and no list of screens to fall out of date as more of the game is
// ported.
//
// **A tilemap's width outlives the screen that asked for it**, though, and that
// is half the test rather than all of it. `$80:9E5C` runs when a level loads;
// nothing puts BG2SC back to 32 when the level ends, so the card that tallies a
// finished level and the card that names the next one are drawn with a 64-column
// BG2 still configured behind them, and asking only about the width calls them
// levels. They were: every fixed screen up to the first level came out centred
// and every card after one was jammed against the left edge of the picture,
// because a fixed screen leaves `$1B6A` at zero, and at camera x zero the
// sliding margins below hand the left margin's whole share to the right one.
//
// The other half is that a fixed screen also switches BG2 *off the main screen*
// (`$212C`), and a level leaves it on for as long as the level lasts -- through
// the map screen, through a boss, through both players, through a bonus room.
// Measured over all 49 movies in the corpus: BG2 is wide-but-unshown in exactly
// the two that finish a level (`level21-exit`, one crossing; `level24-carry`,
// three) and in neither of them for a frame that is not a card. So the question
// is asked of both registers, and the answer is about the picture being drawn
// rather than about a register left over from the last one.
//
// ## The world only reaches so far, and the game only draws 256 of it
//
// BG2's tilemap in VRAM is a 64-column ring and the camera shows 32 of it, but
// the streamer only ever *maintains* those 32: it writes one fresh column at
// the leading edge each time the camera crosses an eight-pixel boundary and
// never touches the rest. A ring slot therefore holds the right map column only
// where the camera has already been -- correct behind it, stale ahead of it --
// which is why a level that scrolls steadily one way showed a band of leftover
// tiles at its leading edge and a level whose camera wanders did not.
//
// Nothing about that is fixable by looking at it harder: those columns have
// never been written. So they get written here, from the same map, by the same
// rule the ROM's own column copy uses (`tilemap_copy_column` in
// `src/port/camera.c`) -- and into ring slots the game does not read, does not
// write, and will overwrite with exactly these values if the camera ever
// carries them into view. That is what makes this safe to do from outside: it
// is not a change to the game's tilemap, it is the rest of the tilemap.
//
// Where the margin runs off the end of the map there is no column to copy, and
// the answer is not to black it out but to stop insisting the two margins be
// equal — see the sliding margins below. `ppu_setWideClamp` is only the
// backstop for a map narrower than the whole picture.
//
// ## Sprites are world things in a level and stagecraft outside one
//
// The card that names a level slides its letters in from beyond the console's
// 256 and trusts that edge to hide them until they arrive. Widen the picture
// and the trick shows: the same words a second time at both edges, which reads
// exactly like a tilemap being wrapped and is nothing of the kind — that screen
// has one background layer enabled and its tilemap is a single repeated tile.
// So objects get `ppu_wideClip` outside a level and `ppu_wideStretch` inside
// one, on the same test as the other two.
//
// (The card that announces how much worse this level is than the last one does
// the same thing with a background instead, and that one is not settled here:
// `ppu_wideAuto` clips it, because it is a 256-pixel tilemap the game never
// scrolls sideways. See the note on the enum in `ppu.h`.)
//
// ## ...and in a level, the game throws away the ones in the margins
//
// This is the one that cannot be fixed by choosing a policy, because by the
// time the picture is drawn the sprites are already gone.
//
// The cull the ROM runs first (`actor_cull`, `$80:BCE2`) is generous: it keeps
// every record within 128 pixels behind the camera and 383 ahead of it, which
// is far more than a 43-pixel margin needs, and it is not the problem. (It is
// not always enough for 21:9's: see the section on it below.) The problem is
// the last thing that happens to a record. `sprite_emit`
// (`$80:BA51` and its three flipped twins) works out where each 16x16 piece
// lands and then does this:
//
//     CMP #$0100 : BCC keep      ; on screen
//     CMP #$FFF1 : BCC drop      ; ...or off it, and there is no third case
//
// A piece whose screen X is 256 or more, or 16 or more to the left of zero, is
// dropped on the floor. Not parked, not clipped — never written to OAM at all,
// because the console cannot show it and OAM is 128 entries and the game has
// better uses for them. That is exactly the band widening turns into picture,
// and it is why a zombie vanishes a body's width before the edge of a
// widescreen frame while walking about quite happily in the game's own memory.
//
// So `ws_margin_sprites` puts them back. It reads the same visible list the
// game's own pass read, the same records, the same metasprites and the same
// VRAM cache, applies the same composition, and keeps the pieces the ROM
// dropped for being outside the console's 256 and inside the widened picture.
// They go into OAM entries the game's pass left parked, so nothing it placed
// moves, and not one byte of the game's memory is written: this is the frame
// the game has already built, plus the entries it could not afford.
//
// ## The graphics for those sprites are not loaded, because nothing asked for
// ## them
//
// A 16x16 frame is only in VRAM if something has drawn it: `sprite_emit` looks
// the frame up as it emits a piece (`$80:B9D6`), and that lookup is what
// uploads it. A piece dropped for being off the console's edge never reaches
// the lookup, so an actor walking off the side of the screen stops refreshing
// the frames of whatever parts of it are already past the edge, and the LRU
// reclaims them a few frames later. Put the sprite back without its graphics
// and you get exactly what this looked like: survivors and pickups that lose
// half of themselves at the margin, and flicker as slots come and go.
//
// `ws_lend_slot` fixes that by borrowing a slot, uploading the frame's 128
// bytes from ROM into it exactly as `$80:B960`'s DMA would have, and pointing
// the margin sprite at it. Which slot can be borrowed is the whole question,
// and there are two answers.
//
// A slot whose `slot_frame` entry is still negative is one the game has never
// allocated: no frame maps to it, so nothing the game can emit points at it,
// and it is free outright. Early in a level there are dozens of those. But the
// cache only ever fills — a slot goes from unused to used and never back —
// so after a few minutes there are none, which is why a long level was where
// the flicker got worse.
//
// The second answer is what makes it hold up: a slot whose graphics no sprite
// in *this frame's* OAM is drawing from is free for exactly the length of this
// picture. The OAM being drawn is sitting right there to be read, and every
// sprite in it is a whole 16x16 frame, so the slots it reads from are known
// exactly. Such a slot is borrowed for one frame and given back at the top of
// the next one — `ws_return_slots` puts into it whatever the game's own cache
// map says belongs there, read back out of ROM — before a single line of that
// frame is drawn.
//
// Either way the game's own tables are never written. It is not told that a
// slot has changed, because by the time it could look, it has not.
//
// ## The things on the ground are not there yet, or not any more
//
// A dropped piece is at least a record the game still has. This one is not.
//
// Everything a level leaves lying about -- the pickups, the keys, the weapons
// and the potions, thirty kinds of them and every one a single 16x16 -- is an
// entry in the level's object list and only sometimes an actor.
// `object_spawner_body` (`$80:C8F6`) walks that list every fourth tick and
// measures each entry against the middle of the camera's window,
// `($1B6A + $80, $1B6C + $70)`:
//
//     LDA $7E6D02,X : SEC : SBC $0E : (abs) : CMP #$0090 : BCS out
//
// Inside $90 of the middle on both axes it calls `object_spawn` and the object
// gets an actor record; outside, `$80:CAA8` takes the record away again. On the
// horizontal that window is the console's 256 plus sixteen pixels either side
// -- exactly one sprite's width of slack, which is precisely enough for a
// pickup to be gone by the time the last of it leaves the console and not one
// pixel more. Widen the picture by 43 and it pops out of existence with a body
// still showing, and pops back in at the other edge just as abruptly.
//
// That is the difference the monsters do not have, and it is why a zombie walks
// calmly off the side of a widened frame while the first-aid kit beside him
// blinks out of existence: monsters are spawned by their own threads and roam,
// and only the things a level put down live and die by the camera.
//
// It cannot be fixed the way the dropped pieces were, because there is no
// record to read. It does not need to be. The list itself is still there and
// still complete -- `W_OBJECT_X`, `W_OBJECT_Y`, `W_OBJECT_TYPE` and
// `W_OBJECT_STATE` in `port/wram.h` -- and so is the table `object_spawn`
// builds the record from, so `ws_object_sprites` draws the object from the two
// of them directly: an entry with no record, at the position the list gives it,
// with the metasprite `$80:CA6C` names for its type. When the camera does reach
// it, `object_spawn` puts the record at that same position out of that same
// list, so this is not a guess at where the thing would be. It is where it is.
//
// The state word is what keeps this honest. Zero is an object with no record,
// and the only kind drawn here. A record offset means it is already an actor
// and the pass above has it. `$8000` means it has been picked up or rescued and
// is never coming back, and `$C000` is the end of the list. Nothing else is
// consulted and nothing is remembered between frames: an object that stops
// being listed stops being drawn, the same tick.
//
// ## The neighbours go the same way, and a copy of one cannot be made to live
//
// The people are on the same plan as the pickups and a different list. A thread
// at `$81:81F6` walks the level's neighbour list against the same middle of the
// camera's window and the same kind of test, `CMP #$00A0` rather than `#$0090`,
// so the slack is 32 pixels either side of the console's 256 instead of 16:
//
//     LDA ($0C),Y : SEC : SBC $1A : (abs) : CMP #$00A0 : BCS out
//
// Inside it, `$81:81A2` starts a thread from the list's last two fields and the
// thread makes its own actor; outside it, `$81:8276` kills the thread, and the
// record goes with it. Thirty-two pixels is again just enough for the console
// and nothing like enough for a margin: a cheerleader standing in the widened
// left edge has a good half of herself still showing when the camera nudges
// right and takes her away.
//
// The pickups' answer will not work here. An object is a row in a table and a
// type, and `object_spawn` puts the same metasprite at the same coordinates
// every time, so drawing one is reading. A neighbour is a *thread*: what she
// looks like is whatever her thread had reached in whatever script it runs, and
// that dies with it. There is no table to go back to.
//
// Keeping the last picture of her instead is easy and it is not enough. She
// stops moving the moment the game lets go, and if the player stops walking she
// stops for ever; guessing the rest of her loop from the poses she was seen in
// gets her breathing again but not doing anything she was going to do; and none
// of it touches the far end at all, because when the camera comes back the
// spawner starts her thread again and the thread starts its script from the
// top, a pose or two from wherever the copy had got to. That reset is in the
// unmodified game too. What is not in the unmodified game is being able to see
// it: at 256 columns it happens 32 pixels off the side of the console.
//
// Which is the whole shape of the problem, and says what to do about it. The
// window is not wrong; the window is 256 pixels wide because the picture was.
// So this one is not drawn from outside the game at all. The window is two
// immediates, its middle (`ADC #$0080` to the camera's column, at `$81:820A`)
// and its reach either side of that (`CMP #$00A0`), and `ws_widen_window`
// writes both: the middle of the picture as it stands this frame, and half the
// picture and the same 32 pixels. The spawner goes on
// doing exactly what it always did -- one comparison, against the picture that
// is actually being drawn. She is spawned before she reaches the edge of it and
// taken away 32 pixels past the other one, which is the relationship the stock
// game has to its own edge, to the pixel. Her thread runs the whole time she is
// in view, so she animates because she is animating: nothing here follows her
// poses, remembers them, or replays them.
//
// This is the one kind of thing in this file that changes what the game does
// rather than what it draws (the wings, below, are the rest of the kind), and
// it is worth being plain about the cost. A neighbour
// in the margin is now a neighbour: she can be rescued out there, and a monster
// standing next to her can reach her out there, where on a console she would
// have been lifted out of the world and been safe until the camera came back.
// That is the same fact as her being visible and animated, seen from the other
// side, and there is no version of one without the other.
//
// **It was the reach alone at first, and that was unfair.** The middle was left
// at the camera's and the reach was made `#$00A0 + 2 * margin`, twice the
// margin because the picture is not always centred on the camera: at the end
// of a map one margin is all of it (the sliding margins, below), and a window
// with a fixed middle has to reach the widest either can get. The reason given
// here for not moving the middle was that nothing should spawn and unspawn as
// the margins trade. They trade a pixel at a time, as the camera closes on the
// map's end -- the picture's left edge in the world is `max(cam_x - margin,
// 0)` -- so a window that follows the picture moves no faster than the
// console's follows the camera, and the reason was not one. What the wide
// reach cost was reported in play-testing: neighbours dying where they cannot
// be seen. A monster touches anything in the visible list, which `actor_cull`
// fills from 128 pixels behind the camera to 383 ahead of it, so a neighbour
// is mortal wherever she exists. On a console that is 32 pixels she cannot be
// seen in, either side. With the reach alone it was 75 either side in 16:9,
// and 118 on the short side at the end of a map. With the middle moved as well
// it is the console's 32 everywhere, and she is still a neighbour in every
// column that is drawn. (Above and below it is 48 rows, stock, and untouched.)
//
// She also holds an actor slot for longer. `$80:825E` already returns
// empty-handed when there is no slot and `$81:81A2` already gives up quietly
// when it does, so the failure mode there is the one the game shipped with.
//
// Of the windows nothing else is written, and the pickups' `#$0090` in
// particular is left alone: the same byte would work there and would buy nothing, because a pickup
// can already be drawn from its list exactly, and changing the game to put a
// record behind it would only be changing the game. The vertical half of the
// neighbours' own test at `$81:8250` keeps its `#$00A0` as well, no rows having
// been added. At margin zero the stock figures go back and the game is the game
// again, which is what makes `--widescreen off` still byte-identical; and the
// co-simulation never sees any of it, because it does not include this file.
//
// ## Some monsters come on from the wings, and the wings are the console's
//
// Reported in play-testing: on level 12 the football players can be seen
// appearing on the field. Most monsters are started by `$81:80EC` at whichever
// of the level's spawn points is nearest a player, wherever that is, and come
// out of the ground or a door; being seen arriving is what they do. The
// football player is not one of those. His thread (`$81:C87B`) throws away
// the spawn point's column and takes one of two of its own (`$81:C7A5`):
//
//     LDA $00 : SEC : SBC $1B6A : CMP #$00A0 : BCS right
//     LDA $1B6A : SEC : SBC #$0008 : ...     ; 8 left of the console
//     right: LDA $1B6A : CLC : ADC #$0148    ; 72 right of it
//
// He is put down in the wings, facing in, gets set, and charges across. While
// he is getting set `$81:C742` takes him away again if the camera leaves him
// outside those same two columns (`SBC #$0008`, then `ADC #$0150` for the
// other one), and once he is running `$81:C850` takes him away when he is
// `#$0140` from the nearer player. Eight pixels left of the console is 35
// pixels inside a 16:9 picture: he appeared out of the air in the left margin
// and stood there. Measured on level 12 from the middle of the field: 32
// arrivals, ten at column -8 (or -6, a tick on) and 22 at 328.
//
// It is the neighbours' problem and it gets the neighbours' answer, for the
// neighbours' reasons: the columns are not wrong, they are the console's, and
// a thread cannot be drawn from outside. `ws_widen_window` moves each of them
// out by twice the margin -- twice because either margin can be all of it --
// and the reach from the player by as much, so that a wing further out is
// still inside it. He arrives 8 pixels past the widest the picture gets on
// that side instead of 8 past the console, and a moment later: on the same
// walk in 16:9, 35 arrivals, twelve at -92 to -98 and 23 at 414 to 418.
//
// Two more routines were found by looking for the idiom (every read of the
// camera's column in the four code banks, 38 of them) and get the same. One
// is the purple tentacle of the bonus rooms (`$82:990F`, records 0, 49 and
// 51), which picks a wing at random from a two-word table at `$82:990B` that
// is `-8` and `$0148` again and gives up `#$00D0` from the player: on record
// 51 in 16:9 it now arrives at -94 and 414, and still finds the player. The
// other is a creature at `$82:EAC5` that runs off and is taken away outside
// `-4` and `$0144`; nothing here has reached it and it has not been seen, but
// the words are the same kind and are moved the same way. The
// rest of the 38 are the camera's own arithmetic, the two spawners' windows
// (above), the big figure's plane (below), two that come on over the *top*
// edge, where there is no margin, and one that only decides whether to play a
// sound.
//
// ## ...and most of them go home when the players are out of sight
//
// Reported in play-testing: zombies in the left margin sinking away the moment
// they had risen, and zombies walking off that side vanishing with most of a
// body still showing. That search could not have found this, because it looks
// at the camera and this does not. A zombie's thread asks, every frame, whether
// either player is within `$D0` of it (`$81:8716`, through `player_bearing` at
// `$80:B2A5`), and when neither is it takes itself away:
//
//     LDA #$00D0 : LDX $16 : LDY $18 : JSL $80B2A5 : TAX : BNE + : DEC $12
//
// 208 pixels is a distance from the *player*, and it is the console's edge in
// disguise: the camera keeps a player well inside the 256, so 208 from one is
// past the edge on either side. The quick save that showed it has the player at
// column 162 and a zombie leaving at -47 -- 46 pixels off the console, and four
// inside a 16:9 picture with the zombie's other half still to come.
//
// So it gets the wings' answer, twice the margin on the reach, for the same
// reason: either margin can be the whole `2 * margin` at the end of a map, and
// the player can be that much further from the picture's edge than from the
// console's. Looking for the idiom -- a reach loaded, `$80:B2A5` or its
// sibling `player_in_range` at `$80:B26B`, and the thread's leaving flag
// decremented or incremented when it comes back empty -- finds seventeen of them
// (`WS_ROM_WORDS`, below): both of the zombie's threads (`$81:87F8`, walking,
// and `$81:88CA`, rising), the creature's `monster_seek` (`$81:BB93`; see
// `port/monster.h`, which reads the word back out of the cartridge because it
// is ported), and fourteen more monsters' copies of the same three lines, all
// `$00D0` but one `$0140` and one `$00F0`. The calls that load a reach and do
// something *else* with the answer -- start a chase, wait for the players to
// come near -- are left alone; being seen is not what they are about.
//
// It is the same trade as the neighbours' window. A monster in the margin
// stays a monster, holding its slot and its share of `W_SPAWN_LOAD` for as long
// as it can be seen, where on a console it would have gone home. And the reach
// is a square, not a pair of columns, so it grows above and below as well,
// where no picture was added: a zombie can follow the players 86 pixels further
// off the top or bottom of a 16:9 picture before it gives up.
//
// ## A boss too big for sprites is a background, and the game parks it
//
// The giant baby and the flying saucer are not sprites (`port/bossbg.h`): each
// is a figure of tiles written into BG1's map, and BG1 is scrolled to where
// the figure stands. In every level BG1 is a 64x64 map -- a plane 512 pixels
// square -- and it holds the figure in its first 14 or 20 columns and nothing
// else: looked at a few seconds into each of the 48 levels, where it is blank
// from corner to corner on the 44 without a big figure (8, 12, 21 and 25
// have one). Two things went wrong with it in the margins,
// and they are the two halves of one report.
//
// **The policy.** BG1 was left to `ppu_wideAuto`, which has to guess, and
// both of its guesses were wrong here. With nothing in the console's edge
// columns it clips the layer to the console's 256, so a figure standing
// across the edge was cut off flat at it. With something in them it takes a
// 64-column map for one the game maintains only half of and repeats the
// console's 256 columns outward -- so the half of the baby at the console's
// left edge was drawn a second time in the right margin, and the margin it
// was actually standing in stayed empty. Neither guess is needed: the plane is
// the game's and all of it is maintained (any 256 of its 512 columns are on
// the console at some scroll). In a level BG1 is `ppu_wideStretch`, like the
// world.
//
// **The plane comes round.** It is 512 pixels and then it repeats, which the
// console never sees: the game keeps the figure's origin within 255 columns
// of the console's left edge either way, and 255 + 256 is one short of 512.
// The picture is wider than that by its margins. With the origin 214 or more
// columns off to the left -- the baby long gone that way -- the right margin
// was reading the plane's *next* lap, and the baby's bottle and arm stood in
// it: a figure flashing at the side of the screen as the boss walked about
// off the other one. So in that band `ws_boss_plane` parks the plane itself,
// where the game would have had the console been that wide. Both decisions
// are taken from where the figure is in its plane (`ws_boss_extent`) and
// against 16 columns more than the picture, since the smoothing captures that
// much of every plane and slides it into view as it eases a scroll.
//
// **The parking.** The job that scrolls BG1 (`$82:8209`, every vblank) takes
// the camera from the plane's world origin (`$1E6E`, `$1E70`) and, when the
// origin is 256 or more to the right of the console's left edge (or 256 or
// more to the left of it, or as far off vertically), writes `$0100` to both
// scrolls instead: the blank quarter of the plane. That is right for the
// console and it is the sprite cull over again -- a figure whose left edge
// is in the right margin is one the game has put away. (To the left it parks
// only a figure that is already past the widest margin.) So when the scroll
// is the parked one and the origin is inside the right margin, `ws_boss_plane`
// writes the scroll the job would have written had the console been that
// wide, from the same two words and the same camera (which copy of them, and
// why the register is read with ten bits, is at the routine). The PPU's
// registers only, which the game writes again at the next vblank and never
// reads.
//
// ## 21:9 is wider than the game's own reach
//
// 16:9 fits inside every distance the game keeps: 43 a side, and 86 on one
// side at the end of a map. 21:9 is 96 a side and 192 on one, and two things
// that were slack for 16:9 run out.
//
// **The cull.** `actor_cull`'s 128 behind and 383 ahead are words in the
// ROM (`$80:BCF8` and `$80:BCFD`, the horizontal test; the vertical one
// after it is left alone). At the right-hand end of a map the picture's left
// edge is 192 behind the camera, and a record between 128 and 192 behind it
// was not in the visible list: not drawn, since the margins' sprites are
// drawn from that list, and out of the reach of every monster, since that
// list is also what they touch. `ws_widen_window` moves each word out to 32
// past the picture's edge when the edge has gone past it, the same 32 the
// neighbours' window keeps, so a neighbour is in the list everywhere she can
// exist. That never happens at 16:9, whose edges stop 42 short, so 16:9 and
// off keep the stock words.
//
// **The ring.** The world's tilemap is a ring of 64 columns, and the margins
// were filled to the widest either one can get, on both sides at once: at
// 21:9 that is 24 columns each side of the game's 32, 80 in all, and the far
// end of one side came round onto the other. Only what the picture reaches
// is filled now, with the smoothing's 16 columns either side of it, which is
// 62 at most.
//
// ## Everything above happens one tick late
//
// The frame the console is about to draw was composed during the *previous*
// tick: the game builds an OAM buffer and a tilemap upload queue as it runs,
// and the NMI at the top of the next frame DMAs both into the hardware and only
// then lets the game run again. So at the moment the margins are drawn, the
// OAM and VRAM on screen are one tick older than the actor records, the camera
// and the sprite cache in WRAM.
//
// Reading the live memory therefore composes the margins from a world that has
// already moved: pieces land one frame's motion away from the same actor's
// on-screen pieces, and near an eight-pixel boundary the tilemap columns go to
// the wrong ring slots. Both show up as edge flicker whenever the camera moves.
// So `Widescreen` keeps a copy of WRAM as it stood at the previous frame start,
// and everything here reads that. It is a lot of memory to copy and it is
// exactly the memory the picture was made from.

#ifndef ZAMN_WIDESCREEN_H
#define ZAMN_WIDESCREEN_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ppu.h"
#include "snes.h"

#include "blood.h"
#include "radar.h"
#include "assets/rom.h"
#include "assets/sprite.h"
#include "port/camera.h"
#include "port/oam.h"
#include "port/wram.h"

// How many distinct frames one widened picture may borrow cache slots for.
// Measured over nine movies and 13,869 frames of play: the busiest picture puts
// twelve pieces back and borrows for five of them, so this is room to spare
// rather than a limit anything is expected to reach.
#define WS_LENT_MAX 24

// The metasprite `object_spawn` (`$80:C9E3`) gives an object of each type, as
// `LDA $1F0A,X : TAX : LDA $CA6C,X` reads it -- 30 words, indexed by a type
// already doubled, and every one of them a pointer into bank `$8F`. The table
// next door at `$80:CA30` is the collide id, which a picture does not need.
#define OBJECT_META_TABLE 0x80ca6cu
#define OBJECT_TYPE_COUNT 30

// The big figure's plane: where its origin is in the world (the two words
// `$82:8209` takes the camera from), and the scroll that job parks it at.
#define WS_BOSS_PLANE_X 0x1e6eu
#define WS_BOSS_PLANE_Y 0x1e70u
#define WS_BOSS_PARKED 0x0100
// How far past the picture's edges a plane is still looked at: the smoothing's
// capture margin, `LAYERS_MARGIN` in `layers.h`, which this file does not see.
#define WS_CAPTURE_SLACK 16

// The neighbours' window (`$81:8207`, and the header): `ADC #$0080` to the
// camera's column is its middle and `CMP #$00A0` its reach either side.
#define WS_WINDOW_MIDDLE_OPCODE 0x0820au
#define WS_WINDOW_MIDDLE_STOCK 0x0080
#define WS_WINDOW_REACH_OPCODE 0x0823cu
#define WS_WINDOW_REACH_STOCK 0x00a0

// `actor_cull`'s horizontal window (`$80:BCF7 CMP #$FF80` and `$80:BCFC
// CMP #$0180`), and how far past the picture's edge it is made to reach once
// the edge has passed it. See "21:9 is wider than the game's own reach".
#define WS_CULL_BEHIND_OPCODE 0x03cf7u
#define WS_CULL_BEHIND_STOCK 0x0080
#define WS_CULL_AHEAD_OPCODE 0x03cfcu
#define WS_CULL_AHEAD_STOCK 0x0180
#define WS_CULL_SLACK 32

// A word in the ROM file that is a distance from the console's edge and has to
// be one from the widest the picture gets: the byte before it that says
// the file is this ROM (the opcode, or for a table the `RTS` it follows),
// what the cartridge has there, and how many margins go on it. LoROM: bank
// `$81` is file offset `$08000` and bank `$82` is `$10000`.
typedef struct {
  uint32_t guard_at;
  uint8_t guard;
  uint32_t word_at;
  uint16_t stock;
  int margins;
} WsRomWord;

static const WsRomWord WS_ROM_WORDS[] = {
    // The football player. Where he is put down, `$81:C7B9 SBC #$0008` and
    // `$81:C7C7 ADC #$0148`; where he may stand while he gets set,
    // `$81:C746 SBC #$0008` and then `$81:C751 ADC #$0150` on top of that, so
    // that one moves twice; and how far from the player he may run,
    // `$81:C850 LDA #$0140`.
    {0x0c7b9u, 0xe9, 0x0c7bau, 0x0008, 2},
    {0x0c7c7u, 0x69, 0x0c7c8u, 0x0148, 2},
    {0x0c746u, 0xe9, 0x0c747u, 0x0008, 2},
    {0x0c751u, 0x69, 0x0c752u, 0x0150, 4},
    {0x0c850u, 0xa9, 0x0c851u, 0x0140, 2},
    // The tentacle's (`$82:990F`) two wings, a table after the `RTS` at `$82:990A`, and its
    // reach, `$82:97A9 LDA #$00D0`.
    {0x1190au, 0x60, 0x1190bu, 0xfff8, -2},
    {0x1190au, 0x60, 0x1190du, 0x0148, 2},
    {0x117a9u, 0xa9, 0x117aau, 0x00d0, 2},
    // `$82:EAC5`: `ADC #$0004` to the creature's column against the camera's,
    // and `ADC #$0144` to the camera's against the creature's.
    {0x16ac7u, 0x69, 0x16ac8u, 0x0004, 2},
    {0x16ad3u, 0x69, 0x16ad4u, 0x0144, 2},
    // How far from both players a monster may be before it leaves: the
    // `LDA #$00D0` before each `JSL $80B2A5` or `$80B26B` whose empty answer
    // sets the thread's leaving flag. The zombie's two threads first,
    // `$81:8716` walking and `$81:8B46` rising; the creature's
    // `monster_seek`, `$81:BB93`; then the rest in address order.
    {0x08716u, 0xa9, 0x08717u, 0x00d0, 2},
    {0x08b46u, 0xa9, 0x08b47u, 0x00d0, 2},
    {0x0bb93u, 0xa9, 0x0bb94u, 0x00d0, 2},
    {0x09d6eu, 0xa9, 0x09d6fu, 0x00d0, 2},
    {0x09dbbu, 0xa9, 0x09dbcu, 0x00d0, 2},
    {0x0aedbu, 0xa9, 0x0aedcu, 0x00d0, 2},
    {0x0b7b6u, 0xa9, 0x0b7b7u, 0x00d0, 2},
    {0x0cad1u, 0xa9, 0x0cad2u, 0x0140, 2},
    {0x0d5b2u, 0xa9, 0x0d5b3u, 0x00d0, 2},
    {0x0d90fu, 0xa9, 0x0d910u, 0x00d0, 2},
    {0x0da32u, 0xa9, 0x0da33u, 0x00d0, 2},
    {0x0dd69u, 0xa9, 0x0dd6au, 0x00d0, 2},
    {0x0de89u, 0xa9, 0x0de8au, 0x00d0, 2},
    {0x0deddu, 0xa9, 0x0dedeu, 0x00d0, 2},
    {0x0df5cu, 0xa9, 0x0df5du, 0x00d0, 2},
    {0x0e610u, 0xa9, 0x0e611u, 0x00d0, 2},
    {0x1b430u, 0xa9, 0x1b431u, 0x00f0, 2},
};

// Everything the hook needs: where the ROM is, because metasprites and sprite
// graphics are read from it, how wide the margins are, the memory the picture
// on screen was composed from, and the cache slots borrowed for it.
typedef struct {
  Rom rom;
  int margin;  // game pixels per side, 0 when widescreen is off
  uint8_t mem[0x20000];
  bool have_mem;
  // ...and the memory the *sprites* on screen were composed from, which is not
  // the same thing. `mem` is copied at the top of the picture, and on a tick
  // the game runs long that lands part way through it: the camera moved and
  // the records not yet, or the other way about. The margins' sprites were
  // drawn from that, so on those ticks they stood a step away from the
  // console's, and how far depended on how long the tick had taken to get
  // there. `pass_mem` is copied when the sprite pass ends and `sprite_mem`
  // from it when the NMI sends the OAM that pass built, so the margins and the
  // middle are drawn from the same moment whatever the timing. See
  // `widescreen_pass_done`.
  uint8_t pass_mem[0x20000];
  uint8_t sprite_mem[0x20000];
  bool have_sprite_mem;
  bool pass_new;    // a pass has ended since the last OAM was sent
  int sends_stale;  // OAM sent since then, with no pass ended in between
  uint16_t lent_frame[WS_LENT_MAX];
  int lent_slot[WS_LENT_MAX];
  int lent_count;
  int back_slot[WS_LENT_MAX];  // ...of those, the ones that have to be put back
  int back_count;
  uint8_t slot_drawn[SPRITE_SLOTS];  // slots this frame's own sprites read from
  // Whose the sprites put into the margins are: for each OAM entry filled
  // here, the record it was composed from and that record's origin on the
  // screen, as `SpriteOamOwners` has them for the game's own -- see
  // `ws_owners`. -1 for an entry that is not one of these, and for a thing on
  // the ground drawn from the level's list, which has no record.
  int16_t owner_rec[OAM_ENTRIES], owner_ox[OAM_ENTRIES], owner_oy[OAM_ENTRIES];
  // How `ws_place_screen_sprites` found the pass that is on screen: frames it
  // was not the newest one, and frames it was none of those kept.
  long place_behind, place_unmatched;
  // `--red-blood` (`src/blood.h`), here because this is the frame hook and
  // there is one: it marks the game over's drips after the sprites are placed.
  Blood blood;
  // The survivor radar's squares (`src/radar.h`), drawn from here for the
  // same reason, and where this frame put the sprites laid out over the
  // panel, which the squares go with.
  Radar radar;
  int screen_place;
} Widescreen;

// WRAM as a flat 128 KB, the way `src/port/wram.h` numbers it: bank `$7E` is
// `$00000-$0FFFF` and bank `$7F` is `$10000-$1FFFF`.
static inline uint16_t ws_r16(const uint8_t* mem, uint32_t off) {
  return (uint16_t)(mem[off & 0x1ffff] | (mem[(off + 1) & 0x1ffff] << 8));
}

// What the sprite code reads: the pass's own memory once there is one, and the
// top-of-picture copy until then (the first frames, and after a quick load).
static inline const uint8_t* ws_sprite_mem(const Widescreen* ws) {
  return ws->have_sprite_mem ? ws->sprite_mem : ws->mem;
}

// `EOR #$FFFF : SEC : SBC #$000F`, the mirror the flipped emitters apply to a
// piece offset. Same as `mirror()` in `src/assets/sprite.c`, which is static.
static inline uint16_t ws_mirror(uint16_t v) {
  return (uint16_t)(((uint16_t)~v) - 0x000f);
}

// Where the main game thread is parked: the address `$80:8353` will hand it
// back to when its wait is up, read off the thread's own stack. That thread is
// `$80:84B1` -- the one that runs a level (`$80:8516`), then the game over
// (`$80:8A00`), then the top scores -- and the entry table names it by the
// address its spawn recorded, `$84B0`, the byte before. `$80:8353` pushes B, P
// and D over the `JSL`'s return address and parks the stack pointer in
// `thread_sp`, so from the parked pointer up the frame is D (a word), P, B and
// then the return's low, high and bank bytes; the return is one past the
// address pushed. Stale while the thread is running -- the table is written
// when it parks -- which is the last place it waited, and so still the right
// routine. 0 when there is no such thread, as there is not on the title.
#define WS_MAIN_THREAD_ENTRY 0x84b0u
static inline uint32_t ws_main_thread_at(const uint8_t* mem) {
  for (uint32_t s = 0; s < WRAM_THREAD_SLOTS; s++) {
    if (ws_r16(mem, W_THREAD_ENTRY + s * 2) != WS_MAIN_THREAD_ENTRY ||
        ws_r16(mem, W_THREAD_ENTRY_BANK + s * 2) != 0x80)
      continue;
    const uint32_t sp = ws_r16(mem, W_THREAD_SP + s * 2);
    return (((uint32_t)mem[(sp + 7) & 0x1ffff] << 16) | ws_r16(mem, sp + 5)) + 1;
  }
  return 0;
}

// The game over: `$80:8A00`, from the wait after the mask's tilemap has gone
// up over the panel's (`$80:8A78`; the `JSL $808353` at `$80:8A0D` returns to
// `$8A11`) to the 300-tick wait with the mask fully up (`$80:8A41`, returning
// to `$8A45`), whose return stays on the stack through the fade that follows.
// The 30-tick wait before the upload (`$8A03`, returning to `$8A07`) is left
// out on purpose: the panel is still on BG3 then, and live, and centring it
// for half a second would be a jump. The scroll shadow tells the two waits
// apart as well -- `$80:8A84` sets it just before the upload, and a level
// never scrolls the panel -- and is checked with the range rather than
// instead of it: it stays where it stopped into the next game.
#define WS_GAME_OVER_FIRST 0x8a11u
#define WS_GAME_OVER_LAST 0x8a45u
static inline bool ws_game_over(const uint8_t* mem) {
  const uint32_t at = ws_main_thread_at(mem);
  return (at >> 16) == 0x80 && (at & 0xffff) >= WS_GAME_OVER_FIRST &&
         (at & 0xffff) <= WS_GAME_OVER_LAST &&
         ws_r16(mem, W_BG3_VSCROLL_SHADOW) != 0;
}

// Put a 16x16 frame into a cache slot's VRAM, which is what the DMA `$80:B960`
// queues would have done: the first 64 bytes are the slot's two top tiles and
// the second 64 the two below them, one VRAM row of 32 words further on.
static inline bool ws_upload(Snes* snes, const Rom* rom, uint16_t frame,
                             int slot) {
  uint8_t raw[SPRITE_FRAME_BYTES];
  if (!sprite_frame_read(rom, frame, raw)) return false;
  const uint16_t base = sprite_slot_vram(slot);
  for (int i = 0; i < 32; i++) {
    snes_writeVramWord(snes, (uint16_t)(base + i),
                       (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8)));
    snes_writeVramWord(snes, (uint16_t)(base + 0x100 + i),
                       (uint16_t)(raw[64 + i * 2] | (raw[65 + i * 2] << 8)));
  }
  return true;
}

// Give back the slots the last picture borrowed, by putting into each one the
// graphics the game's own cache map says belongs there. The live map and not
// the snapshot: a slot the game has re-let since has already had its own upload
// DMA'd into it, and the newest owner is the one whose graphics belong there.
static inline void ws_return_slots(Snes* snes, Widescreen* ws) {
  for (int i = 0; i < ws->back_count; i++) {
    const uint16_t f = ws_r16(snes->ram, W_SLOT_FRAME + ws->back_slot[i] * 2);
    if (!(f & 0x8000))
      ws_upload(snes, &ws->rom, (uint16_t)(f / 2), ws->back_slot[i]);
  }
  ws->back_count = 0;
}

// The OAM tile word for `frame`, putting it into a cache slot nothing else is
// reading from if it is not already in one. -1 if there is nowhere to put it.
//
// Two kinds of slot will do, in that order. One the game has never allocated —
// `slot_frame` still negative — is free outright: no frame maps to it, so
// nothing the game can emit points at it. Failing that, one whose graphics no
// sprite in *this* frame's OAM is drawing from is free for the length of this
// picture, and is put back at the top of the next one before anything is drawn.
// A level that has been running for a while has none of the first kind left,
// because the cache only ever fills, and that is when the second kind matters.
//
// Either way the game's own tables are not touched: it is not told that a slot
// has changed, because by the time it could look, it has not.
static inline int ws_lend_slot(Snes* snes, Widescreen* ws, uint16_t frame) {
  const uint16_t entry = ws_r16(ws_sprite_mem(ws), W_FRAME_SLOT + (uint32_t)frame * 2);
  if (!(entry & 0x8000)) return sprite_slot_tile(entry / 2);

  for (int i = 0; i < ws->lent_count; i++)
    if (ws->lent_frame[i] == frame) return sprite_slot_tile(ws->lent_slot[i]);
  if (ws->lent_count == WS_LENT_MAX) return -1;

  for (int pass = 0; pass < 2; pass++) {
    for (int slot = 0; slot < SPRITE_SLOTS; slot++) {
      const bool mapped =
          !(ws_r16(snes->ram, W_SLOT_FRAME + slot * 2) & 0x8000);
      if (pass == 0 ? mapped : (!mapped || ws->slot_drawn[slot])) continue;
      bool taken = false;
      for (int i = 0; i < ws->lent_count; i++) taken |= ws->lent_slot[i] == slot;
      if (taken) continue;
      if (!ws_upload(snes, &ws->rom, frame, slot)) return -1;

      if (pass == 1) ws->back_slot[ws->back_count++] = slot;
      ws->lent_frame[ws->lent_count] = frame;
      ws->lent_slot[ws->lent_count] = slot;
      ws->lent_count++;
      return sprite_slot_tile(slot);
    }
  }
  return -1;
}

// One metasprite's pieces, composed exactly as `sprite_emit` composes them and
// placed in the OAM entries the game's own pass left parked, for the pieces
// that fall outside the console's 256 and inside the widened picture. Returns
// the next free entry.
//
// The one test worth reading twice is the one that decides which pieces those
// are. `x > -16 && x < 256` is the console's own picture: a 16-wide piece with
// any part of it in there. For a record the game drew, that is precisely the
// `CMP #$0100 / CMP #$FFF1` pair the emitters keep a piece on, so skipping it
// leaves exactly the pieces the ROM dropped. For something the game has no
// record of -- an object the spawner has not reached yet -- the same test
// means something different and just as necessary: nothing drawn from outside
// the game may put a pixel where the console puts one, because there the
// console is right and this is not.
//
// **...except in the four ticks the console is late.** `inside` draws the
// pieces within the console's 256 as well, and is for the things on the ground
// alone. The spawner that gives such a thing its record looks every fourth
// tick, and its window reaches sixteen pixels past the console's edge, which a
// camera at two pixels a tick crosses in eight: with the tick the picture is
// behind by, a thing can be inside the console's columns and still have no
// record. On the console that is a sliver at the edge of the glass turning up
// a moment late. In a widened picture the thing had been in plain sight in the
// margin, drawn from the list, and at column -15 the list stopped drawing it
// and nothing else had started: it went out for a tick and came back, once
// for every pass of the camera. Reported in play-testing as the keys of
// level 7 flashing while walking from side to side with them in the margin,
// and measured there as one tick missing in every 84. A thing with no record
// is where the list says it is on either side of column 256, so it is drawn
// there; if the record has come since the memory this reads was copied, the
// game has drawn the same sprite in the same place and this one is under it.
static inline int ws_emit_meta(Snes* snes, Widescreen* ws, int slot, int rec,
                               const SpriteMeta* meta, int16_t ox, int16_t oy,
                               uint16_t attr_or, uint16_t attr_and, bool flip_x,
                               bool flip_y, int left, int right, bool inside) {
  const uint16_t flip_eor =
      (uint16_t)((flip_x ? 0x4000 : 0) | (flip_y ? 0x8000 : 0));

  for (int i = 0; i < meta->count && slot < OAM_ENTRIES; i++) {
    const SpritePiece* p = &meta->pieces[i];

    uint16_t sy = (uint16_t)p->y;
    if (flip_y) sy = ws_mirror(sy);
    sy = (uint16_t)(sy + (uint16_t)oy);
    // The ROM's Y test, unchanged: nothing is added above or below, because
    // nothing was added above or below.
    if (sy >= 0x00e0 && sy < 0xfff1) continue;

    uint16_t sx = (uint16_t)p->x;
    if (flip_x) sx = ws_mirror(sx);
    sx = (uint16_t)(sx + (uint16_t)ox);
    const int x = (int16_t)sx;
    if (!inside && x > -16 && x < 256) continue;
    // A 16-wide piece at `x` covers `x..x+15`, so it is worth drawing while any
    // of that is inside the widened picture.
    if (x >= 0 ? x > 255 + right : x < -15 - left) continue;

    const int tile = ws_lend_slot(snes, ws, p->frame);
    if (tile < 0) continue;
    const uint16_t word =
        (uint16_t)(((uint16_t)tile | (p->attr & attr_and) | attr_or) ^ flip_eor);
    snes_setSprite(snes, slot, x & 0x1ff, sy & 0xff, word, true);
    ws->owner_rec[slot] = (int16_t)rec;
    ws->owner_ox[slot] = ox;
    ws->owner_oy[slot] = oy;
    slot = snes_freeSprite(snes, slot + 1);
  }
  return slot;
}

// The objects the level put on the ground that have no actor record right now,
// drawn from the four arrays the spawner reads and the metasprite table it
// spawns them with. See the header, and `W_OBJECT_STATE` in `port/wram.h` for
// what the states mean.
static inline int ws_object_sprites(Snes* snes, Widescreen* ws, int slot,
                                    int left, int right) {
  const uint8_t* mem = ws_sprite_mem(ws);
  const uint16_t cam_x = ws_r16(mem, W_CAMERA_X);
  const uint16_t cam_y = ws_r16(mem, W_CAMERA_Y);

  for (int i = 0; i < OBJECT_SLOT_COUNT && slot < OAM_ENTRIES; i++) {
    // `$80:C934 BIT $1EC4,X : BVS done : BMI skip`, in that order and for the
    // same reasons: past the end of the list there is nothing, and an object
    // already picked up is not coming back.
    const uint16_t state = ws_r16(mem, W_OBJECT_STATE + (uint32_t)i * 2);
    if (state & 0x4000) break;
    if (state) continue;  // gone for good, or holding a record already drawn

    const uint16_t type = ws_r16(mem, W_OBJECT_TYPE + (uint32_t)i * 2);
    if (type >= OBJECT_TYPE_COUNT * 2 || (type & 1)) continue;
    SpriteMeta meta;
    if (sprite_meta_read(&ws->rom,
                         ((uint32_t)SPRITE_META_BANK_LO << 16) |
                             rom_word(&ws->rom, OBJECT_META_TABLE + type),
                         &meta) != SPRITE_OK)
      continue;

    // `object_spawn` writes the record it allocates from these three and
    // nothing else: X and Y straight out of the list, Z zero, and the only
    // flag it sets is `ACTOR_DRAW`. So there is no flip, no forced palette and
    // no raised priority to work out -- an object is drawn one way.
    slot = ws_emit_meta(
        snes, ws, slot, -1, &meta,
        (int16_t)(ws_r16(mem, W_OBJECT_X + (uint32_t)i * 2) - cam_x),
        (int16_t)(ws_r16(mem, W_OBJECT_Y + (uint32_t)i * 2) - cam_y), 0x2000,
        0xffff, false, false, left, right, true);
  }
  return slot;
}

// How a visible record is drawn: its metasprite, its origin on the screen and
// what is done to each piece's attributes -- `draw_args` in `src/port/oam.c`,
// which is `$80:BD46`..`$80:BD9F`. False for a record that draws nothing.
static inline bool ws_record_draw(const Widescreen* ws, uint16_t rec, SpriteMeta* meta,
                                  int16_t* ox, int16_t* oy, uint16_t* attr_or,
                                  uint16_t* attr_and, uint16_t* flags_out) {
  const uint8_t* mem = ws_sprite_mem(ws);
  const uint16_t flags = ws_r16(mem, (uint32_t)rec + ACTOR_FLAGS);
  if (!(flags & ACTOR_DRAW)) return false;

  *attr_or = (flags & ACTOR_PRIORITY_TOP) ? 0x3000 : 0x2000;
  *attr_and = 0xffff;
  if (flags & ACTOR_ATTR_SET) {
    *attr_or |= ws_r16(mem, (uint32_t)rec + ACTOR_ATTR);
    *attr_and = 0xf1ff;
  }
  if (flags & ACTOR_SCREEN_SPACE) {
    *ox = (int16_t)ws_r16(mem, (uint32_t)rec + ACTOR_X);
    *oy = (int16_t)ws_r16(mem, (uint32_t)rec + ACTOR_Y);
  } else {
    *ox = (int16_t)(ws_r16(mem, (uint32_t)rec + ACTOR_X) - ws_r16(mem, W_CAMERA_X));
    *oy = (int16_t)(ws_r16(mem, (uint32_t)rec + ACTOR_Y) -
                    ws_r16(mem, (uint32_t)rec + ACTOR_Z) - ws_r16(mem, W_CAMERA_Y));
  }
  const uint16_t ptr = ws_r16(mem, (uint32_t)rec + ACTOR_META);
  if (ptr < 0x8000) return false;
  const uint16_t bank = ws_r16(mem, (uint32_t)rec + ACTOR_META_BANK);
  if (bank < SPRITE_META_BANK_LO || bank > SPRITE_META_BANK_HI) return false;
  *flags_out = flags;
  return sprite_meta_read(&ws->rom, ((uint32_t)bank << 16) | ptr, meta) == SPRITE_OK;
}

// The pieces `sprite_emit` dropped for being outside the console's 256, drawn
// into the OAM entries the game's own pass left parked. See the header.
static inline void ws_margin_sprites(Snes* snes, Widescreen* ws, int left,
                                     int right) {
  const uint8_t* mem = ws_sprite_mem(ws);
  const uint16_t count = ws_r16(mem, W_VISIBLE_ACTOR_COUNT);
  int slot = snes_freeSprite(snes, 0);
  ws->lent_count = 0;
  memset(ws->owner_rec, 0xff, sizeof ws->owner_rec);

  // Which cache slots the picture already on its way to the screen is reading
  // from. Every sprite the game emits is a whole 16x16 frame, so an entry's
  // tile number is a slot's tile number and the map back is exact.
  memset(ws->slot_drawn, 0, sizeof ws->slot_drawn);
  for (int e = 0; e < OAM_ENTRIES; e++) {
    // Parked is `$E0` exactly, and the emitters can reach neither it nor the
    // sixteen rows below it; `$F1` upwards is a sprite hanging off the top of
    // the screen, which is drawn.
    const int y = snes->ppu->oam[e * 2] >> 8;
    if (y >= 0xe0 && y <= 0xf0) continue;
    const int tile = snes->ppu->oam[e * 2 + 1] & 0x1ff;
    const int s = (tile / 32) * SPRITE_SLOTS_PER_ROW + (tile % 32) / 2;
    if (s < SPRITE_SLOTS) ws->slot_drawn[s] = 1;
  }

  for (uint16_t cur = 0; cur < count && slot < OAM_ENTRIES; cur += 2) {
    const uint16_t rec = ws_r16(mem, W_VISIBLE_ACTORS + cur);
    SpriteMeta meta;
    int16_t ox, oy;
    uint16_t attr_or, attr_and, flags;
    if (!ws_record_draw(ws, rec, &meta, &ox, &oy, &attr_or, &attr_and, &flags)) continue;

    slot = ws_emit_meta(snes, ws, slot, rec, &meta, ox, oy, attr_or, attr_and,
                        (flags & SPRITE_FLIP_X) != 0,
                        (flags & SPRITE_FLIP_Y) != 0, left, right, false);
  }

  // ...and then the ones with no record to have been dropped from. Last because
  // they are the only ones drawn from a list rather than from a record, so if
  // OAM runs out it is these that go without.
  ws_object_sprites(snes, ws, slot, left, right);
}

// The sprites of a screen-space record go with whatever BG3 is carrying,
// not with the world: the survivor radar's markers are laid out over its
// box, which is on the panel that `ppu_wideAnchor` pins to the picture's
// edges, and the drips hanging from the game over mask's foot are sprites
// of such records too, laid out over the trunks the mask draws for them --
// anchored with the panel while the mask was centred, they hung 43 columns
// from their trunks, which ended flat. The pass says which OAM entries came
// from which record, and the record's flags are read from `ws->mem`, the
// memory the picture was composed from. A place per slot rather than a moved
// X, so that a frame the game did not redraw is not moved twice.
//
// **Which pass is on screen has to be read off the OAM.** This runs at line 0.
// The vblank just ended DMA'd the pass's buffer -- and the game began its next
// tick straight after that NMI, still inside vblank, so by line 0 the pass has
// usually run *again* and `sprite_oam_owners` describes sprites that are a
// frame from being shown. While the same records keep the same slots nobody
// can tell. When they do not -- the radar coming up puts its marker in slot 0
// and moves everything else along one, and the marker is one slot multiplexed
// over the survivors -- the entry that had become the marker's was still a
// piece of a zombie or of the player on screen, and was pinned to the panel
// for a frame: 43 columns to the left of the rest of him, the player's head
// beside the player. And the marker, in an entry the table still called the
// world's, 43 to the right of its box. So the pass is identified by its bytes
// (`sprite_oam_history`): the newest whose owned entries are what the PPU
// holds.
//
// **None matching is a pass the ROM ran**, and then no table says anything
// about this OAM. That is any tick with a record whose handler the port does
// not have yet -- the port hands the whole actor pass back, the sprites with
// it -- and every tick after F1. It was the newest table, the last the port's
// pass wrote, and that could be a table from seconds before: reported in
// play-testing on level 15, where something by a fire has such a handler, as
// Zeke's head and the top of the fire drawn 43 columns from where they were,
// for as long as the player stood there. Entry 0 was Zeke's head and the old
// table said it was the radar's marker. So an unmatched OAM is read by its
// look instead (`ws_screen_by_look`), and NULL says so.
static inline const SpriteOamOwners* ws_pass_on_screen(const Snes* snes, Widescreen* ws) {
  const uint32_t newest = sprite_oam_owners.serial;
  for (uint32_t back = 0; back < SPRITE_OAM_HISTORY && back < newest; back++) {
    const SpriteOamPass* pass = &sprite_oam_history[(newest - back) % SPRITE_OAM_HISTORY];
    if (pass->owners.serial != newest - back) continue;
    bool same = true;
    for (int s = 0; s < OAM_ENTRIES && same; s++) {
      if (pass->owners.rec[s] < 0) continue;
      const uint8_t* b = &pass->low[s * 4];
      same = snes->ppu->oam[s * 2] == (uint16_t)(b[0] | (b[1] << 8)) &&
             snes->ppu->oam[s * 2 + 1] == (uint16_t)(b[2] | (b[3] << 8));
    }
    if (!same) continue;
    if (back > 0) ws->place_behind++;
    return &pass->owners;
  }
  if (newest > 0) ws->place_unmatched++;
  return NULL;
}

// Which OAM entries a screen-space record drew, found without a table: each
// such record in the visible list is composed as `sprite_emit` composes it,
// from the memory the picture was made from, and an entry is its piece if it
// is that piece -- the same column, row and attributes, and the same tile
// wherever the cache map says which tile that is. The cache map is asked both
// as it was and as it is, since the pass that drew this OAM may have loaded
// the frame after the copy was taken; a frame in neither is matched on the
// rest. Entries the margins filled are theirs, as with a table.
static inline void ws_screen_by_look(const Snes* snes, const Widescreen* ws, bool screen[OAM_ENTRIES]) {
  memset(screen, 0, OAM_ENTRIES * sizeof screen[0]);
  const uint8_t* mem = ws_sprite_mem(ws);
  const uint16_t count = ws_r16(mem, W_VISIBLE_ACTOR_COUNT);
  for (uint16_t cur = 0; cur < count; cur += 2) {
    const uint16_t rec = ws_r16(mem, W_VISIBLE_ACTORS + cur);
    if (!(ws_r16(mem, (uint32_t)rec + ACTOR_FLAGS) & ACTOR_SCREEN_SPACE)) continue;
    SpriteMeta meta;
    int16_t ox, oy;
    uint16_t attr_or, attr_and, flags;
    if (!ws_record_draw(ws, rec, &meta, &ox, &oy, &attr_or, &attr_and, &flags)) continue;
    const bool flip_x = (flags & SPRITE_FLIP_X) != 0, flip_y = (flags & SPRITE_FLIP_Y) != 0;
    const uint16_t flip_eor = (uint16_t)((flip_x ? 0x4000 : 0) | (flip_y ? 0x8000 : 0));

    for (int i = 0; i < meta.count; i++) {
      const SpritePiece* p = &meta.pieces[i];
      uint16_t sy = (uint16_t)p->y;
      if (flip_y) sy = ws_mirror(sy);
      sy = (uint16_t)(sy + (uint16_t)oy);
      uint16_t sx = (uint16_t)p->x;
      if (flip_x) sx = ws_mirror(sx);
      sx = (uint16_t)(sx + (uint16_t)ox);
      const uint16_t attrs = (uint16_t)((((p->attr & attr_and) | attr_or) ^ flip_eor) & 0xfe00);
      int tiles[2], tile_count = 0;
      const uint16_t was = ws_r16(mem, W_FRAME_SLOT + (uint32_t)p->frame * 2);
      const uint16_t now = ws_r16(snes->ram, W_FRAME_SLOT + (uint32_t)p->frame * 2);
      if (!(was & 0x8000)) tiles[tile_count++] = sprite_slot_tile(was / 2);
      if (!(now & 0x8000)) tiles[tile_count++] = sprite_slot_tile(now / 2);

      for (int e = 0; e < OAM_ENTRIES; e++) {
        if (screen[e] || ws->owner_rec[e] >= 0) continue;
        const uint16_t lo = snes->ppu->oam[e * 2], word = snes->ppu->oam[e * 2 + 1];
        const int x = (lo & 0xff) | ((snes->ppu->highOam[e >> 2] >> ((e & 3) * 2)) & 1) << 8;
        if (x != (sx & 0x1ff) || (lo >> 8) != (sy & 0xff) || (word & 0xfe00) != attrs) continue;
        bool tile_ok = tile_count == 0;
        for (int t = 0; t < tile_count; t++) tile_ok |= (word & 0x1ff) == tiles[t];
        if (tile_ok) screen[e] = true;
      }
    }
  }
}

// The owner table of the picture on screen with the margins' sprites in it,
// for whoever moves sprites between ticks (`layers_link` in `layers.h`).
//
// **A sprite in a margin belongs to somebody too.** The pictures between two
// ticks ease each sprite from where it was, and find where it was by whose it
// is: the record it was drawn from, and how far that record's origin moved.
// The game's pass says so for the sprites it places. The ones put back here
// said nothing, and were paired by standing nearest to a sprite of the last
// tick that had said nothing either -- so a piece crossing the console's edge,
// a record's on one side and nobody's on the other, had no last place at all
// and was drawn for a tick where it had got to, while the ground under it was
// still being eased there. With the camera moving that is a jump ahead of the
// ground and back on to it, at the inner edge of a margin, once for every
// thing that crosses it: reported in play-testing as the keys and the other
// pickups flickering in the margins while walking, and never in the middle of
// the screen. A walker's pieces did it again at each change of animation
// frame, which moves a piece further than "nearest" was allowed to look.
//
// `held` is the game's table for this picture, or NULL if its pass did not
// run. Nothing is said then either: a picture with no table is paired by the
// look of its sprites, all of them.
static inline const SpriteOamOwners* ws_owners(const Widescreen* ws, const SpriteOamOwners* held,
                                               SpriteOamOwners* out) {
  if (!held || !ws || ws->margin <= 0) return held;
  *out = *held;
  for (int s = 0; s < OAM_ENTRIES; s++) {
    if (ws->owner_rec[s] < 0) continue;
    out->rec[s] = ws->owner_rec[s];
    out->ox[s] = ws->owner_ox[s];
    out->oy[s] = ws->owner_oy[s];
  }
  return out;
}

static inline void ws_place_screen_sprites(Snes* snes, Widescreen* ws, int place) {
  const uint8_t* mem = ws_sprite_mem(ws);
  if (place == ppu_spriteWorld) {
    for (int s = 0; s < OAM_ENTRIES; s++) snes_setSpritePlace(snes, s, ppu_spriteWorld);
    return;
  }
  const SpriteOamOwners* owners = ws_pass_on_screen(snes, ws);
  bool by_look[OAM_ENTRIES];
  if (!owners) ws_screen_by_look(snes, ws, by_look);
  for (int s = 0; s < OAM_ENTRIES; s++) {
    const int rec = owners ? owners->rec[s] : -1;
    const bool screen = owners ? rec >= 0 && (ws_r16(mem, (uint32_t)rec + ACTOR_FLAGS) & ACTOR_SCREEN_SPACE) != 0
                               : by_look[s];
    snes_setSpritePlace(snes, s, screen ? place : ppu_spriteWorld);
  }
}

// Called at the top of every frame, before any of it is drawn — see
// `SnesFrameHook`. At that moment the game's vblank has finished: this frame's
// tilemap columns are in VRAM, its OAM has been DMA'd, and the picture is fixed
// The Konami logo's first act is a star drawn across the screen with a line
// behind it, and it is BG1: a map of 16x16 tiles, 64 columns of them, whose
// second row is sixteen tiles of line, the star, and then black; the game
// scrolls it from 256 to 0 and the star crosses the console. `ppu_wideAuto`
// takes a 64-column map outside a level for one the game maintains 32 columns
// of and repeats the console's 256 -- so the right margin had a second line
// running through it, and at the end a second star, while the left margin had
// the black from the console's right. It is told by what the map holds, which
// is in video memory from before the screen is lit until the logo is gone.
// Which columns of its plane the big figure is in, in pixels: `[*x0, *x1)`.
// The figure is written into the plane's first screen and is 20 tiles at its
// widest and tallest, so the screen's last word is the blank one. False for a
// plane with nothing in it.
static inline bool ws_boss_extent(const Snes* snes, int* x0, int* x1) {
  const Ppu* ppu = snes->ppu;
  const uint16_t base = ppu->bgLayer[0].tilemapAdr;
  const uint16_t blank = ppu->vram[(base + 0x3ff) & 0x7fff] & 0x3ff;
  int c0 = 32, c1 = -1;
  for (int r = 0; r < 32; r++)
    for (int c = 0; c < 32; c++)
      if ((ppu->vram[(base + r * 32 + c) & 0x7fff] & 0x3ff) != blank) {
        if (c < c0) c0 = c;
        if (c > c1) c1 = c;
      }
  if (c1 < 0) return false;
  *x0 = c0 * 8;
  *x1 = (c1 + 1) * 8;
  return true;
}

// The big figure's plane, parked where the game did not park it and put back
// where it did, for a picture wider than the one the game parks it for. See
// the header. `$82:8209`'s own tests are
//
//     LDA $1B6A : SEC : SBC $1E6E : CMP #$0100 : BCC use
//     CMP #$FF01 : BCC park       ; and #$FF21 for the other axis
//
// and these are the same two decisions taken against what can be shown: the
// picture, and `WS_CAPTURE_SLACK` columns either side of it, which the smoothing
// captures with each plane and slides into view when it eases a scroll
// (`LAYERS_MARGIN`). A figure, or a lap of one, left in those columns is one
// that flickers at the picture's edge as the boss walks.
static inline void ws_boss_plane(Snes* snes, const Widescreen* ws, int left, int right) {
  BgLayer* bg = &snes->ppu->bgLayer[0];
  int x0, x1;
  if (!ws_boss_extent(snes, &x0, &x1)) return;  // a level with no big figure
  const int lo = -left - WS_CAPTURE_SLACK, hi = 256 + right + WS_CAPTURE_SLACK;

  if ((bg->hScroll & 0x3ff) != WS_BOSS_PARKED || (bg->vScroll & 0x3ff) != WS_BOSS_PARKED) {
    // Not parked. The origin's column, off the register: all ten bits of it,
    // because the register is not always the job's. On a frame the game is
    // late for, the job does not run and the register still holds what was
    // written here the frame before, for an origin past 255 -- which nine
    // bits, the plane's 512, read as one far off to the *left*, and parked:
    // the figure gone from the margin for a frame whenever the game dropped
    // one. The job's own origins are -255..255. Once the figure is out of
    // sight to the left and its next lap, 512 further right, is not, the plane
    // is parked as the job parks it.
    int sx = (1024 - (bg->hScroll & 0x3ff)) & 0x3ff;
    if (sx >= 512) sx -= 1024;
    if (sx + x1 <= lo && sx + 512 + x0 < hi) bg->hScroll = bg->vScroll = WS_BOSS_PARKED;
    return;
  }
  // Parked. Where the job would have put it is worked out from the words it
  // read, and there are two sets of those to try. It ran in the vblank that
  // has just ended, and nearly always the game's tick was over by then, so
  // the machine's memory as it stands is what it read: on `level25-lane`, of
  // 4,491 frames with the plane on the console the scroll is the live words'
  // on 4,446 (and the tick-old copy's on 1,591, the ones where nothing
  // moved). But now and then the tick that follows has already moved the
  // figure by line 0 -- it turns round, and its origin jumps -- and the live
  // words then say "on the console" of a plane the job parked, which cannot
  // be what it read. That was the figure gone from the margin for one frame
  // in every few hundred. So: the first of the two that agrees with the job
  // having parked it.
  for (int pass = 0; pass < 2; pass++) {
    const uint8_t* mem = pass ? ws->mem : snes->ram;
    const uint16_t dx = (uint16_t)(ws_r16(mem, W_CAMERA_X) - ws_r16(mem, WS_BOSS_PLANE_X));
    const uint16_t dy = (uint16_t)(ws_r16(mem, W_CAMERA_Y) - ws_r16(mem, WS_BOSS_PLANE_Y));
    const bool job_kept_x = dx < 0x0100 || dx >= 0xff01;
    const bool job_kept_y = dy < 0x0100 || dy >= 0xff21;
    if (job_kept_x && job_kept_y) continue;  // not what the job read
    // The origin's screen column is `-dx`. The game keeps -255..255; this is
    // for an origin further right whose figure still reaches what can be
    // shown -- and whose *last* lap, 512 to the left, does not: at the end of
    // a map the whole of both margins is on the right, and the saucer is wide
    // enough for its far end to come round onto the console's first columns
    // before its near end has left the margin. Then it stays parked; a ghost
    // is worse than a figure that arrives a few columns in.
    const int sx = -(int)(int16_t)dx;
    if (sx < 256 || sx + x0 >= hi || sx - 512 + x1 > lo) return;
    if (!job_kept_y) return;  // off the top or the bottom: parked is right
    bg->hScroll = dx & 0x3ff;
    bg->vScroll = dy & 0x3ff;
    return;
  }
}

// How the extra width is shared between the two sides: equally, until the
// world ends sooner on one of them, and then the other takes what is left.
// See "Where the extra width goes" in `widescreen_frame`.
static inline void ws_split_margins(int cam_x, int map_cols, int margin, int* left_out, int* right_out) {
  const int room_left = cam_x;
  const int room_right = map_cols * 8 - (cam_x + 256);
  int left = margin < room_left ? margin : room_left;
  int right = 2 * margin - left;
  if (right > room_right) {
    right = room_right > 0 ? room_right : 0;
    left = 2 * margin - right;
  }
  if (left < 0) left = 0;
  *left_out = left;
  *right_out = right;
}

static inline bool ws_konami_sweep(const Snes* snes) {
  const Ppu* ppu = snes->ppu;
  const BgLayer* bg = &ppu->bgLayer[0];
  if (ppu->mode != 1 || !bg->bigTiles || !bg->tilemapWider) return false;
  const uint16_t* row = &ppu->vram[(bg->tilemapAdr + 32) & 0x7fe0];
  return (row[0] & 0x3ff) == 0x004 && (row[15] & 0x3ff) == 0x004 && (row[17] & 0x3ff) == 0x002;
}

// but for the columns the console never had. Everything read here comes from
// `ws->mem`, the memory that picture was composed from, which is a tick behind
// the memory the game is running on now. The sprites read the sprite pass's
// own copy instead (`ws_sprite_mem`), which is the same thing on a tick that
// finished in time and the right thing on one that did not.
static inline void widescreen_frame(Snes* snes, Widescreen* ws) {
  const uint8_t* mem = ws->mem;
  const int margin = ws->margin;
  // Both halves, and see the note at the top of this file on why the width
  // alone is not enough: BG2SC keeps its 64 columns across the cards between
  // two levels, and only `$212C` says the world has stopped being drawn.
  const bool in_level =
      snes_bgTilemapWider(snes, 1) && snes_bgOnMainScreen(snes, 1);
  // BG3 is the status panel in a level and everything else outside one --
  // except at a game over, when the game scrolls a 256-wide mask over the
  // panel's map: "GAME OVER" cut out of a purple field, the level showing
  // through the letters (`$80:8A00`). Split like the panel, the mask left the
  // middle third of the picture bare; centred in the picture and carried to
  // its edges instead (`ppu_wideCentre`) it is whole. Centred, not left in
  // the console's place: at the end of a map the margins are not the same
  // width, and there the console's place is off to one side. Which of the two
  // BG3 is carrying is what the main game thread is doing: parked inside the
  // game over routine with the mask's scroll under way (`ws_game_over`), it
  // is the mask. There are two ways into that routine, and the level loop
  // (`$80:8516`) takes them differently: a player's last life clears their
  // panel flag (`hud_panel_on`, `$80:CEDA`) and the loop returns when both
  // are down; the last neighbour lost with none rescued returns too, and the
  // player's flag stays up. The flags were the tell before, and the second
  // way -- the only one open under `--invincible` -- split the mask again.
  // (Two more tells were tried and were wrong: the scroll shadow alone,
  // `$136A`, which stays where it stopped into the next game, whose panel was
  // then centred and carried out to the edges, half a health bar and all; and
  // the layer's own columns, which the mask's first drips come in over, so
  // the curtain was split for its first hundred frames.)
  const bool bg3_mask = in_level && ws_game_over(mem);
  // Outside a level BG3 is the wallpaper behind the LucasArts logo, the title
  // and the character select, which the game steps along every few ticks --
  // and `ppu_wideAuto` continues a 256-pixel map into the margins only once it
  // has seen it move. The select's comes up at scroll 0 and first steps on its
  // fifth tick, so for the first five ticks of the fade-in the margins were the
  // flat colour behind it, lighter than the wallpaper: a flash down both edges.
  // A BG3 filled down both of the console's edge columns is a field, not a card
  // with writing on it, and is repeated from the first frame.
  const bool bg3_field = !in_level && ppu_columnFilledAt(snes->ppu, 2, 0) &&
                         ppu_columnFilledAt(snes->ppu, 2, 255);
  snes_setLayerWide(snes, 2, bg3_field ? ppu_wideTile
                             : !in_level ? ppu_wideAuto : bg3_mask ? ppu_wideCentre : ppu_wideAnchor);
  // (The sprites laid out over BG3 -- the radar's markers, the mask's drips
  // -- go the same way: `ws_place_screen_sprites`, at the end.)
  // BG2 is the scrolling world, and its margins are filled below, so it is the
  // one layer whose continuation is known rather than guessed at.
  snes_setLayerWide(snes, 1, in_level ? ppu_wideStretch : ppu_wideAuto);
  // Sprites are world things in a level and stagecraft outside one. A title
  // card slides its letters in from off the side of the console's 256 and
  // relies on that edge to hide them; widening the picture without saying this
  // shows the trick, as the same words a second time at both edges.
  snes_setLayerWide(snes, 4, in_level ? ppu_wideStretch : ppu_wideClip);
  // BG1 outside a level is the one background that is neither: see
  // `ws_konami_sweep`. In a level it is the big figure's plane, 512 pixels of
  // the game's own with a boss in one corner of it or nothing at all, and it
  // goes on into the margins as the world does -- see the header.
  snes_setLayerWide(snes, 0, in_level ? (snes_bgTilemapWider(snes, 0) ? ppu_wideStretch : ppu_wideAuto)
                             : ws_konami_sweep(snes) ? ppu_wideSweep : ppu_wideAuto);

  if (!in_level || margin <= 0) {
    memset(ws->owner_rec, 0xff, sizeof ws->owner_rec);
    // Nothing outside a level has a map to run off the end of.
    snes_setWidescreen(snes, margin, margin);
    snes_setWideClamp(snes, -PPU_EXTRA_MAX, 255 + PPU_EXTRA_MAX);
    ws->screen_place = ppu_spriteWorld;
    ws_place_screen_sprites(snes, ws, ws->screen_place);
    return;
  }

  const uint16_t cam_x = ws_r16(mem, W_CAMERA_X);
  const uint16_t cam_y = ws_r16(mem, W_CAMERA_Y);
  const int cam_tx = cam_x >> 3, cam_ty = cam_y >> 3;
  const int cursor_x = ws_r16(mem, W_TILEMAP_CURSOR_X);
  const int cursor_y = ws_r16(mem, W_TILEMAP_CURSOR_Y);
  const uint16_t vram_base = ws_r16(mem, W_TILEMAP_VRAM_BASE);
  const uint16_t threshold = ws_r16(mem, W_TILE_PRIORITY_BELOW);
  // One map row in bytes, so half of it is the map's width in tiles. This is
  // the only measurement of the world's size taken here, and both the clamp
  // below and the column bounds come out of it.
  const int map_cols = ws_r16(mem, W_TILEMAP_ROW_BYTES) >> 1;

  // ## Where the extra width goes
  //
  // The camera stops at the edges of the map, because it was written for a
  // 256-pixel window and that window is the whole of what the game thinks is on
  // screen. Hang 43 more pixels off each side of it and at either end of a level
  // the picture leaves the world: nothing to draw, nothing to fill it with, and
  // the actors out there quite correctly do not exist.
  //
  // The answer is not to black it out and not to touch the camera. It is to
  // stop insisting the two margins be equal. The picture is always the same
  // width; when the left margin cannot have its 43 pixels because the world
  // starts sooner, the right margin takes what is left over. Walking to the far
  // west of a map, the view slides to a stop against the world's edge while the
  // player carries on to it — which is what every game that has ever had a
  // camera does at the end of a level, and it costs nothing but the picture no
  // longer being centred on a camera that was never centred on the player
  // either.
  int left, right;
  ws_split_margins(cam_x, map_cols, margin, &left, &right);
  snes_setWidescreen(snes, left, right);
  // ...and if the map is narrower than the whole picture, neither margin can be
  // filled and the total has to stay put or the framebuffer would change size
  // frame to frame. The backdrop covers whatever is left, from the map's two
  // edges in picture columns: world x is `cam_x` plus screen x, so world 0 sits
  // at `-cam_x` and the last map pixel at the far end of the last column.
  snes_setWideClamp(snes, -(int)cam_x, map_cols * 8 - 1 - (int)cam_x);

  // The columns either side of the game's 32 that the picture reaches, and
  // `WS_CAPTURE_SLACK` past its edges for the smoothing. No more: the ring is
  // 64 columns, and at 21:9 the widest either margin can get, filled on both
  // sides, comes round onto the other side. See the header.
  const int lo = (int)cam_x - left - WS_CAPTURE_SLACK;
  const int cols_left = cam_tx - (lo >= 0 ? lo / 8 : -((7 - lo) / 8));
  const int cols_right = ((int)cam_x + 255 + right + WS_CAPTURE_SLACK) / 8 -
                         (cam_tx + CAMERA_WINDOW_TILES_X - 1);
  // The camera shows 28 rows and a fraction, and the fraction is a row.
  const int rows = CAMERA_WINDOW_TILES_Y + 1;

  for (int side = 0; side < 2; side++) {
    for (int i = 0; i < (side ? cols_right : cols_left); i++) {
      // Left of the window, then right of it. `CAMERA_WINDOW_TILES_X` is where
      // the game's own 32 columns end and these begin.
      const int col =
          side ? cam_tx + CAMERA_WINDOW_TILES_X + i : cam_tx - 1 - i;
      if (col < 0 || col >= map_cols) continue;  // off the end of the world

      // Which slot of the 64-column ring that map column belongs in. The game
      // keeps `W_TILEMAP_CURSOR_X` as the slot holding `W_CAMERA_TILE_X`, and
      // everything else is that offset by the distance between the two.
      const int slot = (cursor_x + (col - cam_tx)) & TILEMAP_CURSOR_MASK_X;
      // A 64x32 tilemap is two 32x32 screens side by side, the right one a
      // fixed word offset away — the same split `camera_split_x` works in.
      const uint16_t slot_word =
          (uint16_t)((slot & TILEMAP_SCREEN_MASK) |
                     (slot >= CAMERA_SPLIT_COLUMNS ? TILEMAP_SECOND_SCREEN : 0));

      for (int j = 0; j < rows; j++) {
        const int row = cam_ty + j;
        // Rows cannot leave the map: the camera's own limit stops it 28 rows
        // short of the bottom, and this adds nothing vertically.
        const uint16_t row_base =
            ws_r16(mem, (uint32_t)(W_TILE_ROW_BASE + row * 2));
        uint16_t tile = ws_r16(
            mem, ((uint32_t)(TILEMAP_SRC_BANK & 1) << 16) |
                     (uint16_t)(row_base + col * 2));
        // The priority rule the ROM's column copy applies, and for the same
        // reason: a tile below the level record's threshold is drawn in front
        // of whatever walks over it.
        if ((tile & TILEMAP_COPY_MASK) < threshold) tile |= TILEMAP_PRIORITY_BIT;

        const int ring_row = (cursor_y + j) & TILEMAP_CURSOR_MASK_Y;
        snes_writeVramWord(
            snes, (uint16_t)(vram_base + slot_word + ring_row * 32), tile);
      }
    }
  }

  if (snes_bgTilemapWider(snes, 0)) ws_boss_plane(snes, ws, left, right);
  ws_margin_sprites(snes, ws, left, right);
  ws->screen_place = bg3_mask ? ppu_spriteCentred : ppu_spriteAnchored;
  ws_place_screen_sprites(snes, ws, ws->screen_place);
}

// Move the game's own distances from the console's edges out to the edges of
// the picture that is actually being drawn. Idempotent, and applied every frame
// because `F4` can change the margin between two of them and the camera can
// change how it is shared; at margin zero it writes the stock figures back and
// the game is the game again. Each word is guarded by the opcode in front of
// it, which is not what gets written, so the guard stays true after a patch
// and is false for any ROM that does not have this code there.
//
// The neighbours' window follows the picture: its middle is the picture's and
// its reach is half the picture and the console's 32 pixels, so it ends 32
// past each edge that is drawn wherever the two margins are -- see the header
// for why. The margins are worked out from the machine's memory as it stands,
// not `ws->mem`: this is for the tick about to run, not the picture just made.
//
// The wings (`WS_ROM_WORDS`) are moved by twice the margin, not the margin.
// The two margins slide -- at the end of a map the side with no world left to
// show gives its pixels to the other one -- so either of them can be the whole
// `2 * margin` at once. A monster put down that far out is out of sight
// wherever the margins are, and all it costs him is a longer run.
static inline void ws_rom_word(Cart* cart, uint32_t guard_at, uint8_t guard, uint32_t word_at, uint16_t want) {
  if (cart->romSize <= word_at + 1 || cart->rom[guard_at] != guard) return;
  cart->rom[word_at] = (uint8_t)want;
  cart->rom[word_at + 1] = (uint8_t)(want >> 8);
}

static inline void ws_widen_window(Snes* snes, int margin) {
  Cart* cart = snes->cart;
  if (!cart || !cart->rom) return;
  int left = 0, right = 0;
  if (margin > 0) {
    const uint8_t* mem = snes->ram;
    ws_split_margins(ws_r16(mem, W_CAMERA_X), ws_r16(mem, W_TILEMAP_ROW_BYTES) >> 1, margin, &left, &right);
  }
  // `left + right` is `2 * margin`, so their difference is even.
  ws_rom_word(cart, WS_WINDOW_MIDDLE_OPCODE, 0x69, WS_WINDOW_MIDDLE_OPCODE + 1,
              (uint16_t)(WS_WINDOW_MIDDLE_STOCK + (right - left) / 2));
  ws_rom_word(cart, WS_WINDOW_REACH_OPCODE, 0xc9, WS_WINDOW_REACH_OPCODE + 1,
              (uint16_t)(WS_WINDOW_REACH_STOCK + (left + right) / 2));
  for (size_t i = 0; i < sizeof WS_ROM_WORDS / sizeof WS_ROM_WORDS[0]; i++) {
    const WsRomWord* w = &WS_ROM_WORDS[i];
    ws_rom_word(cart, w->guard_at, w->guard, w->word_at, (uint16_t)(w->stock + w->margins * margin));
  }
  // The cull, only where the picture's edge has passed it: 21:9 at the end of
  // a map. Stock everywhere else.
  int behind = left + WS_CULL_SLACK, ahead = 256 + right + WS_CULL_SLACK;
  if (behind < WS_CULL_BEHIND_STOCK) behind = WS_CULL_BEHIND_STOCK;
  if (ahead < WS_CULL_AHEAD_STOCK) ahead = WS_CULL_AHEAD_STOCK;
  ws_rom_word(cart, WS_CULL_BEHIND_OPCODE, 0xc9, WS_CULL_BEHIND_OPCODE + 1, (uint16_t)(0x10000 - behind));
  ws_rom_word(cart, WS_CULL_AHEAD_OPCODE, 0xc9, WS_CULL_AHEAD_OPCODE + 1, (uint16_t)ahead);
}

static inline void widescreen_hook(Snes* snes, void* ctx) {
  Widescreen* ws = (Widescreen*)ctx;
  ws_widen_window(snes, ws->margin);
  // The first frame has no tick before it to have been composed from, and the
  // game has drawn nothing yet either.
  radar_return(snes, &ws->radar);
  if (ws->have_mem) {
    ws_return_slots(snes, ws);
    widescreen_frame(snes, ws);
    radar_frame(snes, &ws->radar, &ws->rom, ws_sprite_mem(ws), ws->screen_place);
  }
  blood_frame(snes, &ws->blood);
  memcpy(ws->mem, snes->ram, sizeof ws->mem);
  ws->have_mem = true;
}

// The two moments the sprites on screen are fixed at, both the game's own, and
// both heard through `cosim_watch` so that the ROM's pass and the port's are
// heard alike: the `RTL` that ends `sprite_build_oam`, and the vblank job that
// sends OAM (`sprite_upload_flush`, every NMI). The pass's memory is what its
// buffer was built from, and the transfer is when that buffer becomes the
// picture; a pass the NMI cuts into is not sent until it has ended, so the copy
// sent is always the newest pass to have finished.
//
// The job is heard at its entry. The transfer itself is `$80:B99B`, and the
// ROM reaches it from there with nothing between but the frames' uploads, but
// the port stands in for the whole job, so under `run` nothing ever executes
// that instruction. Its entry is reached either way, and no pass can end
// between the two inside one NMI.
#define WS_PASS_DONE_AT 0x80bde2u
#define WS_OAM_SENT_AT 0x80b947u

static inline void widescreen_pass_done(Snes* snes, void* ctx) {
  Widescreen* ws = (Widescreen*)ctx;
  memcpy(ws->pass_mem, snes->ram, sizeof ws->pass_mem);
  ws->pass_new = true;
}

// A tick the game runs long sends the same OAM again, so a send with no pass
// behind it keeps the copy it has. Only for a few: anything that ever built
// OAM without `sprite_build_oam` would otherwise leave the margins drawn from
// a moment that had stopped moving, and the top-of-picture copy is the older
// behaviour and a safe one.
#define WS_PASS_STALE_SENDS 8

static inline void widescreen_oam_sent(Snes* snes, void* ctx) {
  (void)snes;
  Widescreen* ws = (Widescreen*)ctx;
  if (!ws->pass_new) {
    if (++ws->sends_stale >= WS_PASS_STALE_SENDS) ws->have_sprite_mem = false;
    return;
  }
  ws->pass_new = false;
  ws->sends_stale = 0;
  memcpy(ws->sprite_mem, ws->pass_mem, sizeof ws->sprite_mem);
  ws->have_sprite_mem = true;
}

// After a quick load: both copies belong to the machine that is gone, and the
// top-of-picture copy stands in until the next pass is sent. They are not in
// the save, so a save made before them still loads.
static inline void widescreen_forget_passes(Widescreen* ws) {
  ws->have_sprite_mem = false;
  ws->pass_new = false;
  ws->sends_stale = 0;
}

// Hang the above off the machine's frame start. `ws` must outlive `snes`.
static inline void widescreen_install(Snes* snes, Widescreen* ws,
                                      const uint8_t* rom, int rom_len,
                                      int margin) {
  memset(ws, 0, sizeof *ws);
  ws->rom.data = rom;
  ws->rom.size = (uint32_t)rom_len;
  ws->margin = margin;
  ws->radar.steady = true;
  snes_setFrameHook(snes, widescreen_hook, ws);
}

#endif
