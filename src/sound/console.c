#include "sound/console.h"

#include <stdlib.h>
#include <string.h>

#include "statehandler.h"

static void console_reset(void* user) { sound_reset((Sound*)user); }

// LakeSnes asks for `cycles` more, from where it last was. All of them are
// run: the instruction that straddles the end is the sound's own business
// (see `sound.h`), and what the game can see of it is the same either way.
static int console_run(void* user, int cycles) {
  Sound* s = (Sound*)user;
  sound_run(s, s->host + (uint64_t)cycles);
  return cycles;
}

static uint8_t console_read(void* user, uint8_t port) {
  return sound_read_port((Sound*)user, port);
}

static void console_write(void* user, uint8_t port, uint8_t val) {
  sound_write_port((Sound*)user, port, val);
}

static void console_samples(void* user, int16_t* out, int count) {
  sound_get_samples((const Sound*)user, out, count);
}

// The whole machine, as bytes, but for what points outside it: the chip, and
// the player that is its program.
static void console_state(void* user, StateHandler* sh) {
  Sound* s = (Sound*)user;
  static Sound copy;
  const SoundProgram program = s->program;
  const SoundWatchFn watch = s->watch;
  void* ctx = s->watch_ctx;
  if (sh->saving) {
    copy = *s;
    copy.dsp.ram = NULL;
    memset(&copy.program, 0, sizeof copy.program);
    copy.watch = NULL;
    copy.watch_ctx = NULL;
    copy.fault = NULL;
    sh_handleByteArray(sh, (uint8_t*)&copy, (int)sizeof copy);
  } else {
    sh_handleByteArray(sh, (uint8_t*)&copy, (int)sizeof copy);
    *s = copy;
    s->dsp.ram = s->ram;
    s->program = program;
    s->watch = watch;
    s->watch_ctx = ctx;
    if (s->state == SOUND_HALTED) s->fault = "halted before the state was saved";
  }

  Player* lead = player_leading(s);
  if (!lead) return;
  const size_t n = player_state_size(lead);
  uint8_t* bytes = (uint8_t*)malloc(n);
  if (!bytes) return;  // the sizes then differ, and the state will not load
  if (sh->saving) player_save(lead, bytes);
  sh_handleByteArray(sh, bytes, (int)n);
  if (!sh->saving && !player_load(lead, bytes, n)) {
    s->state = SOUND_HALTED;
    s->fault = "the state's player has other settings";
  }
  free(bytes);
}

void sound_attach(Snes* snes, Sound* s, Player* lead) {
  player_lead(lead, s);
  const SnesSound sound = {s, console_reset, console_run, console_read, console_write,
                           console_samples, console_state};
  snes_setSound(snes, &sound);
  console_reset(s);
}
