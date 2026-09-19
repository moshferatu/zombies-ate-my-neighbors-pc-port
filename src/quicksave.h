// Quick save and quick load: F5 and F9, one file, the latest save.
//
// The core can save and load a machine (`snes_saveState`), and
// `zamn_headless --save` has used that since the routes were fitted. What the
// frontend plays is more than the core, though, and a save has to carry the
// rest or put it back:
//
//   * **The harness** (`CosimPriv`): a substituted routine whose cycle budget
//     is part spent when the frame ends is resumed by the harness, not by the
//     core, and a save state knows nothing of it. So a save is made only on a
//     tick `cosim_idle` says there is none of that -- nearly all of them; a
//     request waits for one -- and a load makes the harness forget whatever it
//     held (`cosim_forget_calls`).
//   * **The widescreen's books** (`Widescreen`): its tick-old copy of work RAM,
//     and above all the sprite cache slots it has borrowed and owes back
//     (`back_slot`). The saved video memory has the borrowed graphics in it;
//     with the list restored, the next frame's hook returns them as it always
//     does. Without it they would stay until the game happened to reload them.
//   * **The sprite pass's owner tables** (`sprite_oam_owners`,
//     `sprite_oam_history`), which say whose each sprite is: the smoothing
//     pairs sprites between ticks by them and the widescreen places the radar
//     and the game over's drips by them. Whether the pass ran in the saved
//     tick goes too (`fresh`), which is what `Layers.held_fresh` has to be for
//     the first picture after the load.
//
// What is not in it is what belongs to the launch and not to the game: the
// patches in the cartridge's copy of the ROM (`--level`, `--red-blood`, the
// widened spawn window, the logo bypass), the margin, scaling and smoothing.
// A save made in 16:9 loads in 4:3. And the top scores are not rolled back:
// after a load the file's table is put over the machine's, as after a boot.
//
// The file is `<rom>.quicksave` beside the ROM: a head (magic, the ROM's
// CRC-32, the two sizes), the core's state, the parts above in order. It is
// read whole and checked before the machine is touched, and written to a
// neighbour and moved over, so a bad file loads nothing and a failed save
// leaves the last one.
#ifndef ZAMN_QUICKSAVE_H
#define ZAMN_QUICKSAVE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "snes.h"

#define QUICKSAVE_MAGIC 0x3153515au  // 'ZQS1'

typedef struct {
  void* data;
  size_t size;
} QuickPart;

typedef struct {
  uint32_t magic, rom_crc, core_size, parts_size;
} QuickHead;

typedef struct {
  char path[1024];
  uint32_t rom_crc;
} QuickSave;

static inline uint32_t quicksave_crc(const uint8_t* data, size_t len) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

// `rom` is the image as read from the file, before anything is patched.
static inline void quicksave_init(QuickSave* q, const char* rom_path,
                                  const uint8_t* rom, size_t rom_len) {
  memset(q, 0, sizeof *q);
  snprintf(q->path, sizeof q->path - 8, "%s.quicksave", rom_path);
  q->rom_crc = quicksave_crc(rom, rom_len);
}

static inline size_t quicksave_parts_size(const QuickPart* parts, int count) {
  size_t n = 0;
  for (int i = 0; i < count; i++) n += parts[i].size;
  return n;
}

// The machine must be stopped, and idle in the harness's sense.
static inline bool quicksave_write(const QuickSave* q, Snes* snes,
                                   const QuickPart* parts, int count) {
  QuickHead h = {QUICKSAVE_MAGIC, q->rom_crc, 0, (uint32_t)quicksave_parts_size(parts, count)};
  h.core_size = (uint32_t)snes_saveState(snes, NULL);
  uint8_t* core = (uint8_t*)malloc(h.core_size);
  if (!core) return false;
  snes_saveState(snes, core);
  char tmp[1040];
  snprintf(tmp, sizeof tmp, "%s.tmp", q->path);
  FILE* f = fopen(tmp, "wb");
  bool ok = f && fwrite(&h, sizeof h, 1, f) == 1 && fwrite(core, h.core_size, 1, f) == 1;
  for (int i = 0; ok && i < count; i++) ok = fwrite(parts[i].data, parts[i].size, 1, f) == 1;
  if (f) ok = fclose(f) == 0 && ok;
  free(core);
#ifdef _WIN32
  ok = ok && MoveFileExA(tmp, q->path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
  ok = ok && rename(tmp, q->path) == 0;
#endif
  if (!ok) remove(tmp);
  return ok;
}

typedef enum { QUICKLOAD_OK, QUICKLOAD_NONE, QUICKLOAD_BAD } QuickLoad;

// The machine must be stopped. `QUICKLOAD_NONE` is no file; `QUICKLOAD_BAD` a
// file that is not a save of this ROM by this build. Either way nothing has
// been touched.
static inline QuickLoad quicksave_read(const QuickSave* q, Snes* snes,
                                       const QuickPart* parts, int count) {
  FILE* f = fopen(q->path, "rb");
  if (!f) return QUICKLOAD_NONE;
  QuickHead h;
  bool ok = fread(&h, sizeof h, 1, f) == 1 && h.magic == QUICKSAVE_MAGIC &&
            h.rom_crc == q->rom_crc &&
            h.core_size == (uint32_t)snes_saveState(snes, NULL) &&
            h.parts_size == (uint32_t)quicksave_parts_size(parts, count);
  uint8_t* data = ok ? (uint8_t*)malloc((size_t)h.core_size + h.parts_size) : NULL;
  ok = ok && data && fread(data, (size_t)h.core_size + h.parts_size, 1, f) == 1 &&
       fgetc(f) == EOF;
  fclose(f);
  // The core checks its own head before it writes a field, and past that it
  // does not fail; the parts are plain memory.
  ok = ok && snes_loadState(snes, data, (int)h.core_size);
  if (ok) {
    const uint8_t* at = data + h.core_size;
    for (int i = 0; i < count; i++) {
      memcpy(parts[i].data, at, parts[i].size);
      at += parts[i].size;
    }
  }
  free(data);
  return ok ? QUICKLOAD_OK : QUICKLOAD_BAD;
}

#endif
