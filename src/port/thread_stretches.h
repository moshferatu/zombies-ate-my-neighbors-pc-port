// Stretches of threads, between their calls.
//
// Most of these threads have their loop ported already, a pass at a time.
// What was left was whatever a pass cannot hold: a thread's first
// instructions, its end, and a pass that sleeps in the middle. Others are
// short enough to be stretches and nothing else. A stretch is from where the
// thread starts or a call comes back to the next call, sleep or return.
//
// ## The weed, `$81:D28C`
//
// See `port/weeds.h`. On its guard it snaps on a draw, and `weed_frame`
// leaves at that draw. From there:
//
//   $81:D154  who is nearest, however far, is asked for
//   $81:D15F  kept at `$32`, and four pictures shown, which sleeps
//   $81:D168  three seeds thrown at them, five ticks apart, each a thread
//             begun beside the root: eight to the left, eight to the right
//             and two to the left, with a picture of the weed's own before
//             the second and the third
//   $81:D1DE  its first picture again, and it rests forty-five passes
//   $81:D2AD  the end of a pass: it sleeps two ticks, or its fate is looked
//             at
//
// Its fate positive, it only ends. Negative, who killed it is paid and
// counted, which is `port/begin.h`'s, and after its last pictures its entry
// in the level's list is marked.
//
// It begins at `$81:D2F1` or `$81:D2F9`, which differ in the bits its tiles
// have: `$1400` and `$0800`. The level's load goes up by `$23`, it takes a
// record, and its four arms begin at the root. Its health is twenty-five.
//
// ## The follower, `$82:F49E`
//
// See `port/follower.h`. Every sixteenth frame it looks at the tile it is
// over: on the level, and with bit 7 of the tile's attributes set, it
// sleeps a frame and puts tile `$150D` there. That is the tile a weed can
// take, by the same bit.
//
// Told that something touched it, it shows one last picture for two
// frames: `$82:F3F4`.
//
// It begins only if the level has room for it. Its load is ten, and it is
// put to the left or the right of what it follows by the sign of the word
// at `$00` on its page.
//
// ## The main thread, `$80:84B1`
//
// Between its calls it is mostly calls, which are links. What is not:
//
//   $80:85E1  a game's first words: ten neighbours and its first level.
//             Have the top scores been set to what they begin as?
//             `$7E:2124` says
//   $80:8600  ...which they now have
//   $80:8615  the sound's bank sent, once: `$18` on its page
//   $80:8849  the level's record asked for, from the two words at
//             `$9F:0036` by the level's offset
//   $80:8525  a level faded in: a count at `$1FB6` goes up
//   $80:867B  the level's load cleared, and the neighbours a level begins
//             with: as many as the two players have saved, in decimal
//   $80:C04C  the sprite cache's thread asked for
//
// And three that are not its own: where the front end begins,
// `$80:9126`; a word a player's thread keeps before it asks for its
// record, `$80:D16E`; and the first two sleeps of the thread that puts
// things in a level, `$80:C8F9`, which waits for an odd frame.
//
// ## The martian, `$81:99DF`
//
// See `port/martian.h`. The walker's first instructions, either side of
// `martian_begin`, the end of that, and the twelve ticks a shot waits.
//
// **One that comes in over the top starts at `$81:9A3E`**, eight pixels
// below the top of the screen and at whatever X it was given. If that place
// is in the ground or off the level there is no martian: the thread ends
// before it has a record.
//
// ## The werewolf, `$81:ABF5`
//
// See `port/werewolf.h`. Its thread ends at once unless the word at `$1F94`
// is set. Otherwise it weighs 28 in the level's load, howls if the level's
// sample set is the one with its howl in it, takes a record, sleeps twenty
// ticks, and begins running with ten for what it can be hit by. Then what
// follows the end of a strike and a crouch's pictures, and the rest after a
// landing.
//
// **The check of the sample set is a stretch of its own**, `$81:AC05`. The
// frontend plays the howl itself on a level with another set, and knows to
// by watching the 65816 come to that instruction: see `src/bank_sfx.h`. A
// stretch that ran on through it would silence that, and nothing here would
// notice. So the one before stops there, and this one begins there.
//
// ## The chainsaw maniac's start, `$81:983A`
//
// See `port/chainsaw.h`. Its weight on the level's load, seven passes to its
// first picture, and a record asked for. Then:
//
//   $81:9733  its place kept, in pixels and in quarters of one, a quarter
//             turn to the right, its picture, its record drawn, its
//             attributes, health of 151, no saw, and the state that wanders
//   $81:9854  its entry in the level's list marked
//   $81:985A  its sound if the level has no samples of its own, and its
//             first pictures
//   $81:9820  ...which come back here after the sound
//   $81:9827  what can touch it, and its handler
//   $81:986F  the first sleep
//
// ## A player stung, `$80:F9D0`
//
// See `port/collide.h`: what touches a player with id `$0A` gives them this
// to do next. With no health they only return. Otherwise:
//
//   $80:F9D0  a point of health gone, and sound `$19`
//   $80:F9E2  a picture by the way they face, and they are no longer told
//             what touches them
//   $80:F9F2  ...and nothing can, for 75 ticks
//
// ## The bolt's start, `$81:F808`
//
// See `port/bolt.h`. Its weight on the level's load, two, and sound `$15`;
// then a record, by `JSR $F89D`:
//
//   $81:F8A1  the record, two pixels up at the place it was given, its
//             flags and first picture by the way it goes, what it hits by
//             the player it is from, nothing told it yet, sixty frames, and
//             its handler; and back, to its first sleep
//
// ## The fourth's tile, `$80:EA02`
//
// See `port/walk.h`. The walk stops at the block that breaks the tile open,
// and from there to the sleep after:
//
//   $80:EA02  ten pixels across from the player and twelve up, and the way
//             they face, to `$82:DC57`, which draws a byte at random
//   $82:DC5F  ...and looks it up at `$81:E79F`. `$FF` is nothing, `$FE` a
//             thread at `$82:DCA0` beside the player when they face one of
//             the ways it wants, and anything else a thing of that kind put
//             back in the level's list, `object_put_back`, at that place
//
// Each comes back to the `RTL` there, and the thread sleeps ten ticks.
//
// ## The monster potion, `$80:D9A3`
//
// See `port/player.h`. The ordinary frame stops at the potion's `JMP`, and
// from there the change is calls, and three stretches between them:
//
//   $80:D9AC  sound 5, after a tick's rest
//   $80:D9B3  the pictures of the change, by the player, which sleep
//   $80:D9BC  ...and after them, the monster's words: 2,000 frames, the
//             pictures that are its own, no weapon out and nothing pressed
//             before, facing down, state 2 and `$80:DA54` for what ends it.
//             Then the weapon and the item it holds, `$0E` and `$0C`, by the
//             swap of what the player held, and to `$80:D4E9` for its pose
//
// That comes back to the ordinary frame's `JSR` of its third button.
//
// ## The monster's punch, standing, `$80:EF67`
//
// See `port/pose.h`. The pose stops at the sound, and after it:
//
//   $80:EF82  `JSR $F051`, the fist's box, which on the first picture is
//             nobody's. Then the fist's tile, which is a call
//   $80:EF8F  after four ticks, the second picture
//   $80:EF98  ...and the box for it, which tells those in it: to the `JSL`
//   $80:EFAA  four more, and the third picture's box, which is the same
//             picture
//   $80:EFB9  four more, and the cycle back to nought: the pose's `RTS`
//
// ## A player out by the exit, `$80:FAF6`
//
// See `port/collide.h`: what the exit touches is given this to do next.
//
//   $80:FAF6  the state the player is in, if it has a routine to end it at
//             `$72`, is ended first: by `RTS` to it, with `$80:FAFF` under
//   $80:FB00  ...and back here: the player's 28 off the level's load, and
//             they can no longer be touched or told anything. Below nought
//             the ROM spins where it is, and the two stretches are turned
//             down. Then the weapon put away and a tick's rest
//   $80:FB18  their side of the panel blanked
//   $80:FB1E  a picture of nothing, and the player out. Eighty ticks
//   $80:FB35  ...and with two players' records, theirs is let go. Then a
//             sleep that does not end
//
// The monster's routine, `$80:DA54`, asked by the exit, has nothing to show:
// it is only the monster's words cleared, and the hands given back, and to
// `$80:D4E9` for a pose. That comes back to `$80:FB00`.
//
// ## The exit door, `$82:F70B`
//
// Begun when a level's way out opens, with a player's record at `$04` and
// which player at `$06`. Between its calls:
//
//   $82:F70B  `JSR $F79E`, which sleeps a tick
//   $82:F7A5  ...and asks whether the player stands on solid ground
//   $82:F7B7  ...and if not, sleeps again. If so: nobody in the game ends
//             the thread; a player who is already out hands the search to
//             the other. It starts at the way the player is going, eight
//             ways in all, each 32 pixels off, and asks of each
//   $82:F7FA  ...the ground there, and 16 above, 32 above and 16 below
//   $82:F827  ...the map's edge, anyone there, and how far the other player
//             would be
//   $82:F848  ...and the tile there, three above and six above, each of which
//             has to be one drawn over sprites
//   $82:F88B  The first way that passes is where the door goes, and its
//             weight is added. Eight ways that do not pass, and the search
//             sleeps a tick and starts again.
//   $82:F8DD  its record made there, with no handler yet
//   $82:F929  ...and then one that can be touched; every other tick:
//   $82:F724  touched by both players, or nobody to rescue left, and it
//             waits ten ticks; nobody in the game, and it goes; while there
//             is somebody to rescue, it waits on
//   $82:F744  ...and after the ten ticks, if the player who touched it last
//             is out, it goes; if not, it waits on
//             When it goes its handler is taken and it shows its last
//             pictures. Its end, `$82:F767`, is `port/begin.h`'s.
//
// ## Five ends
//
// Where a creature's frame leaves at its end, or a port in the middle of
// one, the rest round its calls. What each end reaches is a port already:
// `port/begin.h`'s scores, counts and record ends, and the lists of
// pictures.
//
//   $81:CCF7  a slime's fate: dead with nothing to pay goes straight to its
//             end; otherwise it is counted and its killer paid
//   $81:F1DB  a decoy's last tick: one fewer out, and no handler
//   $81:AC2A  a werewolf's death: no handler,
//   $81:AC32  ...and its second record freed, if it has one
//   $81:9887  the chainsaw maniac's death: no handler,
//   $81:988F  ...counted and its killer paid,
//   $81:989B  ...counted by the side that killed it, nothing to touch, and
//             its last pictures
//   $81:8EF0  the zombie of `$81:8ED5`: its record freed,
//   $81:8EF6  ...and its weight given back
//
// ## A player out, `$80:CED5`
//
// The death's count (above) finds no lives left and calls `$80:F354`, the
// tick's rest six other places call too:
//
//   $80:F354  the player's flag cleared, and a tick's sleep
//   $80:F360  ...and cleared again, back to whoever called; the slime's own
//             is the row before
//   $80:CED8  the player out of the game, and its rescues none
//   $80:CEE6  With neighbours still to save, or some saved, and the other
//             player in, it ends: its weight given back and its record
//             filed as none. Otherwise its pictures play for good
//             (`port/bodies.h`), or on the sentinel's page it sleeps for good
//   $80:CF2B  ...its two records freed
//   $80:CF37  ...its first weapon and its tenth item, if it has any of
//             either, dropped where it was (`$80:C97F`), and the thread ends,
//             one word pulled
//
// ## The fishman's death, `$81:E4C1`
//
// See `port/fishman.h`. Its frame leaves at its fate, and from there:
//
//   $81:E4C1  nothing can tell it of anything
//   $81:E4C9  the second record it keeps at `$42` freed, if it has one; then
//             whoever killed it is paid, which is `port/begin.h`'s
//   $81:E4DF  ...and counted: one more fishman, and one more to the side
//             that killed it, as `$80:9D6A` finds the side. It can no
//             longer be touched, and it is heard
//
// Its last pictures, and its record, are `port/begin.h`'s too. The patrol's
// death is the same, with its count at `$81:E572`, and its end its own:
//
//   $81:E59A  its weight given back and its record freed
//   $81:E5AC  ...and the second record, if it has one
//
// ## The pause, `$80:89C2`
//
// See `port/pause.h`. Between the waits, which are `port/hold.h`'s:
//
//   $80:89C2  the volume down: the screen dimmed to 8
//   $80:89E9  Start let go of again: the volume back up, `$7F`
//   $80:89F0  ...and the screen at 15 again, and back to the main loop
//
// The main loop's frame then goes on from the HUD's refresh, `$80:8537`,
// with its two questions (`port/mainloop.h`).
//
// ## A footballer sent off, `$81:C6D9`
//
// See `port/football.h`. Back from its pictures, its handler gives it the
// state for its next pass and returns.
//
// ## An ant's flash, `$81:BAEC`
//
// What hits an ant on level 45 puts a call to this in its thread, at its
// next waking: its attributes `$0C00` for two ticks, and nothing after, and
// back to where the thread was.
//
// ## The walk's square, `$82:F6EB`
//
// See `port/walk.h`: the walk begins this thread for a square of 64 pixels
// a player walks into. It keeps the square's columns, and rows from ten
// above its top to fifty-six below it, and every six ticks asks whether
// that band still has a player in it. While it does, nothing happens.
//
//   $82:F6EB  the band set, and the first sleep
//   $82:F6F8  a pass: is somebody at or below the band's top, and somebody
//             above its bottom?
//
// **The two need not be the same player.** With one above the band and the
// other below it, the band counts as still held.
//
// When it is not held, what happens is the ROM's, `$82:F5FA`: a record is
// shown with a sound and two lists of pictures, the tile at the middle of
// the square becomes the tile before it, and the thread ends and lets the
// square go.
//
// ## A bonus object flying off, `$82:E0B4`
//
// See `port/collide.h`: a player who picks up one of the four bonus objects
// begins this thread, with the kind at `$04`. It sleeps a tick, takes a
// record, and between its calls:
//
//   $82:E0BF  the record is put where the object was, with the object's own
//             picture, drawn and with nothing that can touch it
//   $82:E102  one of the four diagonals by the draw, for twenty-one ticks
//   $82:E115  a tick of it: eight pixels each way. After the last the
//             record is given back.
//
// ## The bystander, `$82:DD52`
//
// See `port/bystander.h`, which has its loop. Its setup:
//
//   $82:DD52  `JSR $DE83`, which asks for a record at once
//   $82:DE87  the record put on the spot, drawn, with the picture its kind
//             names and nothing that can touch it. Around the spot, a box
//             forty wide and sixty-five tall, from twenty to the left and
//             ten above. Its handler is named.
//   $82:DEE8  it is not to end, and the first sleep is five ticks
//
// What it shows when a player comes up, `$82:DDA7`:
//
//   $82:DDA7  nothing, within sixty frames of the last time. Otherwise the
//             frame is kept, both players' flags cleared, and three ticks
//   $82:DDCF  ...the first player's side of the panel blanked
//   $82:DDD6  ...and the second's. Then the panel drawn, and the screen
//             dimmed, which is `port/bystander.h`'s
//
// Then it waits a frame at a time while two words of a table at `$7E:6DA6`
// step, the first down by one and the second up by two, until the first is
// eight:
//
//   $82:DE08  a frame of that, as far as the next `WAI`. After the last,
//             the words its kind names are asked for.
//   $82:DE2F  the pads as they are, kept, and the next of its two pictures
//   $82:DE4F  ...and ten frames, `WAI` by `WAI` at `$82:DEF6`
//   $82:DE56  the same again while neither pad changes. Then its first
//             picture, and the panel back as it was
//
// The screen lit again is `port/bystander.h`'s too, and then the loop goes
// on, or ends if it has been told to:
//
//   $82:DD64  five ticks, or its end
//
// ## A frozen creature, `$81:84D6`
//
// See `port/collide.h`: five hits of the ice weapon send a creature's
// thread here, and it comes back when the freeze is over.
//
//   $81:84D6  its record takes its palette from the record's own attribute
//             word, its collide id is kept on the thread's stack, and it
//             is given `$36` in its place
//   $81:84F2  ten ticks gone, and one off the count of hits at `$7E`. Once
//             that is below zero, the record's palette and collide id are
//             put back, and the thread goes on where it was sent from.
//
// ## The chainsaw maniac's cut, `$81:92D6`
//
// See `port/chainsaw.h`: the state it is in while it cuts a hedge, with the
// hedge's place at `$42` and `$44` and the side it cuts from, one to eight,
// at `$46`. Its stretches, from where each call comes back:
//
//   $81:92D6  the cut called
//   $81:93D3  if the level's sample set is the first, the saw is heard
//   $81:93E2  a place to cut from, across or down by the side
//   $81:9326  across: a draw of one to four, times four, past twenty-eight
//             into the hedge's square of 64. The maniac is put there, and
//             sleeps a tick.
//   $81:9362  down: the same, from thirty-two
//   $81:9347  is somebody standing there? Then another draw.
//   $81:9351  the place is kept, in pixels and quarters, and a record of
//             chips is asked for beside it
//   $81:939E  the chips are put there with their first picture, drawn in
//             front of everything; side seven's the other way round. The
//             maniac is hidden while the chips show their list, eight ticks
//             a picture.
//   $81:944E  the next picture of the list, or the maniac drawn again
//   $81:92D9  the hedge's block swapped for its pair: `port/tile_rows.h`
//             has the instructions, and `cut_swap` is a row of its own
//   $81:930C  hedges cut counted at `$1FC2`, and a second list, five ticks
//             a picture
//   $81:94DA  the next picture of that, or the chips no longer in front and
//             given back
//   $81:94EE  and the maniac's handler is `$81:990B` again
//   $81:931D  its count of refused steps cleared
//
// **The check of the sample set is a stretch of its own**, `$81:93D3`, for
// the werewolf's reason: `src/bank_sfx.h` watches that instruction.
//
// ## A player's death, `$80:CE81`
//
// `$80:CE72`, which a player's thread asks every frame, has found them with
// no health, and played the sound of it. Then:
//
//   $80:CE81  a state that has an end due is ended, by its routine at
//             `$72`: the flashing's `$80:D97F`, or one of five others
//   $80:CE94  the potion's monster shows one picture for thirty ticks.
//             Anyone else is shown a list of pictures by which of the two
//             they are, and the picture they had is kept.
//   $80:CEAA  the monster's picture after, by the same
//   $80:CEC3  a life gone. With one left the kept picture is put back, and
//             `$80:D95F`: health is ten again, and they flash for 120
//             frames with nothing able to touch them.
//
// With none left it is the ROM's, `$80:CED5`, and so is what comes between
// the first and second for a player with no end due.
//
// **The lives cheat patches `$80:CEC5`**, the `DEC` of a life, which is in
// the fourth. `port_cheats.lives` says when it is on: see `port/cheat.h`.
//
// ## A player caught in a slime's splash, `$80:DC1E`
//
// A slime's glob bursts with collide id `$34`, and a player it touches in
// the ordinary state, out of the hurt window, is given this as what they do
// next (`port/collide.h`, the state tail):
//
//   $80:DC1E  covered: the record at `$0A` is put a pixel below them, with
//             the cover's picture, thirty-two up, and drawn over them. They
//             are in state `$0C` for 390 ticks, which `$80:DC70` ends, and
//             nothing they were firing or carrying is out. Then `$80:F354`,
//             a sleep of a tick with the word at `$1F98` cleared before
//   $80:F360  ...and after. Back in `$80:DC1E`, what they hold is put aside
//             for weapon `$0F` and item `$0D`, and they stand
//             (`pose_stand_begin`, `port/pose.h`).
//
// The second is a stretch of `$80:F354`, which six other places call. It
// is offered only the slime's return.
//
// ## The starts of level 17's spider and walker, `$83:B277` and `$81:D704`
//
// See `port/spider.h` and `port/walker.h`, which have their loops. Each
// asks `spawn_has_room` first, which is a link, and with no room the thread
// only ends. Then:
//
//   $83:B27B  the spider's weight, fifteen, on the level's load, and a
//             record asked for
//   $83:B34A  the record put where it was begun, drawn, with its picture
//             and collide id 3. Its handler named, a draw of one of the
//             eight ways, wandering, and the first sleep
//
//   $81:D708  the walker's weight, twenty-four, and a record asked for
//   $81:D691  the record put as the spider's is, a health of one, and its
//             first pictures asked for
//   $81:D6E5  its handler named, a draw of one of the four straight ways,
//             walking, and the first sleep
//
// The draws are `rng_next` (`port/rng.h`), made here. The stretches after
// the record is asked for go on through the `RTS`s of the start's `JSR`s, so
// each needs its start's return under it.
//
// ## A locked door's buzz, `$82:DEFB`
//
// See `port/walk.h`: a player who pushes at a door with no key begins this
// thread every frame they push. The word at `$7E:1FF4` says one is buzzing
// already, and then the new one only ends. Otherwise:
//
//   $82:DEFB  the word set, and sound `$13`
//   $82:DF0A  ten ticks' sleep (an argument), sound `$13` again (another),
//             and twenty ticks' sleep (a third)
//   $82:DF1F  the word cleared, and the thread ends
//
// ## A slime's start, `$81:CCCF`
//
// See `port/slime.h`, which has its loop and `slime_begin`, its record:
//
//   $81:CCCF  its weight, thirty-two, on the level's load, and the record
//             asked for
//   $81:CCAC  its first pictures begun: its handler named, and a draw
//   $81:C9E8  one of the four ways, from the draw, and the loop's state set
//             to `$81:C9FA`. Then it sleeps four ticks.
//
// The draw and the way are `$81:C9E4`, a routine, which the slime's loop
// calls too. The second and third stretches are only the start's: each
// goes on through an `RTS`, and needs it to return to the start.
//
// ## The vampire's states that sleep, `$82:EA35` and on
//
// See `port/vampire.h`: its frame goes as far as the first call of each and
// leaves the loop's return on the stack. Then:
//
//   landing  $82:EA35  sound `$27` where the sample set is set 3
//            $82:EA44  the pictures of landing asked for
//            $82:EA4B  nine health, its handler again, and a draw...
//            $82:EA5E  ...says whether it hides or darts after firing; and
//                      it fires, drawn
//   firing   $82:EE1B  its pictures played: trackers, more for a weaker one
//            $82:EE3F  three ticks apart,
//            $82:EE4A  each sent a way round from the last
//            $82:EE62  the last pictures played: it watches and hides,
//                      watches and fires, or holds and darts
//   hiding   $82:EBA3  its going played: not drawn, and it keeps where
//                      whoever it watched was
//   back     $82:EC5A  32 ticks' sleep after its puff begins (an argument)
//            $82:EC61  things that touch it are told to its handler again,
//                      and it watches
//
// The four that end a state go on through its `RTS` to the loop, which
// sleeps or ends. Each needs the loop's return on the stack.
//
// ## A tracker's start and end, `$82:F03E`, the vampire's puff, and the Snakeoid's pieces
//
// See `port/tracker.h`, which has the tracker's loop and its record:
//
//   $82:F03E  its weight, eight, on the level's load
//   $82:F04B  its record kept, and a tick's sleep
//   $82:F05E  its loop over: its last pictures asked for
//
// The puff the vampire comes back in, `$82:F1E6`:
//
//   $82:F1E6  its weight, five, on the load
//   $82:F1F4  its record, at the place it was asked for, drawn, and its
//             pictures asked for
//
// A piece of the Snakeoid bursting off as it dies (`port/snakeoid.h`),
// `$83:B1DD`, is the same thread but for its weight, its picture, its
// record's flags and a sound:
//
//   $83:B1DD  its weight, seven, on the load
//   $83:B1EB  its record, as the puff's, drawn sorted first and in front of
//             every layer, and sound `$21`
//   $83:B226  its pictures asked for
//
// Its end, at `$83:B22D`, is `port/begin.h`'s.
//
// ## The colour pulse's setup, `$80:A0EF`
//
// See `port/palcycle.h`, which has its waking. The setup keeps the level's
// colour 90 without its green, twice, and a step of one green, and sleeps
// four ticks.
//
// ## The big figure's start, level 25, `$82:9569`
//
// See `port/boss_thread.h`, which has its turns. Before the first:
//
//   $82:9569  its weight, forty, on the level's load, and its colours
//             asked for
//   $82:94EE  its tiles queued to go up, `$2800` bytes from `$93:8000` to
//             VRAM `$5000`, by `port/bossbg.h`'s `boss_bg_queue_one`
//   $82:9511  a frame at a time until they have gone up. Then its place,
//             where its plane is drawn, and a record asked for
//   $82:94B8  one of its four parts, which shows nothing and nothing can
//             touch, kept on its page. After the fourth: none of its four
//             spits out, its handler named, calm, and off to choose
//
// `$82:94B8` knows which part it has made by where its `RTS` goes.
//
// ## The saucer's start, level 21, `$82:873C`
//
// See `port/saucer.h`, which has its passes. Before the first:
//
//   $82:873C  its weight, twenty-four, on the load, and its colours
//   $82:8750  its place, and its picture drawn
//   $82:876A  its tiles queued as the figure's are, from `$93:A620`
//   $82:8786  its lights, its two shots, its health of nine, its hatch
//             shut, and the nearest thing asked for
//   $82:84DD  the spot set beside it on the side that thing is on, as a
//             hunting saucer sets it, and its first sleep
//
// ## One instruction or two
//
// What stands by itself between two calls and belongs to nothing ported: a
// word of a thread's page set to an address before a return, a list of
// pictures named, a page pulled, a sleep's ticks.
//
// ## A player's flashing ended, `$80:D97F`
//
// See `port/player_frame.h`. The player is drawn again whichever frame it
// is, and goes back to the ordinary state, or to the turning one if that is
// what ran under the flashing.
//
// ## A player shown hurt, `$80:D056`
//
// `port/hit.h` leaves a hit player here when it is shown by a list of
// pictures: the picture they had is kept on the thread's stack, the list
// plays, which sleeps, and the picture is put back.
//
// Port code: libc only.

#ifndef PORT_THREAD_STRETCHES_H
#define PORT_THREAD_STRETCHES_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/tile_rows.h"
#include "port/wram.h"

// `LDA #value` and a call: the whole of a stretch.
typedef struct {
  uint32_t pc;
  uint16_t value;
} ThreadArgument;

#define THREAD_ARGUMENT_COUNT 42
extern const ThreadArgument THREAD_ARGUMENTS[THREAD_ARGUMENT_COUNT];

// `LDA #value : STA at`, on the thread's page, and the `RTS` after.
typedef struct {
  uint32_t pc;
  uint16_t value;
  uint8_t at;
} ThreadSetter;

#define THREAD_SETTER_COUNT 2
extern const ThreadSetter THREAD_SETTERS[THREAD_SETTER_COUNT];

// `LDA #a : LDY #y` and a call.
typedef struct {
  uint32_t pc;
  uint16_t a, y;
} ThreadPair;

#define THREAD_PAIR_COUNT 6
extern const ThreadPair THREAD_PAIRS[THREAD_PAIR_COUNT];

// --- the weed ----------------------------------------------------------------

// A seed thrown: where the stretch starts, the weed's picture before it,
// how far across from the root it begins, and the `JSL` that begins it.
typedef struct {
  uint32_t pc;
  uint16_t picture;  // or 0: as it is
  int16_t across;
  uint32_t spawn_pc;
  int block;
} WeedThrow;

#define WEED_THROW_COUNT 3
extern const WeedThrow WEED_THROWS[WEED_THROW_COUNT];

// How it begins: the bits its tiles have.
typedef struct {
  uint32_t pc;
  uint16_t tile_bits;
} WeedKind;

#define WEED_KIND_COUNT 2
extern const WeedKind WEED_KINDS[WEED_KIND_COUNT];

#define WEED_SNAP_ASK_PC 0x81d15bu    // `JSL player_in_range`
#define WEED_SNAP_SHOWN_PC 0x81d15fu
#define WEED_SNAP_SHOW_PC 0x81d164u   // `JSL pictures_play`
#define WEED_SNAP_PICTURES 0xd1f8u
#define WEED_SNAP_REST_PC 0x81d1deu
#define WEED_SNAP_RTS_PC 0x81d1f4u
#define WEED_PASS_END_PC 0x81d2adu
#define WEED_KILLED_PC 0x81d2b3u      // `kill_scored` of `port/begin.h`
#define WEED_DIED_PC 0x81d2c8u
#define WEED_MARK_PC 0x81d2cau        // `JSL victim_mark_gone`
#define WEED_END_PC 0x81d2ceu         // `record_end` of `port/begin.h`
#define WEED_ENTER_PC 0x81d28cu
#define WEED_ALLOC_CALL_PC 0x81d296u  // `JSR $D210`
#define WEED_MADE_PC 0x81d214u
#define WEED_HANDLER_PC 0x81d277u     // `JSL thread_set_handler`
#define WEED_BEGUN_PC 0x81d299u
#define WEED_GROWS_PC 0x81d127u

#define WEED_ANYBODY 0xffffu          // however far
#define WEED_SEED_THREAD 0xd4c9u
#define WEED_SEED_THREAD_BANK 0x0081
#define WEED_SEED_TICKS_APART 0x0005
#define WEED_PICTURE 0xa12bu
#define WEED_PICTURE_BANK 0x0090
#define WEED_COLLIDE_ID 0x0003
#define WEED_ATTR 0x0c00
#define WEED_REST_PASSES 0x002d
#define WEED_LOAD 0x0023
#define WEED_HANDLER 0xd301u
#define WEED_HEALTH 0x0019
#define WEED_DP_ENTRY 0x06            // its place in the level's list
#define WEED_DP_HEALTH 0x0e           // `D301_DP_HEALTH` of `port/collide.h`
#define WEED_DP_FREEZE_HITS 0x7e      // what a freeze has done to it
#define WEED_DP_TARGET 0x32           // who it snaps at

// --- the follower ------------------------------------------------------------

#define FOLLOWER_LOOK_PC 0x82f44au
#define FOLLOWER_EDGE_PC 0x82f44eu      // `JSL terrain_out_of_bounds`
#define FOLLOWER_ON_LEVEL_PC 0x82f452u
#define FOLLOWER_TILE_ASK_PC 0x82f458u  // `JSL tile_attrs_at_pixel`
#define FOLLOWER_TILE_PC 0x82f45cu
#define FOLLOWER_MARK_SLEEP_PC 0x82f464u
#define FOLLOWER_MARK_PC 0x82f468u
#define FOLLOWER_PUT_PC 0x82f472u       // `JSL map_tile_put`
#define FOLLOWER_ROOM_PC 0x82f4a2u
#define FOLLOWER_NO_ROOM_PC 0x82f4dfu   // `RTL`
#define FOLLOWER_MAKE_PC 0x82f4aeu      // `JSR $F354`
#define FOLLOWER_MADE_PC 0x82f358u
#define FOLLOWER_HANDLER_PC 0x82f3afu   // `JSL thread_set_handler`

#define FOLLOWER_HIT_PC 0x82f3f4u
#define FOLLOWER_HIT_SLEEP_PC 0x82f406u
#define FOLLOWER_HIT_PICTURE 0xd781u
#define FOLLOWER_HIT_FRAMES 0x0002

#define FOLLOWER_GROUND 0x0080u         // the bit of a tile it marks
#define FOLLOWER_TILE 0x150du
#define FOLLOWER_LOAD 0x000a
#define FOLLOWER_PICTURE 0xd6b5u
#define FOLLOWER_PICTURE_BANK 0x008f
#define FOLLOWER_COLLIDE_ID 0x0003
#define FOLLOWER_HANDLER 0xf4efu
#define FOLLOWER_DP_SIDE_GIVEN 0x00
#define FOLLOWER_FLIP 0x0002u           // of `ACTOR_FLIP`: drawn the other way

// --- the main thread ---------------------------------------------------------

#define MAIN_RESET_PC 0x8085e1u
#define MAIN_SCORES_ASK_PC 0x8085fcu   // `JSL top_scores_default`
#define MAIN_SCORES_SET_PC 0x808600u
#define MAIN_SOUND_ASK_PC 0x808611u    // `JSL apu_sfx_bank_end`
#define MAIN_SOUND_SET_PC 0x808615u
#define MAIN_FIRST_RTL_PC 0x808617u
#define LEVEL_RECORD_ASK_PC 0x808849u
#define LEVEL_RECORD_PC 0x82ac07u      // what it jumps to
#define LEVEL_FADED_PC 0x808525u
#define MAIN_FRAME_SLEEP_PC 0x80852bu
#define LEVEL_COUNT_PC 0x80867bu
#define LEVEL_COUNT_ASK_PC 0x808693u   // `JSL $80A8A4`, twice
#define LEVEL_COUNT_ASKED_PC 0x808697u
#define LEVEL_COUNT_ASK_B_PC 0x80869du
#define CACHE_START_PC 0x80c04cu
#define THREAD_START_PC 0x8083aeu      // what it jumps to

#define FRONTEND_ENTER_PC 0x809126u
#define FRONTEND_LAYERS_PC 0x809128u   // `JSL layers_reset`
#define PLAYER_KEEP_PC 0x80d16eu
#define PLAYER_ALLOC_PC 0x80d170u      // `JSL actor_slot_alloc`
#define PLAYER_DP_KEPT 0x0a
#define SPAWNER_TICK_PC 0x80c8f9u
#define SPAWNER_TICK_SLEEP_PC 0x80c904u
#define SPAWNER_WAIT_PC 0x80c908u
#define SPAWNER_SLEEP_PC 0x80c90du
#define SPAWNER_DP_CLEARED 0x0c

#define W_MAIN_1B80 0x1b80u            // $2000 as the thread starts

// --- the walk's square -------------------------------------------------------

#define SQUARE_ENTER_PC 0x82f6ebu
#define SQUARE_PASS_PC 0x82f6f8u       // where its sleep comes back
#define SQUARE_YIELD_PC 0x82f6f4u      // `JSL thread_yield`, six ticks in A
#define SQUARE_LEFT_PC 0x82f5fau       // nobody in the band
// ...and from there, round its calls.
#define SQUARE_MADE_PC 0x82f643u       // its record, in A
#define SQUARE_MADE_RETURN 0xf5fcu     // `JSR $F63F`'s
#define SQUARE_SOUND_CALL_PC 0x82f602u // `JSL apu_play_sfx`
#define SQUARE_SHOWN_PC 0x82f60du      // after its first pictures
#define SQUARE_CELL_RETURN 0xf60fu     // `JSR $F678`'s
#define SQUARE_CELL_CALL_PC 0x82f697u  // `JSL blockmap_cell_ptr`
#define SQUARE_CELL_PC 0x82f69bu
#define SQUARE_TILE_CALL_PC 0x82f6a5u  // `JSL $80:AB5A`, the tile less one
#define SQUARE_TILED_PC 0x82f6a9u
#define SQUARE_BOX_CALL_PC 0x82f5f5u   // `JSL actor_notify_box`
#define SQUARE_TOLD_PC 0x82f613u       // after the box, by a link's `RTS`
#define SQUARE_PICTURES_CALL_PC 0x82f616u  // `JSL pictures_play`
#define SQUARE_HIDDEN_PC 0x82f61au
#define SQUARE_FREE_CALL_PC 0x82f61cu  // `JSL actor_slot_free`
#define SQUARE_FREED_PC 0x82f620u
#define SQUARE_END_PC 0x82f70au        // its `RTL`
#define SQUARE_SOUND 0x0021
#define SQUARE_PICTURE 0xd93eu
#define SQUARE_PICTURE_BANK 0x008f
#define SQUARE_COLLIDE_ID 0x0069
#define SQUARE_LAST_PICTURES 0xf62fu
#define SQUARE_BOX_ID 0x006f           // what it tells those round it
#define SQUARE_DP_RECORD 0x08
#define W_SQUARE_COLUMN 0x0038u        // the tile under it, and the box
#define W_SQUARE_ROW 0x003au
#define W_SQUARE_BOX_TOP 0x003cu
#define W_SQUARE_BOX_BOTTOM 0x003eu
#define W_SQUARE_BOX_ID 0x0040u
#define W_SQUARE_CELL 0x0028u          // where `blockmap_cell_ptr` leaves it
#define SQUARE_DONE_PC 0x82f704u       // ...and after that, the end
#define SQUARE_BAND_CALL_PC 0xf6ebu    // `JSR $F6BA`
#define SQUARE_HANDLER_CALL_PC 0xf6eeu // `JSR $F6AB`
#define SQUARE_ASK_CALL_PC 0xf6b1u     // the handler: `JSR $F593`
#define SQUARE_PASS_RETURN 0xf6ffu     // the `PEA` the handler returns by
#define SQUARE_HANDLER 0xf6b1u
#define SQUARE_TICKS 0x0006
#define SQUARE_SIZE 0x0040
#define SQUARE_ABOVE 0x000a            // the band's top, above the square's
#define SQUARE_TO_MIDDLE 0x0042        // ...from there to the middle
#define SQUARE_BELOW_MIDDLE 0x0024     // ...and from there to the bottom
#define W_SQUARE_PLAYERS 0x00d2u       // each player's record, or 0
#define SQUARE_DP_X 0x00               // given: where the square is
#define SQUARE_DP_Y 0x02
#define SQUARE_DP_SLOT 0x04            // ...and which of the five it is
#define SQUARE_DP_DONE 0x0a
#define SQUARE_DP_MIDDLE_X 0x0c
#define SQUARE_DP_MIDDLE_Y 0x0e
#define SQUARE_DP_HANDLER 0x10
#define SQUARE_DP_LEFT 0x18
#define SQUARE_DP_RIGHT 0x1a
#define SQUARE_DP_TOP 0x1c
#define SQUARE_DP_BOTTOM 0x1e
#define SQUARE_DP_SLOT_KEPT 0x24

// --- a bonus object flying off -----------------------------------------------

#define FLYOFF_DRESS_PC 0x82e0bfu      // where the record comes back
#define FLYOFF_HANDLER_PC 0x82e0fau    // `JSL thread_set_handler`, to none
#define FLYOFF_WAY_PC 0x82e102u        // where the draw comes back
#define FLYOFF_PASS_PC 0x82e115u       // where its sleep comes back
#define FLYOFF_YIELD_PC 0x82e111u      // `JSL thread_yield`, one tick in A
#define FLYOFF_FREE_PC 0x82e133u       // `JML actor_slot_free`
#define FLYOFF_PICTURES 0x82e147u      // by the kind
#define FLYOFF_STEPS 0x82e137u         // by the diagonal times four: x, y
#define FLYOFF_PICTURE_BANK 0x008f
#define FLYOFF_FLAGS 0x8008u           // drawn, and its priority
#define FLYOFF_TICKS 20                // and one more: the count runs to -1
#define FLYOFF_DIAGONALS 4
#define FLYOFF_DP_X 0x00               // given: where the object was
#define FLYOFF_DP_Y 0x02
#define FLYOFF_DP_KIND 0x04
#define FLYOFF_DP_RECORD 0x08
#define FLYOFF_DP_STEP_AT 0x0a
#define FLYOFF_DP_TICKS_LEFT 0x0c

// --- the bystander -----------------------------------------------------------

#define BYSTANDER_ENTER_PC 0x82dd52u
#define BYSTANDER_SETUP_RETURN 0xdd54u   // `JSR $DE83`'s
#define BYSTANDER_ALLOC_PC 0x82de83u     // `JSL actor_slot_alloc`
#define BYSTANDER_DRESS_PC 0x82de87u     // where the record comes back
#define BYSTANDER_HANDLER_CALL_PC 0x82dee4u  // `JSL thread_set_handler`
#define BYSTANDER_SET_PC 0x82dee8u       // where that comes back
#define BYSTANDER_STEP_PC 0x82de08u      // after a `WAI`
#define BYSTANDER_WAIT_PC 0x82de07u      // the `WAI`
#define BYSTANDER_SAY_PC 0x82de2bu       // `JSL $80:C219`

// What it shows.
#define BYSTANDER_MEETS_PC 0x82dda7u
#define BYSTANDER_TOO_SOON_PC 0x82ddbau  // the `RTS`
#define BYSTANDER_MEETS_SLEEP_PC 0x82ddcbu
#define W_BYSTANDER_MET_AT 0x6db0u     // of bank `$7E`: the frame it last did
#define BYSTANDER_MEET_GAP 0x003c
#define BYSTANDER_MEETS_TICKS 0x0003
#define BYSTANDER_HUSH_PC 0x82ddcfu
#define BYSTANDER_HUSH_CALL_PC 0x82ddd2u   // `JSL $80:C1CF`
#define BYSTANDER_HUSH_2_PC 0x82ddd6u
#define BYSTANDER_HUSH_2_CALL_PC 0x82ddd9u
#define BYSTANDER_SAID_PC 0x82de2fu
#define BYSTANDER_FLIP_CALL_PC 0x82de4bu   // `JSL sprite_build_oam`
#define BYSTANDER_LOOKED_PC 0x82de56u
#define BYSTANDER_DONE_CALL_PC 0x82de6eu   // `JSL hud_reset`
#define BYSTANDER_BACK_PC 0x82dd64u
#define BYSTANDER_DP_FLIP 0x16             // which of its two pictures
#define BYSTANDER_DP_PADS 0x18             // the two pads, as they were
#define WAIT_FRAMES_WAI_PC 0x82def6u       // `$82:DEF6`, a frame a `WAI`
#define WAIT_FRAMES_PC 0x82def7u           // ...after one
#define WAIT_FRAMES_RTL_PC 0x82defau
// `$80:DD79`: the first leap's landing, from where its sound comes back. The
// player is put where the leap was aimed, their record's pictures are bank
// `$90`'s, and it stops at the `JSL` that plays the splash, the list in A.
// `port/player_frame.h` has the rest.
#define LEAP_SPLASH_PC 0x80dd79u
#define LEAP_SPLASH_CALL_PC 0x80dd8cu  // `JSL $81:832C`
#define LEAP_SPLASH_PICTURES 0xdda5u
#define LEAP_DP_RECORD 0x08
#define LEAP_DP_X 0x30
#define LEAP_DP_Y 0x32
#define LEAP_DP_AIM_X 0x60
#define LEAP_DP_AIM_Y 0x62
#define LEAP_RECORD_PICTURE_BANK 0x0a
// `$80:E2FF`, down in the water off a trampoline, from where its sound comes
// back: the same pictures' bank, and the `JSL` that plays its splash.
#define SOAK_SPLASH_PC 0x80e2ffu
#define SOAK_SPLASH_CALL_PC 0x80e30au  // `JSL $81:832C`
#define SOAK_SPLASH_PICTURES 0xe319u
#define LEAP_PICTURE_BANK 0x0090u

// --- level 29's creature staggered ----------------------------------------
//
// When its stagger meter empties (`enemy_990b_stagger`, `port/collide.h`)
// its body's loop leaves for `$81:9643`. It is answered for by the stagger's
// handler, `$81:96E4`, and spins: eight pictures two ticks apart, from the
// way it faced and round, with bit 1 of its record's flags clear. Then its
// flags are put back, its collide id is 2, and it plays a list of pictures,
// another seven times, and a last. Then it is itself again: its pictures,
// collide id 3, its own handler, and its loop's state at `$12`.
//
//   $81:9643  stagger_begin  the handler and the spin begun, to the sleep
//   $81:9680  stagger_spin   the next picture and a sleep, or the spin over
//                            and the first list asked for
//   $81:96A7  stagger_dazed  the second list, the first of seven times
//   $81:96B3  stagger_daze   ...the next time, or the last list
//   $81:96BE  stagger_over   itself again, to the `JMP` to the state setter
#define STAGGER_BEGIN_PC 0x819643u
#define STAGGER_SPIN_YIELD_PC 0x81967cu  // `JSL thread_yield`, A already 2
#define STAGGER_SPIN_PC 0x819680u
#define STAGGER_SHOWN_CALL_PC 0x8196a3u  // `JSL $81:832C`, the list in A
#define STAGGER_DAZED_PC 0x8196a7u
#define STAGGER_DAZE_CALL_PC 0x8196afu
#define STAGGER_DAZE_PC 0x8196b3u
#define STAGGER_LAST_CALL_PC 0x8196bau
#define STAGGER_OVER_PC 0x8196beu
#define STAGGER_BACK_PC 0x819215u        // `$12` set to the loop's state
#define STAGGER_BANK 0x0081
#define STAGGER_HANDLER 0x96e4u
#define STAGGER_SPIN_PICTURES 0x8196f1u  // a picture for each way
#define STAGGER_SPIN_TICKS 2
#define STAGGER_SPINS 8
#define STAGGER_DAZES 6                  // more times the second list plays
#define STAGGER_SHOWN_LIST 0x9701u
#define STAGGER_DAZE_LIST 0x9713u
#define STAGGER_LAST_LIST 0x971du
#define STAGGER_RESUME 0x96acu           // `$12` while the lists play
#define STAGGER_FLAG_CLEARED 0x0002u
#define STAGGER_COLLIDE_DAZED 0x0002
#define STAGGER_PICTURE_BANK_DAZED 0x008f
#define STAGGER_OWN_PICTURE 0xc73bu
#define STAGGER_OWN_PICTURE_BANK 0x0090
#define STAGGER_COLLIDE_OWN 0x0003
#define STAGGER_OWN_HANDLER 0x990bu
#define STAGGER_DP_RECORD 0x08
#define STAGGER_DP_WAY 0x0c      // the spin's: a way, doubled
#define STAGGER_DP_FACING 0x0e   // the way it faced, from 2
#define STAGGER_DP_STATE 0x12
#define STAGGER_DP_SPINS 0x36
#define STAGGER_DP_FLAGS 0x3c    // its record's flags, kept
#define STAGGER_DP_4A 0x4a       // set to $0F, before and after; I have not
#define STAGGER_DP_4A_VALUE 0x000f  // read what uses it
#define STAGGER_DP_DAZES 0x7e
#define BYSTANDER_HANDLER 0xdeebu
#define BYSTANDER_HANDLER_BANK 0x0082
#define BYSTANDER_PICTURES 0xe611u       // a long pointer by the kind
#define BYSTANDER_PICTURE_BANK 0x008f
#define BYSTANDER_FLAGS 0x8008u
#define BYSTANDER_BOX_LEFT 0x0014
#define BYSTANDER_BOX_WIDTH 0x0028
#define BYSTANDER_BOX_ABOVE 0x000a
#define BYSTANDER_BOX_HEIGHT 0x0041
#define BYSTANDER_WORDS 0x82e14fu        // what each kind says, by the kind
#define BYSTANDER_WORDS_BANK 0x0082
#define BYSTANDER_DP_X 0x00              // given: the spot
#define BYSTANDER_DP_Y 0x02
#define BYSTANDER_DP_KIND 0x04
#define BYSTANDER_DP_PICTURE 0x14        // the pointer the kind names
#define W_BYSTANDER_SHRINKS 0x6da6u      // of bank $7E: down by one...
#define W_BYSTANDER_GROWS 0x6da9u        // ...and up by two, a frame
#define BYSTANDER_SHRUNK 0x0008

// --- a frozen creature -------------------------------------------------------

#define FREEZE_BEGIN_PC 0x8184d6u
#define FREEZE_PASS_PC 0x8184f2u       // where its sleep comes back
#define FREEZE_YIELD_PC 0x8184eeu      // `JSL thread_yield`, ten ticks in A
#define FREEZE_OVER_PC 0x818505u       // the `RTL` back to where it was
#define FREEZE_TICKS 0x000a
#define FREEZE_COLLIDE_ID 0x0036
#define FREEZE_DP_RECORD 0x08
#define FREEZE_DP_HITS 0x7e            // `ACTOR_DP_FREEZE_HITS`

// --- the chainsaw maniac's cut -----------------------------------------------

#define CUT_PC 0x8192d6u
#define CUT_RETURN 0x92d8u              // the first `JSR`'s
#define CUT_HEARD_PC 0x8193d3u          // `LDA $1F52`, which is watched
#define CUT_SOUND_PC 0x8193deu          // `JSL apu_play_sfx`
#define CUT_SOUND 0x0029
#define CUT_AIM_PC 0x8193e2u
#define CUT_AIMS 0x81945du              // a routine by the side
#define CUT_AIM_RETURN 0x93e8u
#define CUT_ACROSS_PC 0x819322u         // `JSL rng`, and back for another
#define CUT_DOWN_PC 0x81935eu
#define CUT_ACROSS_DRAWN_PC 0x819326u
#define CUT_DOWN_DRAWN_PC 0x819362u
#define CUT_ACROSS_SLEEP_PC 0x819343u   // `JSL thread_yield`, a tick in A
#define CUT_DOWN_SLEEP_PC 0x81937fu
#define CUT_ACROSS_WOKE_PC 0x819347u
#define CUT_DOWN_WOKE_PC 0x819383u
#define CUT_ACROSS_ASK_PC 0x81934du     // `JSL actor_at_point`
#define CUT_DOWN_ASK_PC 0x819389u
#define CUT_ACROSS_PLACED_PC 0x819351u
#define CUT_DOWN_PLACED_PC 0x81938du
#define CUT_MAKE_PC 0x81939au           // `JSL actor_slot_alloc`
#define CUT_MAKE_RETURN 0x9412u
#define CUT_MADE_PC 0x81939eu
#define CUT_SLEEP_PC 0x81944au          // `JSL thread_yield`, eight ticks
#define CUT_SHOWN_PC 0x81944eu
#define CUT_LISTED_PC 0x81945cu         // the `RTS` back to the cut
#define CUT_SWAP_PC 0x8192d9u
#define CUT_SWAP_PUT_PC 0x819308u       // `JSL $80:AB5A`, the block in A
#define CUT_TILED_PC 0x81930cu
#define CUT_SECOND_RETURN 0x9312u
#define CUT_SECOND_SLEEP_PC 0x8194d6u   // `JSL thread_yield`, five ticks
#define CUT_SHOWN_AGAIN_PC 0x8194dau
#define CUT_FREE_PC 0x8194eau           // `JSL actor_slot_free`
#define CUT_FREED_PC 0x8194eeu
#define CUT_HANDLER_CALL_PC 0x819319u   // `JSL thread_set_handler`
#define CUT_DONE_PC 0x81931du
#define CUT_SETTER_PC 0x819250u         // `setter_819250`
#define CUT_ACROSS_INTO 0x001c          // into the hedge's square
#define CUT_DOWN_INTO 0x0020
#define CUT_SQUARE 0x0040
#define CUT_FIRST_LISTS 0x81946fu       // by the side times four: a list,
                                        // and how far below the chips go
#define CUT_SECOND_LISTS 0x8194f4u      // by the side doubled
#define CUT_FIRST_TICKS 0x0008
#define CUT_SECOND_TICKS 0x0005
#define CUT_CHIPS 0xb944u
#define CUT_CHIPS_BANK 0x0090
#define CUT_SIDE_TURNED 7               // its chips are drawn the other way
#define CUT_TURNED 0x0002u              // of `ACTOR_FLIP`
#define CUT_HANDLER 0x990bu
#define CUT_HANDLER_BANK 0x0081
#define W_HEDGES_CUT 0x1fc2u
#define CUT_DP_SPREAD 0x3c
#define CUT_DP_LIST 0x48
#define CUT_DP_CHIPS_X 0x00             // where the chips go
#define CUT_DP_CHIPS_Y 0x02
#define CUT_DP_CHIPS_Z 0x04

// --- a player's death --------------------------------------------------------

#define DEATH_SOUNDED_PC 0x80ce81u      // where the sound comes back
#define DEATH_ENDS_RETURN 0xce8au       // the `PEA` before a state's end
// What `$72` can hold: the routines that end a state, by the stores to it.
#define DEATH_ENDS_COUNT 6
extern const uint16_t DEATH_ENDS[DEATH_ENDS_COUNT];
#define DEATH_HANDS_PC 0x80ce8bu        // `JSR $ECFD`: the ROM's
#define DEATH_SHOWN_PC 0x80ce94u
#define DEATH_SLEEP_PC 0x80cea6u        // `JSL thread_yield`, thirty ticks
#define DEATH_PLAY_PC 0x80cebfu         // `JSL pictures_play`, the list in A
#define DEATH_STILL_PC 0x80ceaau
#define DEATH_COUNTED_PC 0x80cec3u
#define DEATH_BACK_PC 0x80ced4u         // the `RTS` to the thread's loop
#define DEATH_NONE_LEFT_PC 0x80ced5u    // `JSR $F354`: the ROM's
#define DEATH_AGAIN_RETURN 0xced3u      // `JSR $D95F`'s
#define DEATH_MONSTER_PICTURES 0xfd72u  // `$10` while they are the monster
#define DEATH_MONSTER_PICTURE 0x9b89u
#define DEATH_MONSTER_TICKS 0x001e
#define DEATH_LISTS 0x80cf63u           // by which of the two
#define DEATH_PICTURES_AFTER 0x80cf5bu  // ...and the monster's after
#define DEATH_HEALTH 0x000a
#define DEATH_FLASHING_FRAMES 0x0078
#define DEATH_COLLIDE_ID 0x0038         // nothing can touch them
#define W_PLAYER_LIVES 0x1d4cu          // a word a player
#define DEATH_DP_RECORD 0x08
#define DEATH_DP_WHO 0x0c               // which of the two, doubled
#define DEATH_DP_PLAYER 0x0e            // which player, doubled
#define DEATH_DP_PICTURES 0x10
#define DEATH_DP_PICTURE_KEPT 0x2e
#define DEATH_DP_STATE 0x70
#define DEATH_DP_ENDS_BY 0x72           // the routine that ends the state
#define DEATH_DP_FRAMES_LEFT 0x74

// --- level 17's spider and walker ------------------------------------------------

#define SPIDER_ENTER_PC 0x83b27bu     // back from `spawn_has_room`
#define SPIDER_NO_ROOM_PC 0x83b2cfu   // the `RTL` that ends it
#define SPIDER_ALLOC_PC 0x83b346u     // `JSL actor_slot_alloc`
#define SPIDER_MADE_PC 0x83b34au
#define SPIDER_MADE_RETURN 0xb28cu    // `JSR $B346`'s
#define SPIDER_DRAW_RETURN 0xb28fu    // `JSR $B457`'s
#define SPIDER_LOAD 0x000f
#define SPIDER_PICTURE 0xf695u
#define SPIDER_PICTURE_BANK 0x008f
#define SPIDER_HANDLER 0xb525u
#define SPIDER_HANDLER_BANK 0x0083
#define SPIDER_DP_22 0x22             // cleared; I have not read its use
#define WALKER_ENTER_PC 0x81d708u
#define WALKER_NO_ROOM_PC 0x81d759u
#define WALKER_ALLOC_PC 0x81d68du
#define WALKER_MADE_PC 0x81d691u
#define WALKER_MADE_RETURN 0xd716u    // `JSR $D68D`'s
#define WALKER_PICTURES_CALL_PC 0x81d6e1u  // `JSL $81:832C`, the list in A
#define WALKER_SHOWN_PC 0x81d6e5u
#define WALKER_DRAW_RETURN 0xd719u    // `JSR $D5D9`'s
#define WALKER_LOAD 0x0018
#define WALKER_PICTURE 0xe4feu
#define WALKER_PICTURE_BANK 0x0090
#define WALKER_FIRST_PICTURES 0xd6f0u
#define WALKER_HANDLER 0xd7f6u
#define WALKER_HANDLER_BANK 0x0081
#define WALKER_HEALTH 0x0001
#define WALKER_DP_HEALTH 0x0c
// Both records: what can touch them, and their attributes.
#define CREEPER_COLLIDE_ID 0x0003
#define CREEPER_ATTR 0x0c00

// --- the chainsaw maniac's start ----------------------------------------------

#define CHAINSAW_ENTER_PC 0x81983au
#define CHAINSAW_RECORD_CALL_PC 0x81972fu   // `JSL record_begin`
#define CHAINSAW_MADE_PC 0x819733u
#define CHAINSAW_MADE_RETURN 0x984bu        // `JSR $972F`'s
#define CHAINSAW_WANDER_CALL_PC 0x819215u   // `setter_819215`
#define CHAINSAW_SET_PC 0x819854u
#define CHAINSAW_MARK_CALL_PC 0x819856u     // `JSL $81:8191`
#define CHAINSAW_MARKED_PC 0x81985au
#define CHAINSAW_SOUND_CALL_PC 0x81981cu    // `JSL apu_play_sfx`
#define CHAINSAW_PICTURES_PC 0x819820u
#define CHAINSAW_PICTURES_CALL_PC 0x819823u // `JSL $81:832C`
#define CHAINSAW_SHOWN_PC 0x819827u
#define CHAINSAW_SHOWN_RETURN 0x985cu       // `JSR $9811`'s
#define CHAINSAW_HANDLER_CALL_PC 0x81986bu  // `JSL thread_set_handler`
#define CHAINSAW_HANDLED_PC 0x81986fu
#define CHAINSAW_LOAD 0x001c
#define CHAINSAW_FIRST_PICTURE_WAIT 0x0007
#define CHAINSAW_START_TURN 0x0004
#define CHAINSAW_START_WAY 0x0004
#define CHAINSAW_PICTURE 0xc5f3u
#define CHAINSAW_START_PICTURE_BANK 0x0090
#define CHAINSAW_ATTR 0x0c00
#define CHAINSAW_HEALTH 0x0097
#define CHAINSAW_DP_4A 0x4a                 // set to fifteen; its use I have not read
#define CHAINSAW_4A 0x000f
#define CHAINSAW_DP_KILLER 0x2e             // who hit it last
#define CHAINSAW_DP_7E 0x7e
#define CHAINSAW_SOUND 0x002a
#define CHAINSAW_FIRST_PICTURES 0x9828u
#define CHAINSAW_COLLIDE_ID 0x0003
#define CHAINSAW_HANDLER 0x990bu
#define CHAINSAW_HANDLER_BANK 0x0081
#define CHAINSAW_DP_LIST 0x06               // its entry in the level's list

// --- a player stung, and an ant's flash ---------------------------------------

#define FISHMAN_UNHANDLED_CALL_PC 0x81e4c5u  // `JSL thread_set_handler`
#define FISHMAN_UNHANDLED_PC 0x81e4c9u
#define FISHMAN_SECOND_FREE_PC 0x81e4d0u     // `JSL actor_slot_free`
#define FISHMAN_SCORED_PC 0x81e4d4u          // `port/begin.h`'s
#define FISHMAN_COUNTED_PC 0x81e4dfu
#define FISHMAN_SOUND_CALL_PC 0x81e4fcu      // `JSL apu_play_sfx`
#define FISHMAN_PATROL_COUNTED_PC 0x81e572u
#define FISHMAN_PATROL_SOUND_CALL_PC 0x81e58fu
#define FISHMAN_PATROL_ENDED_PC 0x81e59au
#define FISHMAN_PATROL_FREE_PC 0x81e5a8u     // `JSL actor_slot_free`
#define FISHMAN_PATROL_FREED_PC 0x81e5acu
#define FISHMAN_PATROL_SECOND_FREE_PC 0x81e5b3u  // `JML actor_slot_free`
#define FISHMAN_DP_SECOND 0x42  // a second record, or `$FFFF`
#define FISHMAN_DP_KILLER 0x22  // who killed it: bit 15 is the side
#define FISHMAN_LOAD 0x0015
#define FISHMAN_SOUND 0x0021
#define W_FISHMEN_KILLED 0x1f70u
#define W_KILLS_BY_SIDE 0x1fd8u  // a word for each side

#define EXIT_DOOR_ENTER_PC 0x82f70bu
#define EXIT_DOOR_SLEEP_PC 0x82f7a1u        // `JSL thread_yield`, a tick
#define EXIT_DOOR_LOOK_PC 0x82f7a5u
#define EXIT_DOOR_LOOK_CALL_PC 0x82f7b3u    // `JSL terrain_blocked`
#define EXIT_DOOR_LOOKED_PC 0x82f7b7u
#define EXIT_DOOR_END_PC 0x82f779u          // its `RTL`
#define EXIT_DOOR_TRY_CALL_PC 0x82f7f6u     // `JSL terrain_blocked`, a way
#define EXIT_DOOR_TRIED_PC 0x82f7fau
#define EXIT_DOOR_ABOVE_CALL_PC 0x82f805u   // ...16 above it
#define EXIT_DOOR_ABOVE_PC 0x82f809u
#define EXIT_DOOR_HIGHER_CALL_PC 0x82f814u  // ...32 above
#define EXIT_DOOR_HIGHER_PC 0x82f818u
#define EXIT_DOOR_BELOW_CALL_PC 0x82f823u   // ...16 below
#define EXIT_DOOR_BELOW_PC 0x82f827u
#define EXIT_DOOR_EDGE_CALL_PC 0x82f82du    // `JSL terrain_out_of_bounds`
#define EXIT_DOOR_EDGE_PC 0x82f831u
#define EXIT_DOOR_SOMEONE_CALL_PC 0x82f83au // `JSL actor_obstacle_at_point`
#define EXIT_DOOR_SOMEONE_PC 0x82f83eu
#define EXIT_DOOR_TETHER_CALL_PC 0x82f844u  // `JSL step_tether_blocked`
#define EXIT_DOOR_TETHER_PC 0x82f848u
#define EXIT_DOOR_TILE_CALL_PC 0x82f85eu    // `JSL tilemap_tile_word`
#define EXIT_DOOR_TILE_PC 0x82f862u
#define EXIT_DOOR_TILE_UP_CALL_PC 0x82f871u // ...three tiles up
#define EXIT_DOOR_TILE_UP_PC 0x82f875u
#define EXIT_DOOR_TILE_TOP_CALL_PC 0x82f887u  // ...six
#define EXIT_DOOR_TILE_TOP_PC 0x82f88bu
#define EXIT_DOOR_ALLOC_PC 0x82f8d9u        // `JSL actor_slot_alloc`
#define EXIT_DOOR_MADE_PC 0x82f8ddu
#define EXIT_DOOR_UNHANDLED_CALL_PC 0x82f914u  // `JSL thread_set_handler`, none
#define EXIT_DOOR_HANDLED_PC 0x82f929u
#define EXIT_DOOR_WAIT_CALL_PC 0x82f720u    // `JSL thread_yield`, two ticks
#define EXIT_DOOR_WAIT_PC 0x82f724u
#define EXIT_DOOR_PAUSE_CALL_PC 0x82f740u   // ...ten
#define EXIT_DOOR_PAUSED_PC 0x82f744u
#define EXIT_DOOR_GONE_CALL_PC 0x82f75cu    // `JSL thread_set_handler`, none
#define EXIT_DOOR_FIND_RETURN 0xf70du       // `JSR $F79E`'s
#define EXIT_DOOR_MAKE_RETURN 0xf71cu       // `JSR $F8D9`'s
#define EXIT_DOOR_WAYS 0x82f8b1u            // a step across and down, each way
#define EXIT_DOOR_OTHER_PLAYER 0x82f8d5u    // a word for each player: the other
#define EXIT_DOOR_WAYS_END 0x0024           // past the last way
#define EXIT_DOOR_FIRST_WAY 0x0004
#define EXIT_DOOR_TRIES 0x0008
#define EXIT_DOOR_LOAD 0x0005
#define EXIT_DOOR_PICTURE 0xec16u
#define EXIT_DOOR_PICTURE_BANK 0x008f
#define EXIT_DOOR_ID 0x0037           // what touches it, `exit_door_collide`
#define EXIT_DOOR_DP_X 0x00                 // where it may go
#define EXIT_DOOR_DP_Y 0x02
#define EXIT_DOOR_DP_PLAYER_RECORD 0x04     // given: whose way it looks along
#define EXIT_DOOR_DP_PLAYER 0x06            // ...and which player, 0 or 2
#define EXIT_DOOR_DP_RECORD 0x08
#define EXIT_DOOR_DP_TRIES 0x0c
#define EXIT_DOOR_DP_WAY 0x0e
#define EXIT_DOOR_DP_COLUMN 0x10
#define EXIT_DOOR_DP_ROW 0x12

#define SLIME_DEATH_PC 0x81ccf7u         // `port/slime.h`'s frame leaves here
#define SLIME_SCORED_CALL_PC 0x81cd01u   // `JSL score_add`
#define SLIME_RECORD_END_PC 0x81cd0cu    // `port/begin.h`'s
#define W_SLIMES_KILLED 0x1f6cu
#define SLIME_WORTH 0x0200
#define SLIME_DP_KILLER 0x22
#define DECOY_DEATH_PC 0x81f1dbu         // `port/decoy.h`'s frame leaves here
#define DECOY_UNHANDLED_CALL_PC 0x81f1e2u  // `JSL thread_set_handler`
#define W_DECOYS_OUT 0x1f90u
// Its start: its weight, its record and its first pictures, under its
// `JSR $F200`. Then, past its handler, its first sleep.
// A creature bubbled, `$81:8404`, which `enemy_bubble_react` in
// `port/collide.h` wakes its thread into. It keeps on its stack its handler,
// the handler's bank a byte, its picture and its collide id; is nothing to
// hit, in the bubble's picture bank; and has the bubble's handler. Three
// hundred ticks later, `$81:8446`, it has back what it kept, and `JML
// thread_set_handler` names the handler again.
#define BUBBLED_PC 0x818404u
#define BUBBLED_HANDLER_CALL_PC 0x818433u  // `JSL thread_set_handler`
#define BUBBLE_OVER_PC 0x818446u
#define BUBBLE_OVER_JML_PC 0x81845au       // `JML thread_set_handler`
#define BUBBLED_COLLIDE_ID 0x0036
#define BUBBLED_PICTURE_BANK 0x0090u
#define BUBBLED_HANDLER 0x845eu
#define BUBBLED_BANK 0x0081u
// The thing record 36 places, whose wait for hits is `port/waits.h`'s: its
// weight, `JSR $F2EC` to its record, and the handler that counts its hits.
#define BREAKABLE_ENTER_PC 0x82f25du
#define BREAKABLE_ALLOC_PC 0x82f2ecu        // `JSL actor_slot_alloc`
#define BREAKABLE_MADE_PC 0x82f2f0u
#define BREAKABLE_MADE_RETURN 0xf269u       // `JSR $F2EC`'s
#define BREAKABLE_HANDLER_CALL_PC 0x82f275u // `JSL thread_set_handler`
#define BREAKABLE_LOAD 0x0008
#define BREAKABLE_PICTURE 0xd526u
#define BREAKABLE_PICTURE_BANK 0x008fu
#define BREAKABLE_COLLIDE_ID 0x0003
#define BREAKABLE_HITS 0x0005
#define BREAKABLE_HANDLER 0xf330u
#define BREAKABLE_BANK 0x0082u
#define BREAKABLE_DP_X 0x12             // where it was begun, kept
#define BREAKABLE_DP_Y 0x14
#define BREAKABLE_TICKS 0x0002          // of its first picture
// ...and record 48's, which waits for a player near (`port/waits.h`): its
// weight and its count, `JSR $A410` to `record_begin`, and from there its
// record's picture and the rest of its page, as far as its handler.
#define FRANKENSTEIN_ENTER_PC 0x81a455u
#define FRANKENSTEIN_BEGIN_CALL_PC 0x81a410u  // `JSL record_begin`
#define FRANKENSTEIN_BEGUN_PC 0x81a414u
#define FRANKENSTEIN_BEGUN_RETURN 0xa466u     // `JSR $A410`'s
#define FRANKENSTEIN_HANDLER_CALL_PC 0x81a46du
#define FRANKENSTEIN_LOAD 0x001c
#define FRANKENSTEIN_COUNT 0x0007
#define FRANKENSTEIN_PICTURE 0xd11bu
#define FRANKENSTEIN_PICTURE_BANK 0x0090u
#define FRANKENSTEIN_FLAGS 0x8008u
#define FRANKENSTEIN_ATTR 0x0c00u
#define FRANKENSTEIN_HANDLER 0xa638u
#define FRANKENSTEIN_BANK 0x0081u
#define DECOY_ENTER_PC 0x81f159u
#define DECOY_ALLOC_PC 0x81f200u          // `JSL actor_slot_alloc`
#define DECOY_MADE_PC 0x81f204u
#define DECOY_MADE_RETURN 0xf165u         // `JSR $F200`'s
#define DECOY_PICTURES_CALL_PC 0x81f24eu  // `JSL pictures_resume`
#define DECOY_FIRST_PC 0x81f166u
#define DECOY_FIRST_SLEEP_PC 0x81f169u    // `JSL thread_yield`
#define DECOY_LOAD 0x0008
#define DECOY_START_PICTURE 0xd659u
#define DECOY_START_PICTURE_BANK 0x008fu
#define DECOY_COLLIDE_ID 0x0038
#define DECOY_START_PICTURES 0xf27eu
#define DECOY_DP_SPRITE 0x08             // and `port/decoy.h`'s fields
#define DECOY_DP_40 0x40                  // set, and nothing here reads it
#define DECOY_TURNS 0x0064
#define DECOY_FIRST_TICKS 0x000a
#define WEREWOLF_DEAD_PC 0x81ac2au
#define WEREWOLF_UNHANDLED_CALL_PC 0x81ac2eu
#define WEREWOLF_UNHANDLED_PC 0x81ac32u
#define WEREWOLF_SECOND_FREE_PC 0x81ac39u  // `JSL actor_slot_free`
#define WEREWOLF_SCORED_PC 0x81ac3du       // `port/begin.h`'s
#define WEREWOLF_DP_SECOND 0x38            // a second record, or `$FFFF`
// After `score_add`: one more killed, nothing can touch it, a sound, and its
// last pictures.
#define WEREWOLF_COUNTED_PC 0x81ac48u
#define WEREWOLF_COUNTED_SOUND_PC 0x81ac5cu  // `JSL apu_play_sfx`
#define WEREWOLF_HEARD_PC 0x81ac60u
#define WEREWOLF_LAST_PICTURES_CALL_PC 0x81ac63u  // `JSL pictures_play`
#define W_WEREWOLVES_KILLED 0x1f80u
#define WEREWOLF_DP_RECORD 0x08
#define WEREWOLF_PICTURE_BANK 0x008f
#define WEREWOLF_SFX 0x002d
#define WEREWOLF_LAST_PICTURES 0xac7au
// The vampire's kill (`port/vampire.h`), after `score_add`: one more killed,
// and one more by the killer's side, which VAMPIRE DESTROYED pays for
// (`port/level_end.h`); then its end, `$82:EF78` (`port/begin.h`).
#define VAMPIRE_COUNTED_PC 0x82ef66u
#define VAMPIRE_COUNTED_END_PC 0x82ef78u
#define W_VAMPIRES_KILLED 0x1f82u
#define W_VAMPIRE_KILLS_BY_SIDE 0x1fecu  // a word for each side
#define VAMPIRE_DP_KILLER 0x34
#define CHAINSAW_DEATH_PC 0x819887u      // `port/chainsaw.h`'s frame leaves here
#define CHAINSAW_UNHANDLED_CALL_PC 0x81988bu
#define CHAINSAW_UNHANDLED_PC 0x81988fu
#define CHAINSAW_SCORED_CALL_PC 0x819897u  // `JSL score_add`
#define CHAINSAW_SCORED_PC 0x81989bu
#define CHAINSAW_LAST_PICTURES_CALL_PC 0x8198bbu  // `JSL pictures_play`
#define W_CHAINSAWS_KILLED 0x1f6eu
#define W_CHAINSAW_KILLS_BY_SIDE 0x1fd0u  // a word for each side
#define CHAINSAW_WORTH 0x0800
#define CHAINSAW_DP_KILLER 0x2e        // who killed it: bit 15 is the side
#define CHAINSAW_PICTURE_BANK 0x0090
#define CHAINSAW_LAST_PICTURES 0x98ddu
#define ZOMBIE_UNSCORED_PC 0x818ef0u
#define ZOMBIE_FREE_CALL_PC 0x818ef2u  // `JSL actor_slot_free`
#define ZOMBIE_FREED_PC 0x818ef6u
#define ZOMBIE_RTL_PC 0x818f02u
#define ZOMBIE_8ED5_LOAD 0x0018
#define ZOMBIE_DP_RECORD 0x08
// The hunting clone's end is the zombie's (`$81:8F55`, `port/clone.h`): the
// same eleven instructions twice over, at these addresses.
#define CLONE_UNSCORED_PC 0x818f70u
#define CLONE_FREE_CALL_PC 0x818f72u
#define CLONE_FREED_PC 0x818f76u
#define CLONE_RTL_PC 0x818f82u

#define REST_BEGUN_PC 0x80f354u
#define REST_SLEEP_PC 0x80f35cu         // `JSL thread_yield`, a tick
#define REST_ENDED_PC 0x80f360u
#define REST_RTS_PC 0x80f365u
#define PLAYER_OUT_NOTED_PC 0x80ced8u
#define PLAYER_OUT_LINK_PC 0x80cee2u    // `JSL $80:C1CF`
#define PLAYER_OUT_PC 0x80cee6u
#define PLAYER_OUT_SLEEP_PC 0x80cf05u   // `JSL thread_yield`, for good
#define PLAYER_OUT_SLEPT_PC 0x80cf09u
#define PLAYER_OUT_FILE_CALL_PC 0x80cf27u  // `JSL player_record_file`
#define PLAYER_OUT_FILED_PC 0x80cf2bu
#define PLAYER_OUT_FREE_CALL_PC 0x80cf2du
#define PLAYER_OUT_FREED_PC 0x80cf31u
#define PLAYER_OUT_FREE_2_CALL_PC 0x80cf33u
#define PLAYER_OUT_FREED_2_PC 0x80cf37u
#define PLAYER_OUT_DROP_CALL_PC 0x80cf55u  // `JSL $80:C97F`
#define PLAYER_OUT_DROPPED_PC 0x80cf59u
#define PLAYER_OUT_RTL_PC 0x80cf5au
#define PLAYER_OUT_SENTINEL 0xfd72u     // what `$10` holds on that page
#define PLAYER_OUT_LOAD 0x001c
#define PLAYER_OUT_FOREVER 0x7fff
#define PLAYER_OUT_DROP_RANGE 0x0032
#define PLAYER_OUT_DP_PLAYER 0x0e
#define PLAYER_OUT_DP_SENTINEL 0x10
#define PLAYER_OUT_DP_RECORD 0x08
#define PLAYER_OUT_DP_SECOND 0x0a
#define PLAYER_OUT_DP_X 0x30
#define PLAYER_OUT_DP_Y 0x32
#define PLAYER_OUT_DP_INVENTORY 0x66
#define PLAYER_OUT_ITEM 0x0012          // the tenth item's count, in the inventory
#define W_DROPPED_WEAPON 0x1fbeu
#define W_DROPPED_ITEM 0x1fc0u

#define BOLT_ENTER_PC 0x81f808u
#define BOLT_SOUND_CALL_PC 0x81f815u     // `JSL apu_play_sfx`
#define BOLT_SOUND 0x0015
#define BOLT_HEARD_PC 0x81f819u
#define BOLT_ALLOC_PC 0x81f89du          // `JSL actor_slot_alloc`
#define BOLT_MADE_PC 0x81f8a1u
#define BOLT_MADE_RETURN 0xf81bu         // `JSR $F89D`'s
#define BOLT_START_HEIGHT 0x001e
#define BOLT_FLAGS 0x81f964u             // a word for each way
#define BOLT_IDS 0x81f918u               // what it hits, by the player
#define BOLT_PICTURE_BANK 0x0090
#define BOLT_DRAWN 0x8002u
#define BOLT_FRAMES 0x003c
#define BOLT_HANDLER 0xf8fcu
#define BOLT_HANDLER_BANK 0x0081
#define BOLT_DP_X 0x00
#define BOLT_DP_Y 0x02
#define BOLT_DP_PLAYER 0x06
#define BOLT_DP_SPRITE 0x08

#define PAUSE_DIM_PC 0x8089c2u           // after the volume went down
#define PAUSE_HELD_PC 0x8089c8u          // `port/hold.h`'s first wait
#define PAUSE_OVER_PC 0x8089e9u          // after the last
#define PAUSE_VOLUME_UP_CALL_PC 0x8089ecu  // `JSL`, A `$7F`
#define PAUSE_VOLUME_UP 0x007f
#define PAUSE_RESUMED_PC 0x8089f0u
#define PAUSE_RTL_PC 0x8089f6u
#define PAUSE_DIM 0x0008
#define PAUSE_BRIGHT 0x000f
#define MAIN_FRAME_REST_PC 0x808537u     // after the HUD's refresh

#define STUNG_PC 0x80f9d0u
#define STUNG_SPARED_PC 0x80f9cfu        // the `RTS`, with no health to take
#define STUNG_SOUND_CALL_PC 0x80f9deu    // `JSL apu_play_sfx`
#define STUNG_HEARD_PC 0x80f9e2u
#define STUNG_UNHANDLED_PC 0x80f36cu     // `player_unhandled`
#define STUNG_HEARD_RETURN 0xf9eeu       // its `JSR`'s
#define STUNG_HELD_PC 0x80f9f2u
#define STUNG_YIELD_PC 0x80f9f5u
#define STUNG_SOUND 0x0019
#define STUNG_TICKS 0x004b
#define STUNG_PICTURES 0x80fa02u         // a word for each way, by `$0C`
#define STUNG_DP_PLAYER 0x0e             // which player, doubled
#define STUNG_DP_WAY 0x0c
#define STUNG_DP_RECORD 0x08

// `$81:C6D9`, where a footballer's pictures come back to: `JMP $C6EC`, and
// the state for its next pass. It stops at the state's `RTS`. Not a stretch
// of its own: `footballer_off_shown` in `src/cosim/routines.c` goes on from
// it to the pass's end. See `port/football.h`.
#define FOOTBALLER_OFF_PICTURED_PC 0x81c6d9u
#define FOOTBALLER_OFF_RTS_PC 0x81c6f1u

#define ANT_FLASH_PC 0x81baecu
#define ANT_FLASH_YIELD_PC 0x81baf7u
#define ANT_FLASHED_PC 0x81bafbu
#define ANT_FLASHED_RTL_PC 0x81bb04u
#define ANT_FLASH_ATTR 0x0c00
#define ANT_FLASH_TICKS 0x0002
#define ANT_DP_RECORD 0x08

// --- a player caught in a slime's splash ---------------------------------------

#define SLIMED_PC 0x80dc1eu
#define SLIMED_REST_RETURN 0xdc63u   // `JSR $F354`'s
#define SLIMED_YIELD_PC 0x80f35cu    // `JSL thread_yield`, A already 1
#define SLIMED_RESTED_PC 0x80f360u
#define SLIMED_HANDS_RETURN 0xdc6cu  // `JSR $F38D`'s
#define SLIMED_COVER_PICTURE 0xa54bu
#define SLIMED_COVER_BANK 0x0090
#define SLIMED_COVER_HEIGHT 0x0020
#define SLIMED_COVER_OVER 0x8000u    // the cover's flags: the player's, and this
#define SLIMED_STATE 0x000c
#define SLIMED_ENDS_BY 0xdc70u
#define SLIMED_TICKS 0x0186
#define SLIMED_WEAPON 0x000f         // what they hold meanwhile
#define SLIMED_ITEM 0x000d
#define SLIMED_DP_RECORD 0x08
#define SLIMED_DP_COVER 0x0a
#define SLIMED_DP_PLAYER 0x0e        // which player, doubled
#define SLIMED_DP_X 0x30
#define SLIMED_DP_Y 0x32
#define SLIMED_DP_TICKS 0x56         // the countdown that ends the state
#define SLIMED_DP_STATE 0x70
#define SLIMED_DP_ENDS_BY 0x72
#define W_MAIN_1E7E 0x1e7eu            // 1 before the sound's bank is sent
#define W_SCORES_SET 0x2124u           // in `$7E`
#define W_LEVELS_FADED 0x1fb6u
#define MAIN_NEIGHBOURS 0x0010         // ten, in decimal
// The level a game starts at is the operand of `$80:85F0`, and it is read
// from the cartridge: `--level` writes another there. See `src/levelstart.h`.
#define MAIN_FIRST_LEVEL_OPERAND 0x8085f1u
#define LEVEL_RECORD_WORDS 0x9f0036u   // two words by the level's offset
#define MAIN_DP_SOUND_SET 0x18
#define MAIN_DP_LEVEL 0x10
#define CACHE_THREAD 0xb947u
#define CACHE_THREAD_BANK 0x0080

// --- the martian -------------------------------------------------------------

#define MARTIAN_ENTER_PC 0x8199dfu
#define MARTIAN_BEGIN_CALL_PC 0x8199e9u  // `JSR martian_begin`
#define MARTIAN_BEGUN_PC 0x8199ecu
#define MARTIAN_FIRST_SLEEP_PC 0x8199f2u
#define MARTIAN_WALKS_PC 0x819d2au
#define MARTIAN_LOAD 0x001e
#define MARTIAN_ARRIVAL_ENTER_PC 0x819a3eu
#define MARTIAN_PLACE_RETURN 0x9a40u           // its `JSR $9AA2`
#define MARTIAN_PLACE_GROUND_PC 0x819aaeu      // `JSL terrain_blocked`
#define MARTIAN_PLACE_GROUNDED_PC 0x819ab2u
#define MARTIAN_PLACE_EDGE_PC 0x819ab8u        // `JSL terrain_out_of_bounds`
#define MARTIAN_PLACED_PC 0x819abcu
#define MARTIAN_ARRIVAL_NONE_PC 0x819a8du      // the `RTL` with no martian
#define MARTIAN_ARRIVAL_BEGIN_CALL_PC 0x819a4du
#define MARTIAN_ARRIVAL_BEGUN_PC 0x819a50u
#define MARTIAN_ARRIVAL_BELOW_TOP 0x0008

// --- the werewolf ------------------------------------------------------------

#define WEREWOLF_ENTER_PC 0x81abf5u
#define WEREWOLF_NONE_PC 0x81abfau          // the `RTL` with none due
#define WEREWOLF_HOWL_CHECK_PC 0x81ac05u    // `LDA $1F52`, which is watched
#define WEREWOLF_SAMPLE_SET 0x0001
#define WEREWOLF_HOWL_CALL_PC 0x81ac10u     // `JSL apu_play_sfx`
#define WEREWOLF_HOWLED_PC 0x81ac14u
#define WEREWOLF_MAKE_PC 0x81a643u          // `JSL record_begin`
#define WEREWOLF_MAKE_RETURN 0xac16u        // what the `JSR` to it pushed
#define WEREWOLF_MADE_PC 0x81a647u
#define WEREWOLF_FIRST_SLEEP_PC 0x81a686u
#define WEREWOLF_WOKE_PC 0x81a68au
#define WEREWOLF_HANDLER_CALL_PC 0x81abbcu  // `JSL thread_set_handler`
#define WEREWOLF_RUNS_PC 0x81abc0u
#define WEREWOLF_STRUCK_PC 0x81ab02u
#define WEREWOLF_RESTED_PC 0x81aa9bu
#define WEREWOLF_CROUCHED_PC 0x81a9afu
#define WEREWOLF_LOAD 0x001c
#define WEREWOLF_HOWL 0x002b
#define WEREWOLF_FIRST_PICTURE 0xe9d8u
#define WEREWOLF_LEAP_PICTURE 0xeb89u
#define WEREWOLF_START_ATTR 0x0c00
#define WEREWOLF_COLLIDE_ID 0x0003
#define WEREWOLF_FIRST_SLEEP 0x0014
#define WEREWOLF_HEALTH 0x000a
#define WEREWOLF_HANDLER 0xac92u
// In the air it is drawn over everything, and is nothing to hit.
#define WEREWOLF_LEAP_FLAGS 0x8008u
#define WEREWOLF_DP_PLACED_X 0x00
#define WEREWOLF_DP_PLACED_Y 0x02
#define WEREWOLF_DP_7E 0x7e                 // cleared; I have not read its use

// --- one instruction or two --------------------------------------------------

#define PAGE_CLEAR_PC 0x80d0ddu       // `STZ $1C`, and an `RTS`
#define PAGE_CLEAR_RTS_PC 0x80d0dfu
#define PAGE_CLEARED 0x1c
#define PAGE_PULL_PC 0x80abd1u        // `PLD`, and an `RTL`
#define PAGE_PULL_RTL_PC 0x80abd2u
#define CARRY_SET_PC 0x80e78eu        // `SEC`, and an `RTS`
#define CARRY_SET_RTS_PC 0x80e78fu
#define VICTIM_GONE_PC 0x8181efu      // `LDA $06`, and `JSL victim_mark_gone`
#define VICTIM_GONE_CALL_PC 0x8181f1u
#define VICTIM_DP_GONE 0x06
// A player out by the exit.
#define PLAYER_LEAVES_PC 0x80faf6u
#define PLAYER_LEAVES_RTS_PC 0x80faffu  // the `RTS` to what ends the state
#define PLAYER_LEAVES_BACK 0xfaffu      // ...and the `PEA` under it
#define PLAYER_LEFT_PC 0x80fb00u
#define PLAYER_LEFT_CALL_PC 0x80fb0cu   // `JSR player_unhandled`
#define PLAYER_LEFT_PANEL_PC 0x80fb18u
#define PLAYER_LEFT_PANEL_CALL_PC 0x80fb1au
#define PLAYER_LEFT_SHOWN_PC 0x80fb1eu
#define PLAYER_LEFT_SLEEP_PC 0x80fb31u
#define PLAYER_GONE_PC 0x80fb35u
#define PLAYER_GONE_SLEEP_PC 0x80fb4cu
#define PLAYER_LOAD 0x001cu
#define PLAYER_LEFT_PICTURE 0x9b89u
#define PLAYER_LEFT_TICKS 0x0050u
#define PLAYER_GONE_TICKS 0x7fffu
#define MONSTER_ENDS_PC 0x80da54u
#define MONSTER_ENDS_SHOWN_PC 0x80da58u  // with nothing at `$72`: its pictures
#define MONSTER_ENDS_CALL_PC 0x80da6fu   // `JSR player_hands_back`
// The monster's punch, standing.
#define PUNCH_FIRST_PC 0x80ef82u
#define PUNCH_AGAIN_PC 0x80ef8fu
#define PUNCH_SECOND_PC 0x80ef98u
#define PUNCH_THIRD_PC 0x80efaau
#define PUNCH_OVER_PC 0x80efb9u
#define PUNCH_FIRST_JSR 0xef82u       // each `JSR $F051`
#define PUNCH_SECOND_JSR 0xef9du
#define PUNCH_THIRD_JSR 0xefacu
#define PUNCH_TELL_PC 0x80f086u       // `JSL actor_notify_box`
#define PUNCH_SHOW_CALL_PC 0x80ef95u  // `JSR pose_show`
#define PUNCH_RTS_PC 0x80efbbu
#define PUNCH_NEXT_PICTURE 0x0004u
#define PUNCH_SECOND_CYCLE 0x0002u
// The monster potion's change.
#define MONSTER_PICTURES_PC 0x80d9b3u
#define MONSTER_PICTURES 0x80d9ecu        // by `$0C`
#define MONSTER_PICTURES_CALL_PC 0x80d9b8u
#define MONSTER_BEGUN_PC 0x80d9bcu
#define MONSTER_FRAMES 0x07d0u
#define MONSTER_ENDS_BY 0xda54u
#define MONSTER_FACING 0x000au
#define MONSTER_WEAPON 0x000eu            // what the swap gives it to hold
#define MONSTER_ITEM 0x000cu
#define MONSTER_SWAP_CALL_PC 0x80d9e3u    // `JSR player_hands_swap`
// The fourth's tile, back from its block.
#define FOURTH_PRIZE_PC 0x80ea02u
#define FOURTH_PRIZE_RETURN 0xea15u      // what `JSL $82:DC57` pushes
#define FOURTH_PRIZE_DRAW_PC 0x82dc5bu   // `JSL rng_next`
#define FOURTH_PRIZE_ITEM_PC 0x82dc72u   // `JSL object_put_back`
#define FOURTH_PRIZE_THREAD_PC 0x82dc9bu // `JSL thread_spawn`
#define FOURTH_PRIZE_RTL_PC 0x82dc9fu
#define FOURTH_PRIZE_ACROSS 0x000au
#define FOURTH_PRIZE_UP 0x000cu
#define FOURTH_PRIZES 0x81e79fu          // by the byte drawn
#define FOURTH_PRIZE_NONE 0x00ffu
#define FOURTH_PRIZE_BESIDE 0x00feu
#define W_FOURTH_FACING 0x0038u          // the way faced, kept on page zero
#define FOURTH_FACING_LOW 0x0006u        // up to here, or...
#define FOURTH_FACING_HIGH 0x000eu       // ...from here
#define FOURTH_BESIDE_LEFT 0x000au       // the thread's place, from X and Y
#define FOURTH_BESIDE_UP 0x0014u
#define FOURTH_BESIDE_THREAD 0xdca0u
#define FOURTH_BESIDE_THREAD_BANK 0x0082u
// The level's ending, `$80:8544`, where the main loop goes with no neighbours
// left to save (`port/mainloop.h`). With neither player having one to their
// name, the game is lost. Otherwise each player playing has a thread begun,
// `$82:F70B`, with their record and which player they are: their way out.
// Then the second loop's first sleep.
#define LEVEL_ENDING_PC 0x808544u
#define LEVEL_ENDING_FIRST_CALL_PC 0x808561u   // `JSL thread_spawn`
#define LEVEL_ENDING_SECOND_PC 0x808565u
#define LEVEL_ENDING_SECOND_CALL_PC 0x80857au
#define LEVEL_ENDING_SLEEP_PC 0x80857eu        // `LDA #2`, and the sleep
#define W_PLAYERS_RESCUED 0x1f9cu              // a word for each player
#define W_PLAYER_RECORDS 0x00d2u               // ...and their records
#define LEVEL_ENDING_THREAD 0xf70bu
#define LEVEL_ENDING_THREAD_BANK 0x0082u
#define LEVEL_ENDING_DP_RECORD 0x04            // what the thread is begun with
#define LEVEL_ENDING_DP_PLAYER 0x06
// The main thread back from the level's loop, `$80:84D5`: `BCS` to the game
// over, which carry set says, or on to the next level.
#define MAIN_BACK_PC 0x8084d5u
#define MAIN_BACK_NEXT_PC 0x8084d7u   // `JSL $80:866F`
#define MAIN_BACK_OVER_PC 0x8084fcu   // `JSL $80:8A00`
// The main loop's way out with nobody playing: `SEC`, and its `RTL`.
#define GAME_LOST_PC 0x8085b5u
#define GAME_LOST_RTL_PC 0x8085b6u
// The last frame stuck in slime, back from what they held being given back:
// the ordinary state, `STZ $70`, and the state's `RTS`. See `port/stuck.h`.
#define STUCK_FREED_PC 0x80dc73u
#define STUCK_FREED_RTS_PC 0x80dc75u
// `$80:C1CF`, a player's side of the status panel blanked: `BNE` on the
// player the caller's `LDX` named, and the `JSR` to that player's panel.
// `port/hud.h` has the rest.
#define PANEL_SIDE_PC 0x80c1cfu
#define PANEL_SIDE_1_CALL_PC 0x80c1d1u
#define PANEL_SIDE_2_CALL_PC 0x80c1f5u

// --- a player's flashing ended -----------------------------------------------

#define FLASHING_END_PC 0x80d97fu
#define FLASHING_END_CALL_PC 0x80d99du  // `JSR $F377`

// --- a player shown hurt -----------------------------------------------------

#define PLAYER_HIT_SHOW_PC 0x80d056u
#define PLAYER_HIT_SHOW_CALL_PC 0x80d066u   // `JSR $F36C`
#define PLAYER_HIT_SHOWN_PC 0x80d070u
#define PLAYER_HIT_SHOWN_CALL_PC 0x80d07eu  // `JSR $F382`
#define PLAYER_DP_RECORD 0x08
#define PLAYER_DP_16 0x16                   // cleared after; I have not read
#define PLAYER_DP_50 0x50                   // the use of either
#define PLAYER_HURT_PICTURE_BANK 0x0090

// --- a locked door's buzz ---------------------------------------------------

#define DOOR_BUZZ_PC 0x82defbu
#define DOOR_BUZZ_SOUND_PC 0x82df06u  // `JSL apu_play_sfx`, sound in A
#define DOOR_BUZZED_PC 0x82df1fu
#define DOOR_END_PC 0x82df22u         // the thread's `RTL`
#define DOOR_BUZZ_SFX 0x0013
#define W_DOOR_BUZZING 0x1ff4u        // in `$7E`: one is buzzing

// --- a slime's start ---------------------------------------------------------

#define SLIME_ENTER_PC 0x81cccfu
#define SLIME_BEGIN_CALL_PC 0x81ccd9u   // `JSR slime_begin`
#define SLIME_SHOWN_PC 0x81ccacu        // where its first pictures come back
#define SLIME_BEGIN_RETURN 0xccdbu      // `slime_begin`'s return, less one
#define SLIME_WAY_CALL_PC 0x81ccdcu     // `JSR $C9E4`
#define SLIME_WAY_RETURN 0xccdeu
#define SLIME_DRAW_PC 0x81c9e4u         // `JSL rng`
#define SLIME_WAY_PC 0x81c9e8u
#define SLIME_FIRST_YIELD_PC 0x81cce4u
#define SLIME_LOAD 0x0020
#define SLIME_HANDLER 0xcddeu
#define SLIME_HANDLER_BANK 0x0081
#define SLIME_STATE_FIRST 0xc9fau       // the loop's state after the way
#define SLIME_YIELD_TICKS 4
#define SLIME_DP_WAY 0x1a               // a way of four, as 2, 6, 10 or 14
#define SLIME_DP_STATE 0x16
#define SLIME_DP_22 0x22

// --- the vampire's states that sleep -----------------------------------------

#define VAMPIRE_LAND_SFX_CALL_PC 0x82ea40u       // `JSL apu_play_sfx`
#define VAMPIRE_LAND_SOUNDED_PC 0x82ea44u
#define VAMPIRE_LAND_PICTURES_CALL_PC 0x82ea47u  // `JSL pictures_resume`
#define VAMPIRE_LANDED_PC 0x82ea4bu
#define VAMPIRE_LAND_DRAW_PC 0x82ea5au           // `JSL rng`
#define VAMPIRE_LAND_WAY_PC 0x82ea5eu
#define VAMPIRE_LAND_SOUNDS 0x0003               // its sound's sample set
#define VAMPIRE_LAND_SFX 0x0027
#define VAMPIRE_LAND_PICTURES 0xea66u
#define VAMPIRE_LANDED_HEALTH 0x0009
#define VAMPIRE_FIRED_PC 0x82ee1bu
#define VAMPIRE_SPAWN_CALL_PC 0x82ee3bu          // `JSL thread_spawn`
#define VAMPIRE_SPAWNED_PC 0x82ee3fu
#define VAMPIRE_SPAWN_SLEEP_PC 0x82ee46u         // `JSL thread_yield`
#define VAMPIRE_SPAWN_AGAIN_PC 0x82ee4au
#define VAMPIRE_FIRED_CALL_PC 0x82ee5eu          // `JSL pictures_resume`
#define VAMPIRE_NEXT_PC 0x82ee62u
#define VAMPIRE_TRACKERS_FROM 0x0036  // less its strength, by 32, and one more
#define VAMPIRE_TRACKER_THREAD 0xf03eu
#define VAMPIRE_SPAWN_TICKS 0x0003
#define VAMPIRE_FIRED_PICTURES 0x82eeafu         // a list for each way
#define VAMPIRE_HIDDEN_PC 0x82eba3u
// Its start: the neighbour's place in the level's list marked gone, its
// weight, its record, and with the sample set that has it a sound. Then the
// state `$82:E827` sets, coming at whoever is nearest, and the first sleep.
#define VAMPIRE_ENTER_PC 0x82ef36u
#define VAMPIRE_GONE_CALL_PC 0x82ef38u     // `JSL victim_mark_gone`
#define VAMPIRE_GONE_PC 0x82ef3cu
#define VAMPIRE_ALLOC_PC 0x82ef8bu         // `JSL actor_slot_alloc`
#define VAMPIRE_MADE_PC 0x82ef8fu
#define VAMPIRE_MADE_RETURN 0xef48u        // `JSR $EF8B`'s
#define VAMPIRE_START_SFX_CALL_PC 0x82efe9u  // `JSL apu_play_sfx`
#define VAMPIRE_SOUNDED_PC 0x82efedu
#define VAMPIRE_DP_GONE 0x06               // its place in the list
#define VAMPIRE_LOAD 0x0014
#define VAMPIRE_START_PICTURE 0xda44u
#define VAMPIRE_START_ATTR 0x0c00u
#define VAMPIRE_START_TURN 0x0004
#define VAMPIRE_START_STRENGTH 0x0036
#define VAMPIRE_START_SFX 0x0026
#define VAMPIRE_DP_HIT_ID 0x34             // what `$82:EFF0` files
#define VAMPIRE_DP_FREEZE_HITS 0x7e
#define VAMPIRE_BACK_PC 0x82ec61u
#define VAMPIRE_COLLIDE_ID 0x0003

// --- a tracker's start and end, and the vampire's puff -----------------------

#define TRACKER_ENTER_PC 0x82f03eu
#define TRACKER_BEGIN_CALL_PC 0x82f048u  // `JSR tracker_begin`
#define TRACKER_BEGUN_PC 0x82f04bu
#define TRACKER_END_CALL_PC 0x82f069u    // `JSL pictures_resume`
#define TRACKER_LOAD 0x0008
#define TRACKER_END_PICTURE_BANK 0x008f
#define TRACKER_END_PICTURES 0xf07fu
#define PUFF_ENTER_PC 0x82f1e6u
#define PUFF_ALLOC_CALL_PC 0x82f1f0u     // `JSL actor_slot_alloc`
#define PUFF_MADE_PC 0x82f1f4u
#define PUFF_PICTURES_CALL_PC 0x82f22bu  // `JSL pictures_resume`
#define PUFF_LOAD 0x0005
#define PUFF_PICTURE 0xa01du
#define PUFF_PICTURE_BANK 0x0090
#define PUFF_PICTURES 0xf241u
#define PUFF_DP_RECORD 0x08
#define PUFF_DP_X 0x00                   // where it was asked for
#define PUFF_DP_Y 0x02
#define PIECE_ENTER_PC 0x83b1ddu
#define PIECE_ALLOC_CALL_PC 0x83b1e7u    // `JSL actor_slot_alloc`
#define PIECE_MADE_PC 0x83b1ebu
#define PIECE_SOUND_CALL_PC 0x83b222u    // `JSL apu_play_sfx`
#define PIECE_HEARD_PC 0x83b226u
#define PIECE_PICTURES_CALL_PC 0x83b229u // `JSL pictures_resume`
#define PIECE_LOAD 0x0007
#define PIECE_PICTURE 0xb9e0u
#define PIECE_PICTURE_BANK 0x0090
#define PIECE_FLAGS (ACTOR_DRAW | ACTOR_SORT_FIRST | ACTOR_PRIORITY_TOP)
#define PIECE_SOUND 0x0021
#define PIECE_PICTURES 0xb23fu

// --- the big figure's start, and the saucer's --------------------------------

#define FIGURE_ENTER_PC 0x829569u
#define FIGURE_START_RETURN 0x9575u       // what `JSR $94E4` leaves
#define FIGURE_COLOURS_CALL_PC 0x8294eau  // `JSL figure_colours_set`
#define FIGURE_COLOURED_PC 0x8294eeu
#define FIGURE_QUEUE_CALL_PC 0x829506u    // `JSL boss_bg_queue_one`
#define FIGURE_YIELD_PC 0x82950du         // `JSL thread_yield`, A already 1
#define FIGURE_WAITED_PC 0x829511u
#define FIGURE_PART_CALL_PC 0x8294b4u     // `JSL actor_slot_alloc`
#define FIGURE_PART_PC 0x8294b8u
#define FIGURE_CHOOSE_RETURN 0x9578u      // what `JSR $89D2` leaves
#define FIGURE_LOAD 0x0028
#define FIGURE_COLOURS 0xffd1u
#define FIGURE_COLOURS_BANK 0x0092
#define FIGURE_TILES 0x8000u
#define FIGURE_TILES_BANK 0x0093
#define FIGURE_TILES_BYTES 0x2800
#define FIGURE_TILES_VRAM 0x5000
#define FIGURE_PLANE_AT_X 0x003c
#define FIGURE_PLANE_AT_Y 0x0082
#define FIGURE_PARTS 4
#define FIGURE_PART_COLLIDE_ID 0x0003
#define FIGURE_PART_FLAGS 0x8008u
#define FIGURE_HANDLER 0x9660u
#define FIGURE_HANDLER_BANK 0x0082
#define FIGURE_DP_PLACE_X 0x00   // given: where it stands
#define FIGURE_DP_PLACE_Y 0x02
#define FIGURE_DP_3C 0x3c        // set to seventy; what reads it I have not found
#define FIGURE_3C 0x0046
// Where each part's `JSR $94B4` comes back to, less one.
extern const uint16_t FIGURE_PART_RETURNS[FIGURE_PARTS];

#define SAUCER_ENTER_PC 0x82873cu
#define SAUCER_COLOURS_CALL_PC 0x82874cu  // `JSL figure_colours_set`
#define SAUCER_COLOURED_PC 0x828750u
#define SAUCER_DRAW_CALL_PC 0x828766u     // `JSL boss_bg_queue`
#define SAUCER_DRAWN_PC 0x82876au
#define SAUCER_QUEUE_CALL_PC 0x828782u    // `JSL boss_bg_queue_one`
#define SAUCER_QUEUED_PC 0x828786u
#define SAUCER_NEAREST_CALL_PC 0x8284d9u  // `JSL actor_nearest`
#define SAUCER_AIMED_PC 0x8284ddu
#define SAUCER_START_RETURN 0x87a9u       // what `JSR $84CE` leaves
#define SAUCER_LOAD 0x0018
#define SAUCER_FIGURE 0xf961u             // for `boss_bg_queue`
#define SAUCER_FIGURE_BANK 0x0097
#define SAUCER_TILES 0xa620u
#define SAUCER_DP_PLACE_X 0x00            // given: where it is
#define SAUCER_DP_PLACE_Y 0x02
#define SAUCER_DP_12 0x12                 // cleared; I have not followed it

// name, first instruction, the one after its last, and how many branches
// in it are always taken. `tools/cycles816.py` prices each.
#define TS_BLOCKS(X) \
  X(TS_LDA_IMM, 0x81d183, 0x81d186, 0) \
  X(TS_LDA_DP, 0x81d2c8, 0x81d2ca, 0) \
  X(TS_LDA_LDY_IMM, 0x8085d3, 0x8085d9, 0) \
  X(TS_LDA_LDX_IMM, 0x808697, 0x80869d, 0) \
  X(TS_LDX_LDY_DP, 0x82f44a, 0x82f44e, 0) \
  X(TS_BRANCH, 0x81d2b1, 0x81d2b3, 0)         /* not taken */ \
  X(TS_JSR, 0x81d299, 0x81d29c, 0) \
  X(TS_WEED_AIM, 0x81d154, 0x81d15b, 0) \
  X(TS_WEED_SHOW, 0x81d15f, 0x81d164, 0) \
  X(TS_WEED_THROW_A, 0x81d168, 0x81d17f, 0) \
  X(TS_WEED_THROW_B, 0x81d18a, 0x81d1a9, 0) \
  X(TS_WEED_THROW_C, 0x81d1b4, 0x81d1d3, 0) \
  X(TS_WEED_REST, 0x81d1de, 0x81d1f4, 0) \
  X(TS_WEED_FATE, 0x81d2ad, 0x81d2b1, 0)      /* LDA $0C : BEQ */ \
  X(TS_WEED_AGAIN, 0x81d29c, 0x81d2a1, 0) \
  X(TS_WEED_KIND, 0x81d2f1, 0x81d2f9, 0)      /* LDA # : STA $0A : JMP */ \
  X(TS_WEED_ENTER, 0x81d28c, 0x81d296, 0) \
  X(TS_WEED_MADE, 0x81d214, 0x81d277, 0) \
  X(TS_WEED_GROWS, 0x81d127, 0x81d12d, 0)     /* LDA # : STA $14 : RTS */ \
  X(TS_FOLLOWER_TILE, 0x82f45c, 0x82f461, 0)  /* AND # : BEQ */ \
  X(TS_FOLLOWER_MARK, 0x82f468, 0x82f472, 0) \
  X(TS_FOLLOWER_LOAD, 0x82f4a4, 0x82f4ae, 0) \
  X(TS_FOLLOWER_SIDE, 0x82f358, 0x82f35f, 0)  /* STA $08 ... BPL */ \
  X(TS_FOLLOWER_LEFT, 0x82f35f, 0x82f369, 1) \
  X(TS_FOLLOWER_RIGHT, 0x82f369, 0x82f371, 0) \
  X(TS_FOLLOWER_MADE, 0x82f371, 0x82f3af, 0) \
  X(TS_MAIN_RESET, 0x8085e1, 0x8085fc, 0) \
  X(TS_MAIN_SCORES_SET, 0x808600, 0x808607, 0) \
  X(TS_MAIN_SOUND_TEST, 0x808607, 0x80860b, 0) \
  X(TS_MAIN_SOUND, 0x80860b, 0x808611, 0) \
  X(TS_MAIN_SOUND_SET, 0x808615, 0x808617, 0) \
  X(TS_LEVEL_RECORD, 0x808849, 0x80885b, 0) \
  X(TS_LEVEL_FADED, 0x808525, 0x80852b, 0) \
  X(TS_LEVEL_COUNT, 0x80867b, 0x808693, 0) \
  X(TS_CACHE_START, 0x80c04c, 0x80c059, 0) \
  X(TS_REP, 0x809126, 0x809128, 0) \
  X(TS_STA_DP, 0x80d16e, 0x80d170, 0) \
  X(TS_FOLLOWER_HIT, 0x82f3f4, 0x82f406, 0) \
  X(TS_SPAWNER_TICK, 0x80c8f9, 0x80c901, 0)      /* LDA abs : AND # : BNE */ \
  X(TS_SPAWNER_WAIT, 0x80c908, 0x80c90d, 0) \
  X(TS_MARTIAN_ENTER, 0x8199df, 0x8199e9, 0) \
  X(TS_MARTIAN_WALKS, 0x819d2a, 0x819d30, 0)  /* LDA # : STA $0A : RTS */ \
  X(TS_RTS, 0x819abf, 0x819ac0, 0) \
  X(TS_ARRIVAL_PLACE, 0x819aa2, 0x819aae, 0) \
  X(TS_ARRIVAL_FREE, 0x819abe, 0x819ac0, 0)   /* CLC : RTS */ \
  X(TS_ARRIVAL_STATE, 0x819db1, 0x819dbb, 0) \
  X(TS_PLAYER_HIT_KEEP, 0x80d056, 0x80d066, 0) \
  X(TS_PLAYER_HIT_BACK, 0x80d070, 0x80d07e, 0) \
  X(TS_FLASHING_END, 0x80d97f, 0x80d998, 0)  /* to the BNE */ \
  X(TS_WOLF_ASK, 0x81abf5, 0x81abfa, 0)      /* LDA abs : BNE */ \
  X(TS_WOLF_HEARD, 0x81ac05, 0x81ac0d, 0)    /* LDA abs : CMP # : BNE */ \
  X(TS_WOLF_MADE, 0x81a647, 0x81a686, 0) \
  X(TS_WOLF_HEALTH, 0x81a68a, 0x81a694, 0)   /* the JMP too */ \
  X(TS_WOLF_UNBLOW, 0x81ab02, 0x81ab0a, 0)   /* the JMP too */ \
  X(TS_WOLF_LEAP, 0x81a9d5, 0x81a9f0, 0)     /* the RTS too */ \
  X(TS_JMP, 0x81a9af, 0x81a9b2, 0) \
  X(TS_STZ_DP, 0x80d0dd, 0x80d0df, 0) \
  X(TS_PLD, 0x80abd1, 0x80abd2, 0) \
  X(TS_SEC, 0x80e78e, 0x80e78f, 0) \
  X(TS_SQUARE_BAND, 0x82f6ba, 0x82f6ea, 0)   /* the RTS too */ \
  X(TS_SQUARE_HANDLER, 0x82f6ab, 0x82f6b0, 0)  /* the RTS too */ \
  X(TS_SQUARE_CALL, 0x82f6f8, 0x82f6ff, 0)   /* PEA : LDA : DEC : PHA : RTS */ \
  X(TS_SQUARE_WHO, 0x82f593, 0x82f597, 0)    /* LDY abs : BEQ */ \
  X(TS_SQUARE_ROW, 0x82f598, 0x82f59e, 0)    /* LDA abs,Y : CMP dp : BCS */ \
  X(TS_SQUARE_BRA, 0x82f5ab, 0x82f5ac, 0) \
  X(TS_SQUARE_HELD, 0x82f5c7, 0x82f5c8, 0)   /* CLC : RTS */ \
  X(TS_SQUARE_LEFT, 0x82f5c9, 0x82f5ca, 0)   /* SEC : RTS */ \
  X(TS_SQUARE_MADE, 0x82f643, 0x82f678, 0)   /* the RTS too */ \
  X(TS_SQUARE_KEPT, 0x82f5fd, 0x82f602, 0) \
  X(TS_SQUARE_CELL_AT, 0x82f678, 0x82f697, 0) \
  X(TS_SQUARE_TILE, 0x82f69b, 0x82f6a5, 0)   /* through `[$28]`, to the cartridge */ \
  X(TS_SQUARE_BACK, 0x82f6a9, 0x82f6ab, 0)   /* PLD : RTS */ \
  X(TS_SQUARE_BOX, 0x82f5cb, 0x82f5f5, 0) \
  X(TS_SQUARE_FREED, 0x82f620, 0x82f623, 0)  /* INC : RTS */ \
  X(TS_SQUARE_SLOT_GIVEN, 0x82f704, 0x82f70a, 0)  /* LDA $24 : JSL */ \
  X(TS_SQUARE_SLOT_FREE, 0x82f561, 0x82f577, 0)  /* the RTL too */ \
  X(TS_SQUARE_DONE, 0x82f700, 0x82f703, 0)   /* LDA $0A : BEQ */ \
  X(TS_FLYOFF_DRESS, 0x82e0bf, 0x82e0f9, 0)  /* to the TAY */ \
  X(TS_FLYOFF_WAY, 0x82e102, 0x82e10d, 0) \
  X(TS_FLYOFF_STEP, 0x82e115, 0x82e130, 0)   /* to the BPL */ \
  X(TS_FLYOFF_END, 0x82e131, 0x82e132, 0)    /* LDA $08 */ \
  X(TS_BYSTANDER_DRESS, 0x82de87, 0x82dee3, 0)  /* to the LDY # */ \
  X(TS_BYSTANDER_SET, 0x82dee8, 0x82deea, 0)   /* STZ $12 : RTS */ \
  X(TS_BYSTANDER_STEP, 0x82de08, 0x82de1f, 0)  /* to the BNE */ \
  X(TS_BYSTANDER_SAY, 0x82de20, 0x82de2a, 0) \
  X(TS_BYSTANDER_MEETS, 0x82dda7, 0x82ddb1, 0)  /* to the BPL */ \
  X(TS_BYSTANDER_NEGATED, 0x82ddb1, 0x82ddb5, 0) \
  X(TS_BYSTANDER_GAP, 0x82ddb5, 0x82ddba, 0)    /* CMP : BCS */ \
  X(TS_BYSTANDER_KEPT, 0x82ddbb, 0x82ddcb, 0) \
  X(TS_BYSTANDER_LDX, 0x82ddcf, 0x82ddd2, 0) \
  X(TS_BYSTANDER_SAID, 0x82de2f, 0x82de31, 0) \
  X(TS_BYSTANDER_FLIP, 0x82de31, 0x82de4b, 0) \
  X(TS_BYSTANDER_LOOKED, 0x82de56, 0x82de5d, 0) \
  X(TS_BYSTANDER_LOOKED_B, 0x82de5d, 0x82de64, 0) \
  X(TS_BYSTANDER_DONE, 0x82de64, 0x82de6e, 0) \
  X(TS_BYSTANDER_BACK, 0x82dd64, 0x82dd68, 0) \
  X(TS_BYSTANDER_END, 0x82dd68, 0x82dd6a, 0) \
  X(TS_WAIT_FRAMES, 0x82def7, 0x82defa, 0)      /* DEC : BNE */ \
  X(TS_LEAP_SPLASH, 0x80dd79, 0x80dd8c, 0) \
  X(TS_SOAK_SPLASH, 0x80e2ff, 0x80e30a, 0) \
  X(TS_SET_HANDLER, 0x808475, 0x808480, 0)   /* thread_set_handler */ \
  X(TS_STAGGER_HANDLER, 0x819643, 0x81964d, 0) \
  X(TS_STAGGER_BEGIN, 0x81964d, 0x81966f, 0) \
  X(TS_STAGGER_PICTURE, 0x81966f, 0x81967c, 0) \
  X(TS_STAGGER_TURN, 0x819680, 0x81968d, 0)  /* to the BNE */ \
  X(TS_STAGGER_SHOWN, 0x81968d, 0x8196a3, 0) \
  X(TS_STAGGER_DAZED, 0x8196a7, 0x8196af, 0) \
  X(TS_STAGGER_DAZE, 0x8196b3, 0x8196b7, 0)  /* DEC $7E : BPL */ \
  X(TS_STAGGER_LAST, 0x8196b7, 0x8196ba, 0) \
  X(TS_STAGGER_OVER, 0x8196be, 0x8196e1, 0) \
  X(TS_STAGGER_BACK, 0x8196e1, 0x8196e4, 0)  /* JMP $9215 */ \
  X(TS_FREEZE_BEGIN, 0x8184d6, 0x8184ea, 0) \
  X(TS_FREEZE_COUNT, 0x8184f2, 0x8184f5, 0)  /* DEC $7E : BPL */ \
  X(TS_FREEZE_END, 0x8184f6, 0x818504, 0)    /* to the RTL */ \
  X(TS_CUT_HEARD, 0x8193d3, 0x8193da, 0)     /* LDA abs : CMP # : BNE */ \
  X(TS_CUT_AIM, 0x8193e2, 0x8193e8, 0)       /* to the JSR (abs,X) */ \
  X(TS_CUT_DRAWN, 0x819326, 0x81933f, 0)     /* and $81:9362-$937B */ \
  X(TS_CUT_WOKE, 0x819347, 0x81934c, 0) \
  X(TS_CUT_KEEP, 0x819355, 0x81935d, 0)      /* the RTS too */ \
  X(TS_CUT_PUT, 0x8193e9, 0x81940f, 0) \
  X(TS_CUT_MADE, 0x81939e, 0x8193d2, 0)      /* the RTS too */ \
  X(TS_CUT_MARKED, 0x819413, 0x819426, 0)    /* to the BNE */ \
  X(TS_CUT_SEVEN, 0x819427, 0x81942c, 0) \
  X(TS_CUT_SHOWN, 0x81942d, 0x81943a, 0) \
  X(TS_CUT_NEXT, 0x81943b, 0x81943e, 0)      /* LDA (dp),Y : BEQ */ \
  X(TS_CUT_PICTURE, 0x81943f, 0x819449, 0) \
  X(TS_CUT_LISTED, 0x819451, 0x81945b, 0) \
  X(TS_CUT_AGAIN, 0x81944e, 0x81944f, 0)     /* PLY : BRA */ \
  X(TS_CUT_TILED, 0x81930c, 0x819312, 0)     /* INC abs : PLD : JSR */ \
  X(TS_CUT_SECOND, 0x8194bb, 0x8194c6, 0) \
  X(TS_CUT_UNMARK, 0x8194dd, 0x8194e9, 0) \
  X(TS_CUT_FREED, 0x8194ee, 0x8194f3, 0)     /* the RTS too */ \
  X(TS_CUT_HANDLER, 0x819313, 0x819318, 0) \
  X(TS_CUT_DONE, 0x81931d, 0x819321, 0)      /* STZ dp : JMP */ \
  X(TS_DEATH_FLASHING, 0x80ce81, 0x80ce84, 0)  /* LDA $72 : BEQ */ \
  X(TS_DEATH_ENDS, 0x80ce85, 0x80ce8a, 0)    /* PEA : DEC : PHA : RTS */ \
  X(TS_DEATH_WHICH, 0x80ce94, 0x80ce9a, 0)   /* LDA $10 : CMP # : BNE */ \
  X(TS_DEATH_MONSTER, 0x80ce9b, 0x80cea5, 0) \
  X(TS_DEATH_LIST, 0x80ceb3, 0x80cebe, 0) \
  X(TS_DEATH_STILL, 0x80ceaa, 0x80ceb2, 0)   /* the BRA too */ \
  X(TS_DEATH_LIFE, 0x80cec3, 0x80cec9, 0)    /* LDX : DEC abs,X : BMI */ \
  X(TS_DEATH_LIFE_KEPT, 0x80cec3, 0x80cec9, 0)  /* ...with three NOPs */ \
  X(TS_DEATH_SHOW, 0x80ceca, 0x80ced0, 0) \
  X(TS_DEATH_AGAIN, 0x80d95f, 0x80d97e, 0)   /* the RTS too */ \
  X(TS_SPIDER_ENTER, 0x83b280, 0x83b28d, 0)  /* the JSR too */ \
  X(TS_SPIDER_MADE, 0x83b34a, 0x83b3a1, 0)   /* the JSL too */ \
  X(TS_SPIDER_DRAW, 0x83b457, 0x83b45b, 0)   /* JSL rng_next */ \
  X(TS_SPIDER_WAY, 0x83b45b, 0x83b46b, 0)    /* and $83:B465, the RTS too */ \
  X(TS_SPIDER_FIRST, 0x83b290, 0x83b295, 0) \
  X(TS_WALKER_ENTER, 0x81d70a, 0x81d717, 0)  /* the JSR too */ \
  X(TS_WALKER_MADE, 0x81d691, 0x81d6e1, 0) \
  X(TS_WALKER_SHOWN, 0x81d6e5, 0x81d6ef, 0)  /* the JSL too */ \
  X(TS_WALKER_DRAW, 0x81d5d9, 0x81d5dd, 0)   /* JSL rng_next */ \
  X(TS_WALKER_WAY, 0x81d5dd, 0x81d5ef, 0)    /* and $81:D5E9, the RTS too */ \
  X(TS_WALKER_FIRST, 0x81d71a, 0x81d71f, 0) \
  X(TS_CHAINSAW_ENTER, 0x81983a, 0x81984c, 0)  /* the JSR too */ \
  X(TS_CHAINSAW_MADE, 0x819733, 0x81977d, 0)   /* the RTS too */ \
  X(TS_CHAINSAW_WAYS, 0x81984c, 0x819854, 0)   /* the JSR too */ \
  X(TS_CHAINSAW_SOUND_TEST, 0x819811, 0x819819, 0) \
  X(TS_CHAINSAW_HANDLER, 0x81985d, 0x81986b, 0) \
  X(TS_CHAINSAW_FIRST, 0x81986f, 0x819874, 0) \
  X(TS_RNG_ONCE, 0x809d39, 0x809d5b, 0)      /* rng_next, a BVC taken */ \
  X(TS_RNG_TWICE, 0x809d39, 0x809d5b, 0)     /* ...or not */ \
  X(TS_SLIMED, 0x80dc1e, 0x80dc64, 0)        /* the JSR too */ \
  X(TS_SLIMED_REST, 0x80f354, 0x80f35c, 0) \
  X(TS_SLIMED_RESTED, 0x80f360, 0x80f366, 0)  /* the RTS too */ \
  X(TS_SLIMED_HANDS, 0x80dc64, 0x80dc70, 0)  /* JSR $F38D : JMP $D4F4 */ \
  X(TS_STUNG, 0x80f9d0, 0x80f9d7, 0)         /* LDX $0E : LDA $1CB8,X : BEQ */ \
  X(TS_BOLT_ENTER, 0x81f808, 0x81f815, 0) \
  X(TS_REST_DONE, 0x80f360, 0x80f365, 0)      /* LDX : STZ abs,X */ \
  X(TS_OUT_NOTED, 0x80ced8, 0x80cee2, 0) \
  X(TS_OUT_LEFT, 0x80cee6, 0x80ceeb, 0)       /* LDA abs : BNE */ \
  X(TS_OUT_TWO, 0x80ceeb, 0x80cef3, 0)        /* LDA abs : ORA abs : BEQ; and $80:CEF3 */ \
  X(TS_OUT_SENTINEL, 0x80cefb, 0x80cf02, 0)   /* LDA dp : CMP # : BNE */ \
  X(TS_OUT_UNLOAD, 0x80cf16, 0x80cf27, 0) \
  X(TS_OUT_DROPS, 0x80cf37, 0x80cf44, 0)      /* to the BEQ */ \
  X(TS_OUT_DROPPED, 0x80cf44, 0x80cf55, 0) \
  X(TS_PLA, 0x80cf59, 0x80cf5a, 0) \
  X(TS_SLIME_SCORED, 0x81ccf9, 0x81cd01, 0)   /* INC : LDX # : LDA dp */ \
  X(TS_UNHANDLED, 0x819887, 0x81988b, 0)      /* LDA #0 : TAY */ \
  X(TS_DECOY_ENDED, 0x81f1db, 0x81f1de, 0)    /* DEC abs */ \
  X(TS_WOLF_SECOND, 0x81ac32, 0x81ac39, 0)    /* LDA dp : CMP # : BEQ */ \
  X(TS_CHAINSAW_SCORED, 0x81988f, 0x819897, 0) \
  X(TS_CHAINSAW_SIDE, 0x81989b, 0x8198a7, 0)  /* the JSL too */ \
  X(TS_CHAINSAW_COUNTED, 0x8198a7, 0x8198bb, 0) \
  X(TS_ZOMBIE_FREED, 0x818ef6, 0x818f02, 0)   /* to the RTL */ \
  X(TS_WOLF_COUNTED, 0x81ac48, 0x81ac5c, 0) \
  X(TS_WOLF_LAST, 0x81ac60, 0x81ac63, 0)       /* LDA # */ \
  X(TS_VAMPIRE_COUNTED, 0x82ef66, 0x82ef75, 0)  /* the JSL too */ \
  X(TS_VAMPIRE_SIDE_COUNTED, 0x82ef75, 0x82ef78, 0) \
  X(TS_EXIT_DOOR_LOOK, 0x82f7a5, 0x82f7b3, 0) \
  X(TS_EXIT_DOOR_ALIVE, 0x82f7b9, 0x82f7c0, 0)     /* LDX : LDA abs,X : BNE */ \
  X(TS_EXIT_DOOR_NOBODY, 0x82f7c0, 0x82f7c2, 0)    /* SEC : RTS */ \
  X(TS_EXIT_DOOR_OUT, 0x82f7c2, 0x82f7c7, 0)       /* LDA abs,X : BEQ */ \
  X(TS_EXIT_DOOR_OTHER, 0x82f7c7, 0x82f7d2, 0) \
  X(TS_EXIT_DOOR_WAYS, 0x82f7d2, 0x82f7dd, 0) \
  X(TS_EXIT_DOOR_TRY, 0x82f7dd, 0x82f7f6, 0) \
  X(TS_EXIT_DOOR_ABOVE, 0x82f7fc, 0x82f805, 0)     /* and $82:F80B, $82:F81A */ \
  X(TS_EXIT_DOOR_XY, 0x82f829, 0x82f82d, 0)        /* LDX dp : LDY dp; $82:F840 */ \
  X(TS_EXIT_DOOR_ZERO_XY, 0x82f833, 0x82f83a, 0) \
  X(TS_EXIT_DOOR_BRA, 0x82f84a, 0x82f84c, 1)       /* and $82:F84C */ \
  X(TS_EXIT_DOOR_TILES, 0x82f84e, 0x82f85e, 0) \
  X(TS_EXIT_DOOR_TILE_TEST, 0x82f862, 0x82f86a, 0) /* and $82:F875, $82:F88B */ \
  X(TS_EXIT_DOOR_UP_3, 0x82f86a, 0x82f871, 0) \
  X(TS_EXIT_DOOR_UP_6, 0x82f87d, 0x82f887, 0) \
  X(TS_EXIT_DOOR_NEXT, 0x82f895, 0x82f8a0, 0)      /* to the BNE */ \
  X(TS_EXIT_DOOR_COUNT, 0x82f8a3, 0x82f8a9, 0)     /* STA : DEC : BMI */ \
  X(TS_EXIT_DOOR_FOUND, 0x82f8af, 0x82f8b1, 0)     /* CLC : RTS */ \
  X(TS_EXIT_DOOR_LOAD, 0x82f710, 0x82f71a, 0) \
  X(TS_EXIT_DOOR_MADE, 0x82f8dd, 0x82f914, 0) \
  X(TS_EXIT_DOOR_TOUCHABLE, 0x82f929, 0x82f934, 0) /* the RTS too */ \
  X(TS_EXIT_DOOR_TOUCHED, 0x82f724, 0x82f728, 0)   /* LDA dp : BMI */ \
  X(TS_EXIT_DOOR_PLAYERS, 0x82f728, 0x82f730, 0)   /* and $82:F730 */ \
  X(TS_EXIT_DOOR_UNTOUCHABLE, 0x82f738, 0x82f73d, 0) \
  X(TS_EXIT_DOOR_SIDE, 0x82f744, 0x82f753, 0)      /* the JSL too */ \
  X(TS_EXIT_DOOR_LEFT, 0x82f753, 0x82f758, 0)      /* LDA abs,X : BEQ */ \
  X(TS_EXIT_DOOR_UNHANDLED, 0x82f758, 0x82f75c, 0) /* LDA #0 : TAY */ \
  X(TS_FISH_FATE, 0x81e4c1, 0x81e4c5, 0)     /* LDA #0 : TAY */ \
  X(TS_FISH_SECOND, 0x81e4c9, 0x81e4d0, 0)   /* and $81:E5AC, the same */ \
  X(TS_FISH_COUNTED, 0x81e4df, 0x81e4ee, 0)  /* the JSL too; $81:E572 */ \
  X(TS_KILLER_SIDE, 0x809d6a, 0x809d72, 0) \
  X(TS_KILLER_OTHER, 0x809d72, 0x809d74, 0)  /* INX : INX */ \
  X(TS_RTL, 0x809d74, 0x809d75, 0) \
  X(TS_FISH_UNTOUCHABLE, 0x81e4ee, 0x81e4fc, 0)  /* and $81:E581 */ \
  X(TS_FISH_ENDED, 0x81e59a, 0x81e5a8, 0) \
  X(TS_BOLT_MADE, 0x81f8a1, 0x81f8fb, 0)     /* the JSL too */ \
  X(TS_PAUSE_DIM, 0x8089c2, 0x8089c8, 0) \
  X(TS_PAUSE_BRIGHT, 0x8089f0, 0x8089f6, 0) \
  X(TS_MAIN_PLAYERS, 0x808537, 0x80853f, 0)  /* LDA : ORA : BEQ */ \
  X(TS_MAIN_NEIGHBOURS, 0x80853f, 0x808544, 0)  /* LDA : BNE */ \
  X(TS_STUNG_HURT, 0x80f9d7, 0x80f9de, 0) \
  X(TS_STUNG_SHOWN, 0x80f9e2, 0x80f9ef, 0)   /* the JSR too */ \
  X(TS_ANT_FLASH, 0x81baec, 0x81baf7, 0) \
  X(TS_FOOTBALLER_OFF, 0x81c6ec, 0x81c6f1, 0)  /* LDA # : STA $0A */ \
  X(TS_ANT_FLASHED, 0x81bafb, 0x81bb04, 0) \
  X(TS_HANDS_SWAP, 0x80f38d, 0x80f3a5, 0)    /* the RTS too */ \
  X(TS_DOOR_ASK, 0x82defb, 0x82df00, 0)      /* LDA abs : BNE */ \
  X(TS_DOOR_BUZZ, 0x82df00, 0x82df06, 0) \
  X(TS_DOOR_DONE, 0x82df1f, 0x82df22, 0) \
  X(TS_SLIME_ENTER, 0x81cccf, 0x81ccd9, 0) \
  X(TS_SLIME_HANDLER, 0x81ccac, 0x81ccb6, 0)  /* the JSL too */ \
  X(TS_SLIME_WAY, 0x81c9e8, 0x81c9f9, 0)     /* the JMP too */ \
  X(TS_SLIME_FIRST, 0x81ccdf, 0x81cce4, 0) \
  X(TS_VAMPIRE_TAIL, 0x82ef57, 0x82ef5b, 0)    /* LDA $2E : BEQ */ \
  X(TS_VAMPIRE_SLEEP, 0x82ef49, 0x82ef4b, 0)   /* LDA $2C */ \
  X(TS_VAMPIRE_LOAD, 0x82ef3c, 0x82ef49, 0)    /* the JSR too */ \
  X(TS_VAMPIRE_MADE, 0x82ef8f, 0x82efe6, 0)    /* to the BNE */ \
  X(TS_VAMPIRE_START, 0x82efed, 0x82eff0, 0)   /* JMP $E827 */ \
  X(TS_VAMPIRE_RESET, 0x82e827, 0x82e850, 0)   /* the JSL too */ \
  X(TS_VAMPIRE_ALIVE, 0x82e850, 0x82e854, 0)   /* LDA $30 : BPL */ \
  X(TS_DECOY_ENTER, 0x81f159, 0x81f166, 0)    /* the JSR too */ \
  X(TS_BREAKABLE_ENTER, 0x82f25d, 0x82f26a, 0) /* the JSR too */ \
  X(TS_LEVEL_ENDING_SPAWN, 0x808551, 0x808561, 0) /* and $856A-$857A */ \
  X(TS_MONSTER_PICTURES, 0x80d9b3, 0x80d9b8, 0) \
  X(TS_LEAVE_ENDS_ASK, 0x80faf6, 0x80fafa, 0) \
  X(TS_LEAVE_ENDS, 0x80fafa, 0x80faff, 0) \
  X(TS_LEFT_LOAD, 0x80fb00, 0x80fb0a, 0) \
  X(TS_LEFT_PANEL, 0x80fb18, 0x80fb1a, 0) \
  X(TS_LEFT_SHOWN, 0x80fb1e, 0x80fb31, 0) \
  X(TS_GONE_RECORDS, 0x80fb35, 0x80fb41, 0) \
  X(TS_GONE_CLEARED, 0x80fb41, 0x80fb49, 0) \
  X(TS_MONSTER_ENDS_ASK, 0x80da54, 0x80da58, 0) \
  X(TS_MONSTER_ENDED, 0x80da67, 0x80da6f, 0) \
  X(TS_PUNCH_TOLD_HEAD, 0x80f051, 0x80f055, 0) \
  X(TS_PUNCH_BOX, 0x80f055, 0x80f086, 0) \
  X(TS_PUNCH_AGAIN, 0x80ef8f, 0x80ef95, 0) \
  X(TS_PUNCH_SECOND, 0x80ef98, 0x80ef9d, 0) \
  X(TS_PUNCH_THIRD, 0x80efaa, 0x80efac, 0) \
  X(TS_PUNCH_OVER, 0x80efb9, 0x80efbb, 0) \
  X(TS_MONSTER_BEGUN, 0x80d9bc, 0x80d9e3, 0) \
  X(TS_FOURTH_BACK, 0x80ea02, 0x80ea16, 0)      /* the JSL too */ \
  X(TS_FOURTH_DRAW, 0x82dc57, 0x82dc5f, 0)      /* JSL rng_next */ \
  X(TS_FOURTH_PRIZE, 0x82dc5f, 0x82dc6d, 0)     /* to the first BEQ */ \
  X(TS_FOURTH_NOT_NONE, 0x82dc6d, 0x82dc72, 0) \
  X(TS_FOURTH_FACING, 0x82dc78, 0x82dc80, 0) \
  X(TS_FOURTH_FACING_B, 0x82dc82, 0x82dc87, 0) \
  X(TS_FOURTH_BESIDE, 0x82dc87, 0x82dc9b, 0) \
  X(TS_BUBBLED, 0x818404, 0x818433, 0) \
  X(TS_BUBBLE_OVER, 0x818446, 0x81845a, 0) \
  X(TS_BREAKABLE_MADE, 0x82f2f0, 0x82f330, 0)  /* the RTS too */ \
  X(TS_BREAKABLE_HANDLER, 0x82f26a, 0x82f275, 0) \
  X(TS_FRANKENSTEIN_ENTER, 0x81a455, 0x81a467, 0)   /* the JSR too */ \
  X(TS_FRANKENSTEIN_BEGUN, 0x81a414, 0x81a454, 0)   /* the JSR too */ \
  X(TS_FRANKENSTEIN_CLEARS, 0x81a191, 0x81a19f, 0)  /* the RTS too */ \
  X(TS_FRANKENSTEIN_HANDLER, 0x81a467, 0x81a46d, 0) \
  X(TS_DECOY_MADE, 0x81f204, 0x81f24e, 0) \
  X(TS_VAMPIRE_SET, 0x82eb5e, 0x82eb64, 0)     /* and $82:EB74-$EB79 */ \
  X(TS_VAMPIRE_SET_HOLD, 0x82ec92, 0x82eca3, 0) \
  X(TS_VAMPIRE_LAND_ASK, 0x82ea35, 0x82ea3d, 0)  /* LDA abs : CMP # : BNE */ \
  X(TS_VAMPIRE_LAND_SOUND, 0x82ea3d, 0x82ea40, 0) \
  X(TS_VAMPIRE_LAND_PICTURES, 0x82ea44, 0x82ea47, 0) \
  X(TS_VAMPIRE_LANDED, 0x82ea4b, 0x82ea5a, 0)  /* the JSL too */ \
  X(TS_VAMPIRE_LAND_WAY, 0x82ea5e, 0x82ea66, 0)  /* the JMP too */ \
  X(TS_VAMPIRE_GO_FIRE, 0x82edfd, 0x82ee0e, 0)   /* the RTS too */ \
  X(TS_VAMPIRE_FIRED, 0x82ee1b, 0x82ee29, 0) \
  X(TS_VAMPIRE_SPAWN, 0x82ee29, 0x82ee3b, 0) \
  X(TS_VAMPIRE_SPAWNED, 0x82ee3f, 0x82ee43, 0)   /* DEC dp : BEQ */ \
  X(TS_VAMPIRE_PAUSE, 0x82ee43, 0x82ee46, 0) \
  X(TS_VAMPIRE_AGAIN, 0x82ee4a, 0x82ee50, 0)     /* DEC dp, DEC dp : BNE */ \
  X(TS_VAMPIRE_WRAP, 0x82ee50, 0x82ee57, 1)      /* the BRA too */ \
  X(TS_VAMPIRE_LAST, 0x82ee57, 0x82ee5e, 0) \
  X(TS_VAMPIRE_NEXT, 0x82ee62, 0x82ee67, 0)      /* LDX dp : JMP (abs,X) */ \
  X(TS_VAMPIRE_HIDDEN, 0x82eba3, 0x82ebcf, 0)    /* the RTS too */ \
  X(TS_VAMPIRE_BACK, 0x82ec61, 0x82ec73, 0)      /* the JSL too */ \
  X(TS_VAMPIRE_BACK_ON, 0x82ec73, 0x82ec76, 0)   /* JMP $EB5E */ \
  X(TS_TRACKER_ENTER, 0x82f03e, 0x82f048, 0) \
  X(TS_TRACKER_BEGUN, 0x82f04b, 0x82f050, 0) \
  X(TS_TRACKER_ENDED, 0x82f05e, 0x82f069, 0) \
  X(TS_PUFF_ENTER, 0x82f1e6, 0x82f1f0, 0) \
  X(TS_PUFF_MADE, 0x82f1f4, 0x82f22b, 0) \
  X(TS_PIECE_ENTER, 0x83b1dd, 0x83b1e7, 0) \
  X(TS_PIECE_MADE, 0x83b1eb, 0x83b222, 0)       /* the sound's LDA too */ \
  X(TS_PIECE_HEARD, 0x83b226, 0x83b229, 0) \
  X(TS_PULSE_ENTER, 0x80a0ef, 0x80a102, 0) \
  X(TS_FIGURE_ENTER, 0x829569, 0x829576, 0)     /* the JSR too */ \
  X(TS_FIGURE_COLOURS, 0x8294e4, 0x8294ea, 0) \
  X(TS_FIGURE_COLOURED, 0x8294ee, 0x829506, 0) \
  X(TS_FIGURE_WAIT_ASK, 0x829511, 0x829517, 0)  /* $82:80E0 too */ \
  X(TS_FIGURE_PLACED, 0x829517, 0x829534, 0)    /* the JSR too */ \
  X(TS_FIGURE_PART, 0x8294b8, 0x8294e4, 0)      /* the RTS too */ \
  X(TS_FIGURE_PART_KEPT, 0x829534, 0x829539, 0) /* and two like it */ \
  X(TS_FIGURE_PARTS_KEPT, 0x829543, 0x829554, 0) \
  X(TS_FIGURE_NO_SPITS, 0x8294a4, 0x8294b4, 0) \
  X(TS_FIGURE_HANDLER, 0x829554, 0x82955e, 0)   /* the JSL too */ \
  X(TS_FIGURE_CALM, 0x82955e, 0x829569, 0)      /* the RTS too */ \
  X(TS_SAUCER_ENTER, 0x82873c, 0x82874c, 0) \
  X(TS_SAUCER_COLOURED, 0x828750, 0x828766, 0) \
  X(TS_SAUCER_DRAWN, 0x82876a, 0x828782, 0) \
  X(TS_SAUCER_QUEUED, 0x828786, 0x8287aa, 0)    /* the JSR too */ \
  X(TS_SAUCER_NEAREST_ASK, 0x8284ce, 0x8284d9, 0) \
  X(TS_SAUCER_AIMED, 0x8284dd, 0x828505, 0)     /* the RTS too */

enum {
#define X(name, from, to, taken) name,
  TS_BLOCKS(X)
#undef X
  TS_TAKEN,  // a branch taken
  TS_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[TS_BLOCK_COUNT];
} ThreadStretchWork;

// What a stretch needs of where it starts.
typedef enum {
  TS_NEEDS_NOTHING,
  TS_NEEDS_RECORD,  // the thread's record on its page, in low WRAM
  TS_NEEDS_A,       // a record in A
  TS_NEEDS_LEVEL,   // the level's offset is one into the cartridge
  TS_NEEDS_COUNTS,  // the two counts of those saved are small
  TS_NEEDS_PLACE,   // the arriving martian's `JSR $9AA2` is what it returns to
  TS_NEEDS_WOLF,    // a werewolf's record, and its loop to return to
  TS_NEEDS_SQUARE,  // the band's handler, and the players' records in low WRAM
  TS_NEEDS_FLYOFF_RECORD,  // a record in Y, and a kind that is one
  TS_NEEDS_FLYOFF,  // the record on the page, and a diagonal
  TS_NEEDS_BYSTANDER_RECORD,  // a record in Y, and a kind that is one
  TS_NEEDS_BYSTANDER_SET,     // its setup's `JSR` to return to
  TS_NEEDS_BYSTANDER_KIND,    // a kind that is one
  TS_NEEDS_CUT,         // a side of one to eight, and the maniac's record
  TS_NEEDS_CUT_PLACED,  // ...and the aim's `JSR` to return to
  TS_NEEDS_CUT_MADE,    // ...and the chips' record in A, a list, and the
                        //    `JSR` that asked for them
  TS_NEEDS_CUT_LIST,    // the chips' record and a list
  TS_NEEDS_CUT_TILED,   // the maniac's page on the stack, with a side
  TS_NEEDS_CUT_FREED,   // the second list's `JSR` to return to
  TS_NEEDS_DEATH_ENDS,  // an end due that is one of the six, or none
  TS_NEEDS_DEATH,       // which player, which of the two, and the record
  TS_NEEDS_SLIME_BEGUN,  // `slime_begin`'s `JSR` to return to
  TS_NEEDS_SLIME_WAY,    // the start's `JSR $C9E4` to return to
  TS_NEEDS_VAMPIRE,       // the vampire's loop to return to, and its record
  TS_NEEDS_VAMPIRE_HIDDEN,  // ...and whoever it watched, in low WRAM
  TS_NEEDS_VAMPIRE_NEXT,    // ...and what it does after firing, one of three
  TS_NEEDS_FIGURE_PART,    // a record in A, and a part's `JSR` to return to
  TS_NEEDS_SAUCER_AIMED,   // a record in X, and the start's `JSR` to return to
  TS_NEEDS_PLAYER_LOAD,    // the level's load holds a player's
  TS_NEEDS_SLIMED,         // the player's two records in low WRAM, and which
                           // player
  TS_NEEDS_SLIMED_RESTED,  // ...and the slime's `JSR $F354` to return to
  TS_NEEDS_SPIDER_MADE,    // a record in A, and the start's `JSR` to return to
  TS_NEEDS_WALKER_MADE,    // likewise
  TS_NEEDS_WALKER_SHOWN,   // the start's `JSR` to return to
  TS_NEEDS_CHAINSAW_MADE,  // a record on the page, and the start's `JSR`
  TS_NEEDS_STUNG,          // which player, and their record
  TS_NEEDS_SQUARE_MADE,    // a record in A, and the `JSR` to return to
  TS_NEEDS_SQUARE_CELL,    // the direct page at zero, the page under it and
                           // the `JSR` to return to, and a cell in the
                           // cartridge
  TS_NEEDS_SQUARE_TILED,   // ...the page and the `JSR`
  TS_NEEDS_SQUARE_FREED,   // the pass's return, and one of the five
  TS_NEEDS_FISHMAN_ENDED,  // its record, and a load to give its weight back
                           // from
  TS_NEEDS_EXIT_DOOR_FIND,      // the search's `JSR` to return to, a player's
                           // record and which player
  TS_NEEDS_EXIT_DOOR_MADE,      // a record in A and Y, and the `JSR` to return to
  TS_NEEDS_EXIT_DOOR_HANDLED,   // the door's record, and the `JSR` to return to
  TS_NEEDS_EXIT_DOOR,           // the door's record
  TS_NEEDS_ZOMBIE_FREED,   // a load to give its weight back from
  TS_NEEDS_WEREWOLF_COUNTED,  // its record in low WRAM, by `$08`
  TS_NEEDS_PLAYER,          // which player, a small index, on the page
  TS_NEEDS_REST_ENDED,      // ...and a caller other than the slime
  TS_NEEDS_PLAYER_OUT,      // ...and a load to give its weight back from,
                            //    if it is to
  TS_NEEDS_PLAYER_DROPS,    // its inventory in low WRAM
  TS_NEEDS_BOLT_MADE,      // a record in A and Y, the start's `JSR` to return
  TS_NEEDS_VAMPIRE_MADE,    // a record in A, the start's `JSR` to return
  TS_NEEDS_VAMPIRE_SOUNDED, // ...that `JSR`'s return
  TS_NEEDS_BREAKABLE_MADE, // a record in A, the start's `JSR` to return to
  // A record on its page, the start's `JSR` likewise.
  TS_NEEDS_FRANKENSTEIN_BEGUN,
  TS_NEEDS_DECOY_MADE,     // a record in A and Y, the start's `JSR` to return
                           // to, and a way and a player the tables have
  TS_NEEDS_STUNG_SHOWN,    // ...and a way the table has, read through `$80`
  TS_NEEDS_CHAINSAW_SHOWN, // ...and the start's other `JSR`
} ThreadStretchNeeds;

bool thread_stretch_needs_met(const Wram* w, ThreadStretchNeeds needs,
                              const PortCpu* c);

// name, symbol, entry, the port, its exits and what it needs.
#define THREAD_STRETCHES(X) \
  X(argument_weed_a, "$81:D183", 0x81d183u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_weed_b, "$81:D1AD", 0x81d1adu, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_weed_c, "$81:D1D7", 0x81d1d7u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_follower, "$82:F4B1", 0x82f4b1u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_level_wait, "$80:8516", 0x808516u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_martian_shot, "$81:99C2", 0x8199c2u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_player_hurt, "$80:D069", 0x80d069u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81ec72, "$81:EC72", 0x81ec72u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81f30e, "$81:F30E", 0x81f30eu, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_flyoff, "$82:E0B4", 0x82e0b4u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(setter_819215, "$81:9215", 0x819215u, thread_setter, SETTER, TS_NEEDS_NOTHING) \
  X(setter_819250, "$81:9250", 0x819250u, thread_setter, SETTER, TS_NEEDS_NOTHING) \
  X(page_clear, "$80:D0DD", PAGE_CLEAR_PC, page_clear, SMALL, TS_NEEDS_NOTHING) \
  X(page_pull, "$80:ABD1", PAGE_PULL_PC, page_pull, SMALL, TS_NEEDS_NOTHING) \
  X(carry_set, "$80:E78E", CARRY_SET_PC, carry_set, SMALL, TS_NEEDS_NOTHING) \
  X(victim_gone, "$81:81EF", VICTIM_GONE_PC, victim_gone, SMALL, TS_NEEDS_NOTHING) \
  X(game_lost, "$80:85B5", GAME_LOST_PC, game_lost, SMALL, TS_NEEDS_NOTHING) \
  X(main_back, "$80:84D5", MAIN_BACK_PC, main_back, SMALL, TS_NEEDS_NOTHING) \
  X(argument_808a00, "$80:8A00", 0x808a00u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(level_ending, "$80:8544", LEVEL_ENDING_PC, level_ending, LEVEL_ENDING, TS_NEEDS_NOTHING) \
  X(level_ending_second, "$80:8565", LEVEL_ENDING_SECOND_PC, level_ending_second, LEVEL_ENDING, TS_NEEDS_NOTHING) \
  X(argument_80857e, "$80:857E", LEVEL_ENDING_SLEEP_PC, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(stuck_freed, "$80:DC73", STUCK_FREED_PC, stuck_freed, SMALL, TS_NEEDS_NOTHING) \
  X(panel_side, "$80:C1CF", PANEL_SIDE_PC, panel_side, SMALL, TS_NEEDS_NOTHING) \
  X(pair_main_layers, "$80:85D3", 0x8085d3u, thread_pair, PAIR, TS_NEEDS_NOTHING) \
  X(pair_martian_handler, "$81:9B30", 0x819b30u, thread_pair, PAIR, TS_NEEDS_NOTHING) \
  X(pair_hud_thread, "$80:C2DE", 0x80c2deu, thread_pair, PAIR, TS_NEEDS_NOTHING) \
  X(weed_snap_aim, "$81:D154", 0x81d154u, weed_snap_aim, WEED, TS_NEEDS_NOTHING) \
  X(weed_snap_show, "$81:D15F", WEED_SNAP_SHOWN_PC, weed_snap_show, WEED, TS_NEEDS_NOTHING) \
  X(weed_throw_a, "$81:D168", 0x81d168u, weed_throw, WEED, TS_NEEDS_RECORD) \
  X(weed_throw_b, "$81:D18A", 0x81d18au, weed_throw, WEED, TS_NEEDS_RECORD) \
  X(weed_throw_c, "$81:D1B4", 0x81d1b4u, weed_throw, WEED, TS_NEEDS_RECORD) \
  X(weed_snap_rest, "$81:D1DE", WEED_SNAP_REST_PC, weed_snap_rest, WEED, TS_NEEDS_RECORD) \
  X(weed_pass_end, "$81:D2AD", WEED_PASS_END_PC, weed_pass_end, WEED_PASS, TS_NEEDS_NOTHING) \
  X(weed_fate, "$81:D2B1", 0x81d2b1u, weed_fate, WEED, TS_NEEDS_NOTHING) \
  X(weed_died, "$81:D2C8", WEED_DIED_PC, weed_died, WEED, TS_NEEDS_NOTHING) \
  X(weed_enter_a, "$81:D2F1", 0x81d2f1u, weed_enter, WEED, TS_NEEDS_NOTHING) \
  X(weed_enter_b, "$81:D2F9", 0x81d2f9u, weed_enter, WEED, TS_NEEDS_NOTHING) \
  X(weed_made, "$81:D214", WEED_MADE_PC, weed_made, WEED, TS_NEEDS_A) \
  X(weed_begun, "$81:D299", WEED_BEGUN_PC, weed_begun, WEED_PASS, TS_NEEDS_NOTHING) \
  X(follower_look, "$82:F44A", FOLLOWER_LOOK_PC, follower_look, FOLLOWER, TS_NEEDS_NOTHING) \
  X(follower_on_level, "$82:F452", FOLLOWER_ON_LEVEL_PC, follower_on_level, FOLLOWER, TS_NEEDS_NOTHING) \
  X(follower_tile, "$82:F45C", FOLLOWER_TILE_PC, follower_tile, FOLLOWER, TS_NEEDS_NOTHING) \
  X(follower_mark, "$82:F468", FOLLOWER_MARK_PC, follower_mark, FOLLOWER, TS_NEEDS_NOTHING) \
  X(follower_room, "$82:F4A2", FOLLOWER_ROOM_PC, follower_room, FOLLOWER, TS_NEEDS_NOTHING) \
  X(follower_made, "$82:F358", FOLLOWER_MADE_PC, follower_made, FOLLOWER, TS_NEEDS_A) \
  X(main_reset, "$80:85E1", MAIN_RESET_PC, main_reset, MAIN, TS_NEEDS_NOTHING) \
  X(main_scores_set, "$80:8600", MAIN_SCORES_SET_PC, main_scores_set, MAIN, TS_NEEDS_NOTHING) \
  X(main_sound_set, "$80:8615", MAIN_SOUND_SET_PC, main_sound_set, MAIN, TS_NEEDS_NOTHING) \
  X(level_record_ask, "$80:8849", LEVEL_RECORD_ASK_PC, level_record_ask, MAIN, TS_NEEDS_LEVEL) \
  X(level_faded, "$80:8525", LEVEL_FADED_PC, level_faded, MAIN, TS_NEEDS_NOTHING) \
  X(level_count, "$80:867B", LEVEL_COUNT_PC, level_count, MAIN, TS_NEEDS_COUNTS) \
  X(level_count_asked, "$80:8697", LEVEL_COUNT_ASKED_PC, level_count_asked, MAIN, TS_NEEDS_NOTHING) \
  X(cache_start, "$80:C04C", CACHE_START_PC, cache_start, MAIN, TS_NEEDS_NOTHING) \
  X(follower_hit, "$82:F3F4", FOLLOWER_HIT_PC, follower_hit, FOLLOWER, TS_NEEDS_RECORD) \
  X(frontend_enter, "$80:9126", FRONTEND_ENTER_PC, frontend_enter, MAIN, TS_NEEDS_NOTHING) \
  X(player_keep, "$80:D16E", PLAYER_KEEP_PC, player_keep, MAIN, TS_NEEDS_NOTHING) \
  X(spawner_tick, "$80:C8F9", SPAWNER_TICK_PC, spawner_tick, MAIN, TS_NEEDS_NOTHING) \
  X(spawner_wait, "$80:C908", SPAWNER_WAIT_PC, spawner_wait, SPAWNER, TS_NEEDS_NOTHING) \
  X(martian_enter, "$81:99DF", MARTIAN_ENTER_PC, martian_enter, MARTIAN, TS_NEEDS_NOTHING) \
  X(martian_begun, "$81:99EC", MARTIAN_BEGUN_PC, martian_begun, MARTIAN, TS_NEEDS_NOTHING) \
  X(martian_arrival_enter, "$81:9A3E", MARTIAN_ARRIVAL_ENTER_PC, martian_arrival_enter, MARTIAN, TS_NEEDS_NOTHING) \
  X(martian_arrival_grounded, "$81:9AB2", MARTIAN_PLACE_GROUNDED_PC, martian_arrival_grounded, MARTIAN, TS_NEEDS_PLACE) \
  X(martian_arrival_placed, "$81:9ABC", MARTIAN_PLACED_PC, martian_arrival_placed, MARTIAN, TS_NEEDS_PLACE) \
  X(martian_arrival_begun, "$81:9A50", MARTIAN_ARRIVAL_BEGUN_PC, martian_arrival_begun, MARTIAN, TS_NEEDS_NOTHING) \
  X(flashing_end, "$80:D97F", FLASHING_END_PC, flashing_end, PLAYER_HIT, TS_NEEDS_RECORD) \
  X(werewolf_enter, "$81:ABF5", WEREWOLF_ENTER_PC, werewolf_enter, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_howl_check, "$81:AC05", WEREWOLF_HOWL_CHECK_PC, werewolf_howl_check, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_howled, "$81:AC14", WEREWOLF_HOWLED_PC, werewolf_howled, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_made, "$81:A647", WEREWOLF_MADE_PC, werewolf_made, WOLF, TS_NEEDS_RECORD) \
  X(werewolf_woke, "$81:A68A", WEREWOLF_WOKE_PC, werewolf_woke, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_runs, "$81:ABC0", WEREWOLF_RUNS_PC, werewolf_runs, WOLF, TS_NEEDS_WOLF) \
  X(werewolf_struck, "$81:AB02", WEREWOLF_STRUCK_PC, werewolf_struck, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_rested, "$81:AA9B", WEREWOLF_RESTED_PC, werewolf_rested, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_crouched, "$81:A9AF", WEREWOLF_CROUCHED_PC, werewolf_crouched, WOLF, TS_NEEDS_WOLF) \
  X(player_hit_show, "$80:D056", PLAYER_HIT_SHOW_PC, player_hit_show, PLAYER_HIT, TS_NEEDS_RECORD) \
  X(player_hit_shown, "$80:D070", PLAYER_HIT_SHOWN_PC, player_hit_shown, PLAYER_HIT, TS_NEEDS_RECORD) \
  X(square_enter, "$82:F6EB", SQUARE_ENTER_PC, square_enter, SQUARE, TS_NEEDS_NOTHING) \
  X(square_pass, "$82:F6F8", SQUARE_PASS_PC, square_pass, SQUARE, TS_NEEDS_SQUARE) \
  X(flyoff_dress, "$82:E0BF", FLYOFF_DRESS_PC, flyoff_dress, FLYOFF, TS_NEEDS_FLYOFF_RECORD) \
  X(flyoff_way, "$82:E102", FLYOFF_WAY_PC, flyoff_way, FLYOFF, TS_NEEDS_NOTHING) \
  X(flyoff_pass, "$82:E115", FLYOFF_PASS_PC, flyoff_pass, FLYOFF, TS_NEEDS_FLYOFF) \
  X(bystander_enter, "$82:DD52", BYSTANDER_ENTER_PC, bystander_enter, BYSTANDER, TS_NEEDS_NOTHING) \
  X(bystander_dress, "$82:DE87", BYSTANDER_DRESS_PC, bystander_dress, BYSTANDER, TS_NEEDS_BYSTANDER_RECORD) \
  X(bystander_set, "$82:DEE8", BYSTANDER_SET_PC, bystander_set, BYSTANDER, TS_NEEDS_BYSTANDER_SET) \
  X(bystander_step, "$82:DE08", BYSTANDER_STEP_PC, bystander_step, BYSTANDER, TS_NEEDS_BYSTANDER_KIND) \
  X(leap_splash, "$80:DD79", LEAP_SPLASH_PC, leap_splash, LEAP, TS_NEEDS_RECORD) \
  X(soak_splash, "$80:E2FF", SOAK_SPLASH_PC, soak_splash, LEAP, TS_NEEDS_RECORD) \
  X(stagger_begin, "$81:9643", STAGGER_BEGIN_PC, stagger_begin, STAGGER, TS_NEEDS_RECORD) \
  X(stagger_spin, "$81:9680", STAGGER_SPIN_PC, stagger_spin, STAGGER, TS_NEEDS_RECORD) \
  X(stagger_dazed, "$81:96A7", STAGGER_DAZED_PC, stagger_dazed, STAGGER, TS_NEEDS_NOTHING) \
  X(stagger_daze, "$81:96B3", STAGGER_DAZE_PC, stagger_daze, STAGGER, TS_NEEDS_NOTHING) \
  X(stagger_over, "$81:96BE", STAGGER_OVER_PC, stagger_over, STAGGER, TS_NEEDS_RECORD) \
  X(freeze_begin, "$81:84D6", FREEZE_BEGIN_PC, freeze_begin, FREEZE, TS_NEEDS_RECORD) \
  X(freeze_pass, "$81:84F2", FREEZE_PASS_PC, freeze_pass, FREEZE, TS_NEEDS_RECORD) \
  X(cut_enter, "$81:92D6", CUT_PC, cut_enter, CUT, TS_NEEDS_NOTHING) \
  X(cut_heard, "$81:93D3", CUT_HEARD_PC, cut_heard, CUT, TS_NEEDS_CUT) \
  X(cut_aim, "$81:93E2", CUT_AIM_PC, cut_aim, CUT, TS_NEEDS_CUT) \
  X(cut_drawn_across, "$81:9326", CUT_ACROSS_DRAWN_PC, cut_drawn, CUT, TS_NEEDS_CUT) \
  X(cut_drawn_down, "$81:9362", CUT_DOWN_DRAWN_PC, cut_drawn, CUT, TS_NEEDS_CUT) \
  X(cut_woke_across, "$81:9347", CUT_ACROSS_WOKE_PC, cut_woke, CUT, TS_NEEDS_CUT) \
  X(cut_woke_down, "$81:9383", CUT_DOWN_WOKE_PC, cut_woke, CUT, TS_NEEDS_CUT) \
  X(cut_placed_across, "$81:9351", CUT_ACROSS_PLACED_PC, cut_placed, CUT, TS_NEEDS_CUT_PLACED) \
  X(cut_placed_down, "$81:938D", CUT_DOWN_PLACED_PC, cut_placed, CUT, TS_NEEDS_CUT_PLACED) \
  X(cut_made, "$81:939E", CUT_MADE_PC, cut_made, CUT, TS_NEEDS_CUT_MADE) \
  X(cut_shown, "$81:944E", CUT_SHOWN_PC, cut_list_again, CUT, TS_NEEDS_CUT_LIST) \
  X(cut_tiled, "$81:930C", CUT_TILED_PC, cut_tiled, CUT, TS_NEEDS_CUT_TILED) \
  X(cut_shown_again, "$81:94DA", CUT_SHOWN_AGAIN_PC, cut_list_again, CUT, TS_NEEDS_CUT_LIST) \
  X(cut_freed, "$81:94EE", CUT_FREED_PC, cut_freed, CUT, TS_NEEDS_CUT_FREED) \
  X(cut_done, "$81:931D", CUT_DONE_PC, cut_done, CUT, TS_NEEDS_NOTHING) \
  X(death_sounded, "$80:CE81", DEATH_SOUNDED_PC, death_sounded, DEATH, TS_NEEDS_DEATH_ENDS) \
  X(death_shown, "$80:CE94", DEATH_SHOWN_PC, death_shown, DEATH, TS_NEEDS_DEATH) \
  X(death_still, "$80:CEAA", DEATH_STILL_PC, death_still, DEATH, TS_NEEDS_DEATH) \
  X(death_counted, "$80:CEC3", DEATH_COUNTED_PC, death_counted, DEATH, TS_NEEDS_DEATH) \
  X(argument_swipe, "$81:E8C1", 0x81e8c1u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_door_a, "$82:DF0A", 0x82df0au, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_door_b, "$82:DF11", 0x82df11u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_door_c, "$82:DF18", 0x82df18u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(slimed, "$80:DC1E", SLIMED_PC, slimed, SLIMED, TS_NEEDS_SLIMED) \
  X(slimed_rested, "$80:F360", SLIMED_RESTED_PC, slimed_rested, SLIMED, TS_NEEDS_SLIMED_RESTED) \
  X(square_made, "$82:F643", SQUARE_MADE_PC, square_made, SQUARE_SHOWN, TS_NEEDS_SQUARE_MADE) \
  X(square_shown, "$82:F60D", SQUARE_SHOWN_PC, square_shown, SQUARE_SHOWN, TS_NEEDS_RECORD) \
  X(square_cell, "$82:F69B", SQUARE_CELL_PC, square_cell, SQUARE_SHOWN, TS_NEEDS_SQUARE_CELL) \
  X(square_tiled, "$82:F6A9", SQUARE_TILED_PC, square_tiled, SQUARE_SHOWN, TS_NEEDS_SQUARE_TILED) \
  X(square_told, "$82:F613", SQUARE_TOLD_PC, square_told, SQUARE_SHOWN, TS_NEEDS_NOTHING) \
  X(square_hidden, "$82:F61A", SQUARE_HIDDEN_PC, square_hidden, SQUARE_SHOWN, TS_NEEDS_NOTHING) \
  X(square_freed, "$82:F620", SQUARE_FREED_PC, square_freed, SQUARE_SHOWN, TS_NEEDS_SQUARE_FREED) \
  X(exit_door_enter, "$82:F70B", EXIT_DOOR_ENTER_PC, exit_door_enter, EXIT_DOOR, TS_NEEDS_NOTHING) \
  X(exit_door_look, "$82:F7A5", EXIT_DOOR_LOOK_PC, exit_door_look, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_looked, "$82:F7B7", EXIT_DOOR_LOOKED_PC, exit_door_looked, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_tried, "$82:F7FA", EXIT_DOOR_TRIED_PC, exit_door_tried, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_above, "$82:F809", EXIT_DOOR_ABOVE_PC, exit_door_above, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_higher, "$82:F818", EXIT_DOOR_HIGHER_PC, exit_door_higher, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_below, "$82:F827", EXIT_DOOR_BELOW_PC, exit_door_below, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_edge, "$82:F831", EXIT_DOOR_EDGE_PC, exit_door_edge, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_someone, "$82:F83E", EXIT_DOOR_SOMEONE_PC, exit_door_someone, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_tether, "$82:F848", EXIT_DOOR_TETHER_PC, exit_door_tether, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_tile, "$82:F862", EXIT_DOOR_TILE_PC, exit_door_tile, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_tile_up, "$82:F875", EXIT_DOOR_TILE_UP_PC, exit_door_tile_up, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_tile_top, "$82:F88B", EXIT_DOOR_TILE_TOP_PC, exit_door_tile_top, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_FIND) \
  X(exit_door_made, "$82:F8DD", EXIT_DOOR_MADE_PC, exit_door_made, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_MADE) \
  X(exit_door_handled, "$82:F929", EXIT_DOOR_HANDLED_PC, exit_door_handled, EXIT_DOOR, TS_NEEDS_EXIT_DOOR_HANDLED) \
  X(exit_door_wait, "$82:F724", EXIT_DOOR_WAIT_PC, exit_door_wait, EXIT_DOOR, TS_NEEDS_EXIT_DOOR) \
  X(exit_door_paused, "$82:F744", EXIT_DOOR_PAUSED_PC, exit_door_paused, EXIT_DOOR, TS_NEEDS_NOTHING) \
  X(rest_begun, "$80:F354", REST_BEGUN_PC, rest_begun, PLAYER_OUT, TS_NEEDS_PLAYER) \
  X(rest_ended, "$80:F360", REST_ENDED_PC, rest_ended, PLAYER_OUT, TS_NEEDS_REST_ENDED) \
  X(player_out_noted, "$80:CED8", PLAYER_OUT_NOTED_PC, player_out_noted, PLAYER_OUT, TS_NEEDS_PLAYER) \
  X(player_out, "$80:CEE6", PLAYER_OUT_PC, player_out, PLAYER_OUT, TS_NEEDS_PLAYER_OUT) \
  X(player_out_slept, "$80:CF09", PLAYER_OUT_SLEPT_PC, player_out_slept, PLAYER_OUT, TS_NEEDS_NOTHING) \
  X(player_out_filed, "$80:CF2B", PLAYER_OUT_FILED_PC, player_out_filed, PLAYER_OUT, TS_NEEDS_NOTHING) \
  X(player_out_freed, "$80:CF31", PLAYER_OUT_FREED_PC, player_out_freed, PLAYER_OUT, TS_NEEDS_NOTHING) \
  X(player_out_freed_2, "$80:CF37", PLAYER_OUT_FREED_2_PC, player_out_freed_2, PLAYER_OUT, TS_NEEDS_PLAYER_DROPS) \
  X(player_out_dropped, "$80:CF59", PLAYER_OUT_DROPPED_PC, player_out_dropped, PLAYER_OUT, TS_NEEDS_NOTHING) \
  X(slime_fate, "$81:CCF7", SLIME_DEATH_PC, slime_fate, ENDS, TS_NEEDS_NOTHING) \
  X(decoy_ended, "$81:F1DB", DECOY_DEATH_PC, decoy_ended, ENDS, TS_NEEDS_NOTHING) \
  X(werewolf_dead, "$81:AC2A", WEREWOLF_DEAD_PC, creature_unhandled, ENDS, TS_NEEDS_NOTHING) \
  X(werewolf_unhandled, "$81:AC32", WEREWOLF_UNHANDLED_PC, werewolf_unhandled, ENDS, TS_NEEDS_NOTHING) \
  X(chainsaw_dead, "$81:9887", CHAINSAW_DEATH_PC, creature_unhandled, ENDS, TS_NEEDS_NOTHING) \
  X(chainsaw_unhandled, "$81:988F", CHAINSAW_UNHANDLED_PC, chainsaw_unhandled, ENDS, TS_NEEDS_NOTHING) \
  X(chainsaw_scored, "$81:989B", CHAINSAW_SCORED_PC, chainsaw_scored, ENDS, TS_NEEDS_RECORD) \
  X(zombie_unscored, "$81:8EF0", ZOMBIE_UNSCORED_PC, zombie_unscored, ENDS, TS_NEEDS_NOTHING) \
  X(zombie_freed, "$81:8EF6", ZOMBIE_FREED_PC, zombie_freed, ENDS, TS_NEEDS_ZOMBIE_FREED) \
  X(clone_unscored, "$81:8F70", CLONE_UNSCORED_PC, zombie_unscored, ENDS, TS_NEEDS_NOTHING) \
  X(clone_freed, "$81:8F76", CLONE_FREED_PC, zombie_freed, ENDS, TS_NEEDS_ZOMBIE_FREED) \
  X(werewolf_counted, "$81:AC48", WEREWOLF_COUNTED_PC, werewolf_counted, ENDS, TS_NEEDS_WEREWOLF_COUNTED) \
  X(werewolf_heard, "$81:AC60", WEREWOLF_HEARD_PC, werewolf_heard, ENDS, TS_NEEDS_NOTHING) \
  X(vampire_counted, "$82:EF66", VAMPIRE_COUNTED_PC, vampire_counted, ENDS, TS_NEEDS_NOTHING) \
  X(fishman_fate, "$81:E4C1", FISHMAN_FATE_PC, fishman_fate, FISHMAN_DEATH, TS_NEEDS_NOTHING) \
  X(fishman_unhandled, "$81:E4C9", FISHMAN_UNHANDLED_PC, fishman_unhandled, FISHMAN_DEATH, TS_NEEDS_NOTHING) \
  X(fishman_counted, "$81:E4DF", FISHMAN_COUNTED_PC, fishman_counted, FISHMAN_DEATH, TS_NEEDS_RECORD) \
  X(fishman_patrol_counted, "$81:E572", FISHMAN_PATROL_COUNTED_PC, fishman_counted, FISHMAN_DEATH, TS_NEEDS_RECORD) \
  X(fishman_patrol_ended, "$81:E59A", FISHMAN_PATROL_ENDED_PC, fishman_patrol_ended, FISHMAN_DEATH, TS_NEEDS_FISHMAN_ENDED) \
  X(fishman_patrol_freed, "$81:E5AC", FISHMAN_PATROL_FREED_PC, fishman_patrol_freed, FISHMAN_DEATH, TS_NEEDS_NOTHING) \
  X(bolt_enter, "$81:F808", BOLT_ENTER_PC, bolt_enter, BOLT, TS_NEEDS_NOTHING) \
  X(bolt_heard, "$81:F819", BOLT_HEARD_PC, bolt_heard, BOLT, TS_NEEDS_NOTHING) \
  X(bolt_made, "$81:F8A1", BOLT_MADE_PC, bolt_made, BOLT, TS_NEEDS_BOLT_MADE) \
  X(pause_dim, "$80:89C2", PAUSE_DIM_PC, pause_dim, PAUSE, TS_NEEDS_NOTHING) \
  X(pause_over, "$80:89E9", PAUSE_OVER_PC, pause_over, PAUSE, TS_NEEDS_NOTHING) \
  X(pause_resumed, "$80:89F0", PAUSE_RESUMED_PC, pause_resumed, PAUSE, TS_NEEDS_NOTHING) \
  X(main_frame_rest, "$80:8537", MAIN_FRAME_REST_PC, main_frame_rest, MAIN_REST, TS_NEEDS_NOTHING) \
  X(stung, "$80:F9D0", STUNG_PC, stung, STUNG, TS_NEEDS_STUNG) \
  X(stung_heard, "$80:F9E2", STUNG_HEARD_PC, stung_heard, STUNG, TS_NEEDS_STUNG_SHOWN) \
  X(stung_held, "$80:F9F2", STUNG_HELD_PC, stung_held, STUNG, TS_NEEDS_NOTHING) \
  X(ant_flash, "$81:BAEC", ANT_FLASH_PC, ant_flash, ANT_FLASH, TS_NEEDS_RECORD) \
  X(ant_flashed, "$81:BAFB", ANT_FLASHED_PC, ant_flashed, ANT_FLASH, TS_NEEDS_RECORD) \
  X(spider_enter, "$83:B27B", SPIDER_ENTER_PC, spider_enter, SPIDER, TS_NEEDS_NOTHING) \
  X(spider_made, "$83:B34A", SPIDER_MADE_PC, spider_made, SPIDER, TS_NEEDS_SPIDER_MADE) \
  X(walker_enter, "$81:D708", WALKER_ENTER_PC, walker_enter, WALKER, TS_NEEDS_NOTHING) \
  X(walker_made, "$81:D691", WALKER_MADE_PC, walker_made, WALKER, TS_NEEDS_WALKER_MADE) \
  X(walker_shown, "$81:D6E5", WALKER_SHOWN_PC, walker_shown, WALKER, TS_NEEDS_WALKER_SHOWN) \
  X(chainsaw_enter, "$81:983A", CHAINSAW_ENTER_PC, chainsaw_enter, CHAINSAW, TS_NEEDS_NOTHING) \
  X(chainsaw_made, "$81:9733", CHAINSAW_MADE_PC, chainsaw_made, CHAINSAW, TS_NEEDS_CHAINSAW_MADE) \
  X(chainsaw_set, "$81:9854", CHAINSAW_SET_PC, chainsaw_set, CHAINSAW, TS_NEEDS_NOTHING) \
  X(chainsaw_marked, "$81:985A", CHAINSAW_MARKED_PC, chainsaw_marked, CHAINSAW, TS_NEEDS_NOTHING) \
  X(chainsaw_heard, "$81:9820", CHAINSAW_PICTURES_PC, chainsaw_heard, CHAINSAW, TS_NEEDS_NOTHING) \
  X(chainsaw_shown, "$81:9827", CHAINSAW_SHOWN_PC, chainsaw_shown, CHAINSAW, TS_NEEDS_CHAINSAW_SHOWN) \
  X(chainsaw_handled, "$81:986F", CHAINSAW_HANDLED_PC, chainsaw_handled, CHAINSAW, TS_NEEDS_NOTHING) \
  X(door_buzz, "$82:DEFB", DOOR_BUZZ_PC, door_buzz, DOOR, TS_NEEDS_NOTHING) \
  X(door_buzzed, "$82:DF1F", DOOR_BUZZED_PC, door_buzzed, DOOR, TS_NEEDS_NOTHING) \
  X(slime_enter, "$81:CCCF", SLIME_ENTER_PC, slime_enter, SLIME, TS_NEEDS_NOTHING) \
  X(slime_shown, "$81:CCAC", SLIME_SHOWN_PC, slime_shown, SLIME, TS_NEEDS_SLIME_BEGUN) \
  X(slime_way, "$81:C9E8", SLIME_WAY_PC, slime_way, SLIME, TS_NEEDS_SLIME_WAY) \
  X(argument_vampire_back, "$82:EC5A", 0x82ec5au, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(vampire_land, "$82:EA35", VAMPIRE_LAND_PC, vampire_land, VAMPIRE, TS_NEEDS_NOTHING) \
  X(vampire_land_sounded, "$82:EA44", VAMPIRE_LAND_SOUNDED_PC, vampire_land_sounded, VAMPIRE, TS_NEEDS_NOTHING) \
  X(vampire_landed, "$82:EA4B", VAMPIRE_LANDED_PC, vampire_landed, VAMPIRE, TS_NEEDS_NOTHING) \
  X(vampire_land_way, "$82:EA5E", VAMPIRE_LAND_WAY_PC, vampire_land_way, VAMPIRE, TS_NEEDS_VAMPIRE) \
  X(vampire_fired, "$82:EE1B", VAMPIRE_FIRED_PC, vampire_fired, VAMPIRE, TS_NEEDS_NOTHING) \
  X(vampire_spawned, "$82:EE3F", VAMPIRE_SPAWNED_PC, vampire_spawned, VAMPIRE, TS_NEEDS_NOTHING) \
  X(vampire_spawn_again, "$82:EE4A", VAMPIRE_SPAWN_AGAIN_PC, vampire_spawn_again, VAMPIRE, TS_NEEDS_NOTHING) \
  X(vampire_next, "$82:EE62", VAMPIRE_NEXT_PC, vampire_next, VAMPIRE, TS_NEEDS_VAMPIRE_NEXT) \
  X(vampire_hidden, "$82:EBA3", VAMPIRE_HIDDEN_PC, vampire_hidden, VAMPIRE, TS_NEEDS_VAMPIRE_HIDDEN) \
  X(vampire_back, "$82:EC61", VAMPIRE_BACK_PC, vampire_back, VAMPIRE, TS_NEEDS_VAMPIRE) \
  X(vampire_enter, "$82:EF36", VAMPIRE_ENTER_PC, vampire_enter, VAMPIRE, TS_NEEDS_NOTHING) \
  X(vampire_gone, "$82:EF3C", VAMPIRE_GONE_PC, vampire_gone, VAMPIRE, TS_NEEDS_NOTHING) \
  X(vampire_made, "$82:EF8F", VAMPIRE_MADE_PC, vampire_made, VAMPIRE, TS_NEEDS_VAMPIRE_MADE) \
  X(vampire_sounded, "$82:EFED", VAMPIRE_SOUNDED_PC, vampire_sounded, VAMPIRE, TS_NEEDS_VAMPIRE_SOUNDED) \
  X(bubbled, "$81:8404", BUBBLED_PC, bubbled, BUBBLE, TS_NEEDS_RECORD) \
  X(bubble_over, "$81:8446", BUBBLE_OVER_PC, bubble_over, BUBBLE, TS_NEEDS_RECORD) \
  X(breakable_enter, "$82:F25D", BREAKABLE_ENTER_PC, breakable_enter, BREAKABLE, TS_NEEDS_NOTHING) \
  X(breakable_made, "$82:F2F0", BREAKABLE_MADE_PC, breakable_made, BREAKABLE, TS_NEEDS_BREAKABLE_MADE) \
  X(frankenstein_enter, "$81:A455", FRANKENSTEIN_ENTER_PC, frankenstein_enter, FRANKENSTEIN, TS_NEEDS_NOTHING) \
  X(frankenstein_begun, "$81:A414", FRANKENSTEIN_BEGUN_PC, frankenstein_begun, FRANKENSTEIN, TS_NEEDS_FRANKENSTEIN_BEGUN) \
  X(decoy_enter, "$81:F159", DECOY_ENTER_PC, decoy_enter, DECOY_START, TS_NEEDS_NOTHING) \
  X(decoy_made, "$81:F204", DECOY_MADE_PC, decoy_made, DECOY_START, TS_NEEDS_DECOY_MADE) \
  X(decoy_first, "$81:F166", DECOY_FIRST_PC, decoy_first, DECOY_START, TS_NEEDS_NOTHING) \
  X(tracker_enter, "$82:F03E", TRACKER_ENTER_PC, tracker_enter, TRACKER, TS_NEEDS_NOTHING) \
  X(tracker_begun, "$82:F04B", TRACKER_BEGUN_PC, tracker_begun, TRACKER, TS_NEEDS_NOTHING) \
  X(tracker_ended, "$82:F05E", TRACKER_ENDED_PC, tracker_ended, TRACKER, TS_NEEDS_RECORD) \
  X(puff_enter, "$82:F1E6", PUFF_ENTER_PC, puff_enter, TRACKER, TS_NEEDS_NOTHING) \
  X(puff_made, "$82:F1F4", PUFF_MADE_PC, puff_made, TRACKER, TS_NEEDS_A) \
  X(piece_enter, "$83:B1DD", PIECE_ENTER_PC, piece_enter, TRACKER, TS_NEEDS_NOTHING) \
  X(piece_made, "$83:B1EB", PIECE_MADE_PC, piece_made, TRACKER, TS_NEEDS_A) \
  X(piece_heard, "$83:B226", PIECE_HEARD_PC, piece_heard, TRACKER, TS_NEEDS_NOTHING) \
  X(pulse_enter, "$80:A0EF", PALCYCLE_PULSE_ENTER_PC, pulse_enter, PULSE, TS_NEEDS_NOTHING) \
  X(argument_figure_queued, "$82:950A", 0x82950au, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_82a871, "$82:A871", 0x82a871u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_8085bb, "$80:85BB", 0x8085bbu, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_8085c2, "$80:85C2", 0x8085c2u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_80ea16, "$80:EA16", 0x80ea16u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_80ef7b, "$80:EF7B", 0x80ef7bu, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_80ef88, "$80:EF88", 0x80ef88u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_80efa3, "$80:EFA3", 0x80efa3u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_80efb2, "$80:EFB2", 0x80efb2u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81843b, "$81:843B", 0x81843bu, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81a471, "$81:A471", 0x81a471u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81c6d2, "$81:C6D2", 0x81c6d2u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81e500, "$81:E500", 0x81e500u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81e593, "$81:E593", 0x81e593u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81e8ba, "$81:E8BA", 0x81e8bau, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81f1e6, "$81:F1E6", 0x81f1e6u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81f9ab, "$81:F9AB", 0x81f9abu, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_82ef0b, "$82:EF0B", 0x82ef0bu, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_82f279, "$82:F279", 0x82f279u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_82f606, "$82:F606", 0x82f606u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_82f760, "$82:F760", 0x82f760u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_82f918, "$82:F918", 0x82f918u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81cd05, "$81:CD05", 0x81cd05u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(pair_80815f, "$80:815F", 0x80815fu, thread_pair, PAIR, TS_NEEDS_NOTHING) \
  X(pair_81f252, "$81:F252", 0x81f252u, thread_pair, PAIR, TS_NEEDS_NOTHING) \
  X(pair_82f91f, "$82:F91F", 0x82f91fu, thread_pair, PAIR, TS_NEEDS_NOTHING) \
  X(figure_enter, "$82:9569", FIGURE_ENTER_PC, figure_enter, FIGURE, TS_NEEDS_NOTHING) \
  X(figure_coloured, "$82:94EE", FIGURE_COLOURED_PC, figure_coloured, FIGURE, TS_NEEDS_NOTHING) \
  X(figure_waited, "$82:9511", FIGURE_WAITED_PC, figure_waited, FIGURE, TS_NEEDS_NOTHING) \
  X(figure_part, "$82:94B8", FIGURE_PART_PC, figure_part, FIGURE, TS_NEEDS_FIGURE_PART) \
  X(saucer_enter, "$82:873C", SAUCER_ENTER_PC, saucer_enter, SAUCER, TS_NEEDS_NOTHING) \
  X(saucer_coloured, "$82:8750", SAUCER_COLOURED_PC, saucer_coloured, SAUCER, TS_NEEDS_NOTHING) \
  X(saucer_drawn, "$82:876A", SAUCER_DRAWN_PC, saucer_drawn, SAUCER, TS_NEEDS_NOTHING) \
  X(saucer_queued, "$82:8786", SAUCER_QUEUED_PC, saucer_queued, SAUCER, TS_NEEDS_NOTHING) \
  X(saucer_aimed, "$82:84DD", SAUCER_AIMED_PC, saucer_aimed, SAUCER, TS_NEEDS_SAUCER_AIMED) \
  X(fourth_prize, "$80:EA02", FOURTH_PRIZE_PC, fourth_prize, FOURTH_PRIZE, TS_NEEDS_NOTHING) \
  X(argument_80d9ac, "$80:D9AC", 0x80d9acu, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(monster_pictured, "$80:D9B3", MONSTER_PICTURES_PC, monster_pictured, MONSTER, TS_NEEDS_NOTHING) \
  X(monster_begun, "$80:D9BC", MONSTER_BEGUN_PC, monster_begun, MONSTER, TS_NEEDS_NOTHING) \
  X(punch_first, "$80:EF82", PUNCH_FIRST_PC, punch_first, PUNCH, TS_NEEDS_NOTHING) \
  X(punch_again, "$80:EF8F", PUNCH_AGAIN_PC, punch_again, PUNCH, TS_NEEDS_NOTHING) \
  X(punch_second, "$80:EF98", PUNCH_SECOND_PC, punch_second, PUNCH, TS_NEEDS_NOTHING) \
  X(punch_third, "$80:EFAA", PUNCH_THIRD_PC, punch_third, PUNCH, TS_NEEDS_NOTHING) \
  X(punch_over, "$80:EFB9", PUNCH_OVER_PC, punch_over, PUNCH, TS_NEEDS_NOTHING) \
  X(player_leaves, "$80:FAF6", PLAYER_LEAVES_PC, player_leaves, LEAVES, TS_NEEDS_PLAYER_LOAD) \
  X(player_left, "$80:FB00", PLAYER_LEFT_PC, player_left, LEAVES, TS_NEEDS_PLAYER_LOAD) \
  X(player_left_panel, "$80:FB18", PLAYER_LEFT_PANEL_PC, player_left_panel, LEAVES, TS_NEEDS_NOTHING) \
  X(player_left_shown, "$80:FB1E", PLAYER_LEFT_SHOWN_PC, player_left_shown, LEAVES, TS_NEEDS_RECORD) \
  X(player_gone, "$80:FB35", PLAYER_GONE_PC, player_gone, LEAVES, TS_NEEDS_NOTHING) \
  X(monster_ends, "$80:DA54", MONSTER_ENDS_PC, monster_ends, MONSTER, TS_NEEDS_NOTHING) \
  X(bystander_meets, "$82:DDA7", BYSTANDER_MEETS_PC, bystander_meets, BYSTANDER_MEETS, TS_NEEDS_NOTHING) \
  X(bystander_hushed, "$82:DDCF", BYSTANDER_HUSH_PC, bystander_hushed, BYSTANDER_MEETS, TS_NEEDS_NOTHING) \
  X(bystander_hushed_2, "$82:DDD6", BYSTANDER_HUSH_2_PC, bystander_hushed_2, BYSTANDER_MEETS, TS_NEEDS_NOTHING) \
  X(bystander_said, "$82:DE2F", BYSTANDER_SAID_PC, bystander_said, BYSTANDER_MEETS, TS_NEEDS_NOTHING) \
  X(argument_82de4f, "$82:DE4F", 0x82de4fu, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(wait_frames, "$82:DEF7", WAIT_FRAMES_PC, wait_frames, WAIT_FRAMES, TS_NEEDS_NOTHING) \
  X(bystander_looked, "$82:DE56", BYSTANDER_LOOKED_PC, bystander_looked, BYSTANDER_MEETS, TS_NEEDS_NOTHING) \
  X(bystander_back, "$82:DD64", BYSTANDER_BACK_PC, bystander_back, BYSTANDER_BACK, TS_NEEDS_NOTHING)

#define X(name, sym, entry, port, exits, needs) \
  void port(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k);
// Several stretches share a port, so each is declared as often.
THREAD_STRETCHES(X)
#undef X
// ...and one that is not a row of its own: see `FOOTBALLER_OFF_PICTURED_PC`.
void footballer_off_pictured(Wram* w, const Rom* rom, PortCpu* c,
                             ThreadStretchWork* k);

// `$81:92D9`, the cut's hedge, to the `JSL` that puts the block. False if
// its cell is not where a word can be read.
bool cut_swap_supported(const Wram* w, const Rom* rom, uint16_t page);
void cut_swap(Wram* w, const Rom* rom, PortCpu* c, BlockSwapWork* k);

#endif
