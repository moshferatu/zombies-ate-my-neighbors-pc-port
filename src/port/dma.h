// Sending memory to the picture hardware, and six vblank jobs that do
// little else.
//
//   $80:C872  dma_to_cgram        bytes of colours, from the first colour on
//   $80:C8B8  dma_to_vram         bytes of tiles or of a tilemap, to an address
//   $80:C892  dma_to_cgram_at     bytes of colours, from a colour its caller names
//   $80:C2AB  hud_tiles_job       the third layer's tiles, from the cartridge
//   $80:C2E8  hud_layer_set       ...and where that layer's map and tiles are
//   $80:A084  palette_job         the level's 256 colours
//   $80:A09E  background_job      the background's 128, from their second copy
//   $82:D88C  tile_anim_job       the animated tiles that changed this frame
//   $80:C34A  hud_upload_job      the HUD's four rows, from their shadow
//   $82:8308  colours_112_job     sixteen colours, from the 112th on
//   $80:9F62  vram_clear_job      a kilobyte of VRAM zeroed, a vblank at a time
//   $82:B82E  text_map_job        the words a screen has printed, two rows
//   $82:B9B6                      ...of sixteen: the same job again
//
// Each fills in DMA channel 0 and starts it. None of them writes the machine:
// like every routine in `port/hw.h`'s scheme it records what it stored, in
// the ROM's order, between the runs of its own instructions, and the harness
// makes the writes on the ROM's cycles.
//
// ## The colours
//
// A level's colours are kept at `$7E:5428`, the background's 128 and then the
// sprites'. The background's are kept a second time at `$7E:5628`, and that
// copy is the one the level's colour animations write, so it is the one shown.
// `palette_job` sends the first table whole; `background_job` sends the copy.
//
// ## The animated tiles
//
// `port/bodies.h`'s `level_tile_anim` steps up to eight tiles through their
// frames and sets a bit for each one that changed. This job is the other
// half. For each bit, last slot first, it does two things:
//
// **It gives the tile its frame's attributes.** A level's tiles each have a
// word that says what they are to walk on, and a tile that animates between
// water and ground has to change that as well as its picture. The word comes
// from the level's own table, through the pointer at `$BE`, and goes into the
// working copy the collision reads, through the pointer at `$BA`.
//
// **It sends the frame's 32 bytes** to the tile's place in VRAM.
//
// The bits are the top eight of a word, shifted out one at a time, so the
// word is zero when the job is done.
//
// ## Its contract with the ROM
//
// WRAM is written as the ROM writes it, and the trace is the ROM's writes in
// its order. The registers each leaves are in the notes on each below.
//
// Port code: libc only.

#ifndef PORT_DMA_H
#define PORT_DMA_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/hw.h"
#include "port/wram.h"

#define DMA_TO_CGRAM_PC 0x80c872u
#define DMA_TO_CGRAM_RTL_PC 0x80c891u
#define DMA_TO_VRAM_PC 0x80c8b8u
#define DMA_TO_VRAM_RTL_PC 0x80c8dbu
#define DMA_TO_CGRAM_AT_PC 0x80c892u
#define DMA_TO_CGRAM_AT_RTL_PC 0x80c8b7u
#define HUD_TILES_JOB_PC 0x80c2abu
#define HUD_TILES_JOB_RTL_PC 0x80c2d9u
#define HUD_LAYER_SET_PC 0x80c2e8u
#define HUD_LAYER_SET_RTL_PC 0x80c2f6u
#define WINDOW_HDMA_SETUP_PC 0x82df23u
#define WINDOW_HDMA_SETUP_RTL_PC 0x82df6au
#define CAMERA_JOB_BEGIN_PC 0x828000u
#define CAMERA_JOB_BEGIN_QUEUE_PC 0x82800fu  // `JSL vbl_queue_b_add`
#define PALETTE_JOB_PC 0x80a084u
#define PALETTE_JOB_RTL_PC 0x80a092u
#define BACKGROUND_JOB_PC 0x80a09eu
#define BACKGROUND_JOB_RTL_PC 0x80a0acu
#define TILE_ANIM_JOB_PC 0x82d88cu
#define TILE_ANIM_JOB_RTL_PC 0x82d8cau
#define HUD_UPLOAD_JOB_PC 0x80c34au
#define HUD_UPLOAD_JOB_RTL_PC 0x80c378u
#define COLOURS_112_JOB_PC 0x828308u
#define COLOURS_112_JOB_RTL_PC 0x828336u
#define VRAM_CLEAR_JOB_PC 0x809f62u
#define VRAM_CLEAR_JOB_RTL_PC 0x809f98u  // of two: the other is at `$9F9A`
#define COLOURS_112_ASK_PC 0x828163u
#define COLOURS_112_ASK_RTL_PC 0x82816eu
#define VRAM_WIPE_PC 0x809fb0u
#define VRAM_WIPE_RTL_PC 0x809fdcu
#define COLOURS_JOB_PC 0x809fdfu
#define COLOURS_JOB_RTL_PC 0x80a036u
#define TEXT_MAP_JOB_PC 0x82b82eu
#define TEXT_MAP_JOB_RTL_PC 0x82b849u
#define TEXT_LINES_JOB_PC 0x82b9b6u   // the same instructions
#define TEXT_LINES_JOB_RTL_PC 0x82b9d1u

// The third layer's tiles, in the cartridge, and where its map and they are
// in VRAM as the two registers say it.
#define HUD_TILES_AT 0xe841u
#define HUD_TILES_BANK 0x96u
#define HUD_TILES_BYTES 0x1190u
#define HUD_TILES_VRAM 0x4000u
#define HUD_LAYER_MAP 0x64u    // `$2109`
#define HUD_LAYER_TILES 0x44u  // `$210C`
// The HUD's shadow, four rows of 32 words less 32, and where it goes.
#define HUD_SHADOW_AT 0x5f36u
#define HUD_SHADOW_BYTES 0x00c0u
#define HUD_VRAM_AT 0x6440u
// The sixteen colours `$82:8308` sends, in the level's table, and the colour
// they start at.
#define COLOURS_112_AT 0x5508u
#define COLOURS_112_BYTES 0x0020u
#define COLOURS_112_FIRST 0x70u
// `vram_clear_job`: the word address it has reached, a zero word in the
// cartridge to send over and over, and how much it sends a vblank.
#define W_VRAM_CLEAR_AT 0x00c8u
#define VRAM_CLEAR_SOURCE 0x9f9bu
#define VRAM_CLEAR_SOURCE_BANK 0x80u
#define VRAM_CLEAR_BYTES 0x0800u
#define VRAM_CLEAR_WORDS 0x0400u
// `vram_wipe`: from where in VRAM, how much, and a zero word of its own.
#define VRAM_WIPE_AT 0x2000u
#define VRAM_WIPE_BYTES 0xc000u
#define VRAM_WIPE_SOURCE 0x9fddu
// `colours_job`: the first 112 colours, and the 128 of the sprites'.
#define COLOURS_LOW_BYTES 0x00e0u
#define COLOURS_HIGH_FIRST 0x80u
#define COLOURS_HIGH_BYTES 0x0100u

// `text_map_job`: the map `port/text.h` prints into, how much of it goes,
// the layer's map in VRAM, and which kilobyte of that the text is on.
#define TEXT_MAP_JOB_AT 0x6502u       // `TEXT_MAP_AT`
#define TEXT_MAP_JOB_BYTES 0x0800u
#define TEXT_MAP_JOB_VRAM 0x6800u
#define W_TEXT_MAP_JOB_PAGE 0x1e8eu   // `W_TEXT_JOB_BYTE`

// The colours, as the level has them, and the background's second copy.
#define PALETTE_AT 0x5428u
#define PALETTE_BYTES 0x0200u
#define PALETTE_SHOWN_AT 0x5628u
#define PALETTE_SHOWN_BYTES 0x0100u
#define PALETTE_BANK 0x7eu

// The animated tiles: eight slots, a word each.
#define TILE_ANIM_SLOTS 8
#define TILE_ANIM_BYTES 0x0020u       // a tile's picture
#define W_TILE_ANIM_VRAM 0x6db4u      // where the tile is in VRAM
#define W_TILE_ANIM_SOURCE 0x6dc4u    // where its frame's picture is
#define W_TILE_ANIM_ATTR 0x6dd4u      // the tile's number, doubled
#define W_TILE_ANIM_FRAME_ATTR 0x6de4u  // the frame's, doubled
#define W_TILE_ANIM_CHANGED 0x1f56u   // a bit a slot, the top eight
#define W_TILE_ANIM_SOURCE_BANK 0x1e82u
// The level's table of attributes, a 24-bit pointer on page zero. The working
// copy's is `W_TILE_ATTRS`.
#define W_TILE_ATTRS_LEVEL 0x00beu

// The runs of the ROM's instructions between the writes, for the harness to
// price. A run ends where a store's write begins.
enum {
  DMA_STORE,     // STx abs, up to its write
  DMA_IMM,       // LDA #imm : STA abs
  DMA_TAKEN,     // a branch taken
  DMA_RTL,       // REP #$20 : RTL
  DC_HEAD,       // $C872 REP #$10 : SEP #$20 : STA $4304
  DV_HEAD,       // $C8B8 REP #$30 : STA $4302
  DV_BANK,       // LDA $04,S : SEP #$20 : STA $4304
  PJ_HEAD,       // LDA # : LDX # : LDY # : JSL
  PJ_TAIL,       // CLC : RTL
  TJ_HEAD,       // $D88C SEP #$20 : LDA #$80 : STA $2115
  TJ_START,      // REP #$20 : LDX #$000E
  TJ_TEST,       // ASL $1F56 : BCC
  TJ_SEND,       // $D89D-$D8BF, the level's attributes read from the cartridge
  TJ_SENT,       // PLA : PLX
  TJ_NEXT,       // DEX : DEX : BPL
  TJ_TAIL,       // CLC : RTL
  DMA_IMM16,     // LDA #imm16 : STA abs
  DMA_SEP_IMM,   // SEP #$20 : LDA #imm : STA abs
  DMA_REP_IMM16, // REP #$30 : LDA #imm16 : STA abs
  DMA_REP_RTL,   // REP #$20 : CLC : RTL
  DMA_FLAG_RTL,  // SEC or CLC, and RTL
  HJ_TAIL,       // REP #$30 : REP #$31 : RTL
  VC_HEAD,       // LDA $C8 : STA $2116
  VC_STEP,       // CLC : ADC #$0400 : STA $C8 : LDA #$1809 : STA $4300
  VC_TEST,       // REP #$20 : BIT $C8 : BMI
  TMJ_HEAD,      // $B82E-$B846, as far as the `JSL`'s own cycles
  TMJ_TAIL,      // PLA : CLC : RTL
  TPJ_HEAD,      // $80:AC55 LDA $ED : BNE
  TPJ_COUNT,     // REP #$30 : LDX $D0 : BEQ
  TPJ_FIRST,     // DEX : DEX : LDA $1C92,X : STA $2116
  TPJ_LOAD,      // LDA abs,X : STA abs
  TPJ_NEXT,      // DEX : DEX : BPL
  TPJ_TAIL,      // STZ $D0 : CLC : RTL
  LR_HEAD,       // $80:88A9 six STZ : SEP #$30 : LDA #$00 : STA $210D
  DCA_FIRST,     // REP #$20 : LDA $04,S : SEP #$20 : STA $2121
  DMA_REP,       // REP #$20
  DMA_SEP_STORE, // SEP #$20 : STZ abs
  DMA_REP_LOADS, // REP #$30 : LDA #imm16 : LDY #imm16
  DMA_REP_STZ16, // REP #$20 : STZ abs, to memory
  DMA_IMM_ORA,   // LDA #imm : ORA #imm : STA abs
  DMA_IMM16_MEM, // LDA #imm16 : STA abs, to memory
  DMA_REP_LOADS3,// REP #$20 : LDA #imm16 : LDY #imm16 : LDX #imm16
  DMA_JSL,       // JSL
  DMA_REP_PLB2,  // REP #$20 : PLB : PLB
  LC_BRIGHT,     // $82:AC2B LDA abs : AND # : STA abs : LDA # : STA abs
  LC_WORDS,      // LDA #imm16 : PLX
  DMA_LOAD,      // LDA #imm16
  OB_HEAD,       // $80:92F3 six STZ : SEP #$20 : LDA # : STA $212C
  OB_TAIL,       // REP #$20 : LDA # : STA abs : LDA # : LDY #
  SS_SEND,       // $80:921F STZ : STZ : LDA # : PHA : LDA # : LDX # : LDY #
  TH_HEAD,       // $80:941D STZ : STZ, a byte each : LDA # : STA $4360
  PL_HEAD,       // $80:98A9 LDA # : STA : STA : STZ : STZ : SEP #$20 : LDA # : STA $2125
  DMA_BLOCK_COUNT
};

// `$80:C872`. `bytes` from `bank:at` to the colours, starting at the first.
// It leaves 1 in A's low byte and A's high byte, X and Y as they were.
void dma_to_cgram(HwTrace* t, uint8_t bank, uint16_t at, uint16_t bytes);

// `$80:C8B8`. `bytes` from `bank:at` to VRAM at `vram`, a word address. The
// ROM's caller pushes the bank as a word; it leaves 1 in A's low byte and
// that word's high byte in A's.
void dma_to_vram(HwTrace* t, uint8_t bank, uint16_t at, uint16_t vram,
                 uint16_t bytes);

// `$80:A084` and `$80:A09E`. Both run once: carry clear. They leave 1 in A,
// and the table's address and length in X and Y.
void palette_job(HwTrace* t);
void background_job(HwTrace* t);

// Can `tile_anim_job` take this one? Only with the level's attributes in the
// cartridge, where every level has them, and the working copy in WRAM.
bool tile_anim_job_supported(const Wram* w);

// `$82:D88C`. Returns how many tiles it sent. It runs once: carry clear. X
// is left at `$FFFE`; with a tile sent, A is the word at `$1E82` and Y is 32.
int tile_anim_job(Wram* w, const Rom* rom, HwTrace* t);

// `$80:C892`. As `dma_to_cgram`, but from the colour `first`, which the
// ROM's caller pushes as a word. It leaves 1 in A's low byte and that
// word's high byte in A's.
void dma_to_cgram_at(HwTrace* t, uint8_t bank, uint16_t at, uint16_t bytes,
                     uint8_t first);

// `$80:C2AB`, which `$80:C2DA` queues after the HUD is reset. It runs once:
// carry clear. A is left `$4001`. It is `hud_upload_job` with other numbers.
void hud_tiles_job(HwTrace* t);

// `$80:C2E8`, the end of `$80:C2DA`, from where its `JSL` to queue the job
// comes back to its `RTL`. `$44` is left in A's low byte.
void hud_layer_set(HwTrace* t);

// `$82:DF23`, as far as its `RTL`: HDMA off, and channels 5, 6 and 7 set up
// to move the two windows' edges down the screen. Each writes a pair of
// registers a line. Channel 5 reads a table in WRAM for the first window,
// and 6 and 7 a table each in bank `$82`, for the first and the second.
// Nothing is switched on here. A is left the last table's address.
#define WINDOW_TABLE_WRAM 0x6da6u
#define WINDOW_TABLE_A 0xe691u
#define WINDOW_TABLE_B 0xe69bu
#define WINDOW_TABLE_BANK 0x82u
void window_hdma_setup(HwTrace* t);

// `$82:8000`, as far as its `JSL`: the first layer's map is 64 by 64 at
// `$6800`, and the job that scrolls it, `$82:8209`, is loaded to be queued.
#define CAMERA_JOB 0x8209u
#define CAMERA_JOB_BANK 0x0082u
void camera_job_begin(HwTrace* t);

// `$82:DB96` to its `RTL`, the end of the victims' list being parsed. Both
// windows are given to the colour arithmetic, with their four edges at
// zero. Inside them the fixed colour is taken off the second layer and the
// backdrop: five of each of red, green and blue. No channel writes every line. And the word at
// `$136E` is zero. I have not found what reads that.
#define COLOUR_WINDOW_PC 0x82db96u
#define COLOUR_WINDOW_RTL_PC 0x82dbc0u
#define COLOUR_WINDOW_FIXED 0xe5u  // the last byte A is given
#define W_COLOUR_WINDOW_WORD 0x136eu
void colour_window_reset(Wram* w, HwTrace* t);

// `$82:AC27` to `$82:AC44`, in the screen before a level
// (`port/level_card.h`). Through `$82:ACD6` the first layer is put at the
// left, its map at `$6800` and its tiles at `$2000`, and VRAM's address
// made to step on the high byte. Then full brightness goes in the shadow,
// only the first layer is turned on, and the first string is pulled. It
// stops on the call to the big letters, with the string's bank in A and
// the string in X, which it returns.
#define LEVEL_CARD_LAYER_PC 0x82ac27u
#define LEVEL_CARD_LAYER_END_PC 0x82ac44u
uint16_t level_card_layer(Wram* w, HwTrace* t, uint16_t s);

// `$82:ACB0` to the `RTL` at `$82:ACD5`, with the screen off. The level's
// layers: the first three and the sprites turned on, mode 1 with the third
// layer in front, the first layer's tiles at `$5000` and the second's at
// `$2000`, and the first layer at the top left. It pulls the spare byte and
// the caller's data bank, which it returns.
#define LEVEL_CARD_LAYERS_BACK_PC 0x82acb0u
#define LEVEL_CARD_RTL_PC 0x82acd5u
uint8_t level_card_layers_back(const Wram* w, HwTrace* t, uint16_t s);

// --- The front end's thread, where it writes to the hardware -----------------
//
// `$80:9126` between its calls is in `port/frontend_thread.h`. These are the
// stretches of it that write a register.
//
// The screen before the title is `$80:92F3`, and `$80:91F7` shows it:
//
//   $80:92F3  opening_begin      every layer's scroll zeroed, the first and
//                                third layers turned on and the brightness
//                                shadow zeroed. It stops on the call that
//                                queues `opening_job`.
//   $80:9219  screen_show_begin  the third layer alone, at its corner, and
//                                its map's send set out. It stops on that.
//   $80:9255  screen_show_end    every layer off, to the `RTL`.
//
// The title has a wave on its first layer: a table at `$7E:8000` of where
// each line is across, which `$80:9570` makes again every frame and HDMA's
// seventh channel sends, two bytes a line, to the layer's scroll.
//
//   $80:9418  title_hdma_setup   that channel pointed at the table, with
//                                every channel off. It stops where the
//                                title's sprites begin.
//   $80:94AF  title_layers       mode 1, the third layer in front with its
//                                tiles 16 across, and the first and third
//                                turned on. It stops at `title_begin`.
//   $80:9502  title_hdma_on      the channel on, and a frame to sleep in A.
//   $80:9529  title_hdma_off     ...and off, the first layer back at the
//                                left, and sixteen frames to sleep.
#define OPENING_BEGIN_PC 0x8092f3u
#define OPENING_BEGUN_PC 0x80931au
#define OPENING_JOB 0x938eu  // in bank `$80`
#define SCREEN_SHOW_BEGIN_PC 0x809219u
#define SCREEN_SHOW_BEGUN_PC 0x809232u
#define SCREEN_SHOW_MAP 0xde80u  // in bank `$9E`
#define SCREEN_SHOW_MAP_BANK 0x009eu
#define SCREEN_SHOW_MAP_VRAM 0x6400u
#define SCREEN_SHOW_MAP_BYTES 0x0780u
#define SCREEN_SHOW_END_PC 0x809255u
#define SCREEN_SHOW_RTL_PC 0x80925cu
#define TITLE_HDMA_SETUP_PC 0x809418u
#define TITLE_HDMA_SET_UP_PC 0x80943du
#define TITLE_WAVE_TABLE 0x8000u  // in bank `$7E`
#define TITLE_LAYERS_PC 0x8094afu
#define TITLE_LAYERS_END_PC 0x8094beu
#define TITLE_LAYERS_ON 0x0005u
#define TITLE_HDMA_ON_PC 0x809502u
#define TITLE_HDMA_ON_END_PC 0x80950eu
#define TITLE_HDMA_OFF_PC 0x809529u
#define TITLE_HDMA_OFF_END_PC 0x809536u
#define TITLE_WAVE_FRAME 0x0001u
#define TITLE_AFTER_WAVE_FRAMES 0x0010u

// The two screens before a game (`port/menus.h`):
//
//   $80:96C1  title_menu_layers  the sprites turned on with the title's two
//                                layers, for the menu's two choices. It
//                                stops at the menu's first pass.
//   $80:98A9  portraits_layers   the players' screen's: the first layer on
//                                the second screen and added to the third,
//                                under the second, the third and the
//                                sprites. Two layers' scroll is set first.
#define TITLE_MENU_LAYERS_PC 0x8096c1u
#define TITLE_MENU_LAYERS_END_PC 0x8096c7u
#define TITLE_MENU_LAYERS_ON 0x0015u
#define PORTRAITS_LAYERS_PC 0x8098a9u
#define PORTRAITS_LAYERS_END_PC 0x8098dfu
#define PORTRAITS_MAP_AT_6800 0x0068u
#define PORTRAITS_SCROLL_X 0x0006u

void title_menu_layers(HwTrace* t);
void portraits_layers(Wram* w, HwTrace* t);

void opening_begin(Wram* w, HwTrace* t);
// `s` is the stack pointer it finds, and it pushes a word.
void screen_show_begin(Wram* w, HwTrace* t, uint16_t s);
void screen_show_end(HwTrace* t);
void title_hdma_setup(Wram* w, HwTrace* t);
void title_layers(HwTrace* t);
void title_hdma_on(HwTrace* t);
void title_hdma_off(Wram* w, HwTrace* t);

// `$83:8015` to `$83:802D`, in the logo screens' routine (`port/intro.h`).
// Mode 1, with the first layer's tiles 16 across. The first layer's map is
// at `$4000` and four screens big, the second's at `$6000`, and both
// layers' tiles are at 0.
#define INTRO_LAYERS_SETUP_PC 0x838015u
#define INTRO_LAYERS_SETUP_END_PC 0x83802du  // `INTRO_FILL_PC`
void intro_layers_setup(HwTrace* t);

// `$83:80C7` to `$83:80F1`, after the first vblank the screen's job has
// had. The first layer's shadow is put a screen to the left and four down,
// the second's a screen to the left, and the two layers are turned on. It
// stops on a call to `intro_colours_copy`, having set out what that takes:
// the first screen's 128 bytes of colours.
#define INTRO_LAYERS_ON_PC 0x8380c7u
#define INTRO_LAYERS_ON_END_PC 0x8380f1u
#define INTRO_FIRST_COLOURS 0x91f0u  // in bank `$83`
#define INTRO_FIRST_COLOUR_BYTES 0x0080u
void intro_layers_on(Wram* w, HwTrace* t);

// `$80:C34A`, which `hud_refresh` queues when the HUD has changed
// (`port/hud.h`). It runs once: carry clear. A is left `$6401`, the high byte
// of the VRAM address over the 1 that started the transfer.
void hud_upload_job(HwTrace* t);

// `$82:8308`, which `$82:8163` queues: the level's colours 112 to 127 again,
// which is the eighth of the background's palettes. It runs once: carry
// clear, and 1 in A.
void colours_112_job(HwTrace* t);

// `$82:8163`, a job of the other queue, the one that runs after the
// picture: it puts `colours_112_job` on the vblank's. It runs once: carry
// clear. Returns the slot `vbl_queue_a_add` gave it, or -1 with that queue
// full.
int colours_112_ask(Wram* w);

// `$80:9F62`, which `$80:9F9D` queues and waits on: a kilobyte of VRAM zeroed
// from the word address at `$C8`, which it moves on. It stays queued, carry
// set, until that address reaches `$8000`: thirty-two vblanks from zero for
// the whole of VRAM. Returns the address it left. A is left `$0801`.
uint16_t vram_clear_job(Wram* w, HwTrace* t);

// `$80:9FB0`, which the screens between levels call once `vram_clear_job`
// is through: VRAM from the word address `$2000` up zeroed in one transfer,
// which is three quarters of it. A is left `$C001`. Carry is not touched.
void vram_wipe(HwTrace* t);

// `$80:9FDF`, a job of the vblank's: the level's colours sent in two
// transfers, the first 112 and then the sprites' 128. Colours 112 to 127 are
// `colours_112_job`'s to send. It runs once: carry clear. A is left `$0101`.
void colours_job(HwTrace* t);

// `$82:B82E`, which a screen in bank `$82` queues when it has printed: the
// first kilobyte of the text's map sent to the layer's map in VRAM, at the
// kilobyte `$1E8E` says. That is two rows of the thirty-two a map has for
// each of its sixteen lines of text. `$82:B9B6` is the same instructions,
// queued by the printer of several lines. It runs once: carry clear. A is
// left `$007E`, and the address in VRAM, which it returns, in X.
uint16_t text_map_job(const Wram* w, HwTrace* t);

// --- What a screen's sends are called with -----------------------------------
//
// A screen of the front end sets itself up with a run of calls, one after
// the other, to `dma_to_vram`, `dma_to_cgram` and `dma_to_cgram_at`, and
// once to `$80:CD20`. Between each two there is nothing but the numbers the
// next is called with. A send to VRAM is given its bank as a word on the
// stack, which its caller pulls off afterwards, and the rest in A, X and Y:
//
//     PLA               the word the last call was given
//     LDA #  : PHA      ...and this one's
//     LDA #  : LDX #  : LDY #
//
// Not every one has every part. Each is a stretch here, from where the last
// call comes back to the `JSL` of the next, or to the `RTS` or the first
// instruction that is something else. They are named for where they are.
// An absent part is -1.
//
//                                  pull  pushed  A       X       Y
#define SEND_ARGS_BY_ADDRESS(X) \
  /* `$80:91F7`, the screen before the title's colours */ \
  X(8091ff, "$80:91FF", 0x8091ffu, 0x809208u, 0, -1, 0x009e, 0x8e76, 0x0100) \
  X(80920c, "$80:920C", 0x80920cu, 0x809215u, 0, -1, 0x0094, 0xfeab, 0x0010) \
  X(809236, "$80:9236", 0x809236u, 0x809237u, 1, -1, -1, -1, -1) \
  /* `$80:9290`, that screen's tiles and maps */ \
  X(809290, "$80:9290", 0x809290u, 0x80929du, 0, 0x0097, 0xafc4, 0x5000, 0x0e20) \
  X(8092a1, "$80:92A1", 0x8092a1u, 0x8092afu, 1, 0x009e, 0xaf00, 0x6800, 0x0800) \
  X(8092b3, "$80:92B3", 0x8092b3u, 0x8092c1u, 1, 0x0092, 0xf6b1, 0x4000, 0x08e0) \
  X(8092c5, "$80:92C5", 0x8092c5u, 0x8092d3u, 1, 0x009e, 0xa700, 0x6400, 0x0800) \
  X(8092d7, "$80:92D7", 0x8092d7u, 0x8092e1u, 1, -1, 0x0083, 0xee8c, 0x0100) \
  X(8092e5, "$80:92E5", 0x8092e5u, 0x8092eeu, 0, -1, 0x0094, 0xfeab, 0x0010) \
  /* `$80:93AC`, the title's */ \
  X(8093b1, "$80:93B1", 0x8093b1u, 0x8093beu, 0, 0x0092, 0x8000, 0x5000, 0x2800) \
  X(8093c2, "$80:93C2", 0x8093c2u, 0x8093d0u, 1, 0x009e, 0xd700, 0x6800, 0x0800) \
  X(8093d4, "$80:93D4", 0x8093d4u, 0x8093e2u, 1, 0x0095, 0xd4c3, 0x4000, 0x17e0) \
  X(8093e6, "$80:93E6", 0x8093e6u, 0x8093f4u, 1, 0x009e, 0xcf00, 0x6400, 0x0800) \
  X(8093f8, "$80:93F8", 0x8093f8u, 0x809402u, 1, -1, 0x0083, 0xf28c, 0x0100) \
  X(809406, "$80:9406", 0x809406u, 0x809413u, 0, 0x0080, 0x0083, 0x9599, 0x0100) \
  X(809417, "$80:9417", 0x809417u, 0x809418u, 1, -1, -1, -1, -1) \
  /* `$80:97AC`, the portraits' screen's. The second send's length is what \
     `$80:CD20` left in Y. */ \
  X(8097ac, "$80:97AC", 0x8097acu, 0x8097b9u, 0, 0xa300, 0x0094, 0x007e, 0x8000) \
  X(8097bd, "$80:97BD", 0x8097bdu, 0x8097c8u, 1, 0x007e, 0x8000, 0x2000, -1) \
  X(8097cc, "$80:97CC", 0x8097ccu, 0x8097dau, 1, 0x0097, 0xd5a2, 0x4000, 0x0a70) \
  X(8097de, "$80:97DE", 0x8097deu, 0x8097ecu, 1, 0x0096, 0xff0d, 0x5000, 0x00e0) \
  X(8097f0, "$80:97F0", 0x8097f0u, 0x8097feu, 1, 0x009e, 0xbf00, 0x7000, 0x0800) \
  X(809802, "$80:9802", 0x809802u, 0x809810u, 1, 0x009e, 0xc700, 0x6800, 0x0800) \
  X(809814, "$80:9814", 0x809814u, 0x809822u, 1, 0x009e, 0xb700, 0x6400, 0x0800) \
  X(809826, "$80:9826", 0x809826u, 0x809830u, 1, -1, 0x0083, 0xef8c, 0x0100) \
  X(809834, "$80:9834", 0x809834u, 0x809841u, 0, 0x0080, 0x009e, 0x8f76, 0x0100) \
  X(809845, "$80:9845", 0x809845u, 0x809846u, 1, -1, -1, -1, -1) \
  /* `$80:9A90`, the job that sends the text map the portraits are on */ \
  X(809a90, "$80:9A90", 0x809a90u, 0x809a9du, 0, 0x007e, 0x6502, 0x71a0, 0x0400)

typedef struct {
  uint32_t pc, exit;
  bool pull;
  int32_t pushed, a, x, y;
} SendArgs;

enum {
#define X(at, sym, pc, exit, pull, pushed, a, x, y) SEND_ARGS_AT_##at,
  SEND_ARGS_BY_ADDRESS(X)
#undef X
  SEND_ARGS_COUNT
};
extern const SendArgs SEND_ARGS[SEND_ARGS_COUNT];

void send_args(Wram* w, PortCpu* c, const SendArgs* args);

#endif
