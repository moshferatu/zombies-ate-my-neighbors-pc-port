#include "sound/console.h"

#include <string.h>

#include "statehandler.h"

static void console_reset(void* user) {
  Sound* s = (Sound*)user;
  // The watcher belongs to the frontend, not the machine: a reset keeps it.
  const SoundWatchFn watch = s->watch;
  void* ctx = s->watch_ctx;
  sound_reset(s);
  s->watch = watch;
  s->watch_ctx = ctx;
}

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

// The whole machine, as bytes, but for what points outside it.
static void console_state(void* user, StateHandler* sh) {
  Sound* s = (Sound*)user;
  static Sound copy;
  const SoundWatchFn watch = s->watch;
  void* ctx = s->watch_ctx;
  if (sh->saving) {
    copy = *s;
    copy.dsp.ram = NULL;
    copy.watch = NULL;
    copy.watch_ctx = NULL;
    copy.fault = NULL;
    sh_handleByteArray(sh, (uint8_t*)&copy, (int)sizeof copy);
  } else {
    sh_handleByteArray(sh, (uint8_t*)&copy, (int)sizeof copy);
    *s = copy;
    s->dsp.ram = s->ram;
    s->watch = watch;
    s->watch_ctx = ctx;
    if (s->state == SOUND_HALTED) s->fault = "halted before the state was saved";
  }
}

void sound_attach(Snes* snes, Sound* s) {
  const SnesSound sound = {s, console_reset, console_run, console_read, console_write,
                           console_samples, console_state};
  snes_setSound(snes, &sound);
  console_reset(s);
}
