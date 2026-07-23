// Phase 0b interactive frontend: window + keyboard input + audio, driving the
// vendored SNES core. This is the "playable baseline" — the same core that
// serves as the reference oracle for the diff harness in later phases.
//
// Usage: zamn [rom.sfc]   (defaults to the ROM next to the executable's CWD)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <SDL.h>

#include "snes.h"

#define FB_W 512
#define FB_H 480
#define AUDIO_FREQ 48000
#define SAMPLES_PER_FRAME (AUDIO_FREQ / 60) // 800

// Keyboard -> SNES button bit (standard controller report order).
enum { BTN_B=0, BTN_Y=1, BTN_SELECT=2, BTN_START=3,
       BTN_UP=4, BTN_DOWN=5, BTN_LEFT=6, BTN_RIGHT=7,
       BTN_A=8, BTN_X=9, BTN_L=10, BTN_R=11 };

static int key_to_button(SDL_Keycode k) {
  switch (k) {
    case SDLK_UP:        return BTN_UP;
    case SDLK_DOWN:      return BTN_DOWN;
    case SDLK_LEFT:      return BTN_LEFT;
    case SDLK_RIGHT:     return BTN_RIGHT;
    case SDLK_z:         return BTN_B;
    case SDLK_x:         return BTN_A;
    case SDLK_a:         return BTN_Y;
    case SDLK_s:         return BTN_X;
    case SDLK_q:         return BTN_L;
    case SDLK_w:         return BTN_R;
    case SDLK_RETURN:    return BTN_START;
    case SDLK_RSHIFT:    return BTN_SELECT;
    case SDLK_BACKSPACE: return BTN_SELECT;
    default:             return -1;
  }
}

static uint8_t* read_file(const char* path, int* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  if (len <= 0) { fclose(f); return NULL; }
  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); return NULL; }
  fclose(f); *out_len = (int)len; return buf;
}

int main(int argc, char** argv) {
  const char* rom_path = (argc >= 2) ? argv[1] : "Zombies Ate My Neighbors.sfc";

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) { fprintf(stderr, "error: cannot read ROM '%s'\n", rom_path); return 1; }

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom, rom_len)) {
    fprintf(stderr, "error: core rejected ROM '%s'\n", rom_path);
    return 1;
  }
  // Default pixel format (RGBX) pairs byte-for-byte with an RGBX8888 texture.
  snes_reset(snes, true);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* win = SDL_CreateWindow("Zombies Ate My Neighbors (native)",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, FB_W, FB_H,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_RenderSetLogicalSize(ren, FB_W, FB_H);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBX8888,
      SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);

  SDL_AudioSpec want, have;
  SDL_memset(&want, 0, sizeof(want));
  want.freq = AUDIO_FREQ; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 2048;
  SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  int16_t audio_buf[SAMPLES_PER_FRAME * 2];
  if (audio) SDL_PauseAudioDevice(audio, 0);

  printf("Controls: Arrows=D-pad  Z=B X=A A=Y S=X  Q=L W=R  Enter=Start RShift=Select  Esc=Quit\n");

  // Frame pacing. VSync is unreliable (may be >60 Hz or driver-ignored), so we
  // pace explicitly. Primary clock is the audio device: it consumes samples at
  // a fixed 48000 Hz, so gating frame production on the queue draining locks the
  // loop to exactly 60 Hz and keeps A/V in sync. Fallback (no audio) is a timer.
  const Uint32 bytes_per_frame = (Uint32)(SAMPLES_PER_FRAME * 2 * sizeof(int16_t));
  const Uint32 audio_high_water = bytes_per_frame * 4; // keep <= ~4 frames buffered
  const double target_frame_ms = 1000.0 / 60.0;
  const Uint64 perf_freq = SDL_GetPerformanceFrequency();
  Uint64 prev_frame = SDL_GetPerformanceCounter();

  bool running = true;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        if (e.key.keysym.sym == SDLK_ESCAPE) { running = false; continue; }
        int b = key_to_button(e.key.keysym.sym);
        if (b >= 0) snes_setButtonState(snes, 1, b, e.type == SDL_KEYDOWN);
      }
    }

    // --- Pace to ~60 Hz before advancing the emulation ---
    if (audio) {
      while (running && SDL_GetQueuedAudioSize(audio) > audio_high_water)
        SDL_Delay(1);
    } else {
      for (;;) {
        double elapsed = (SDL_GetPerformanceCounter() - prev_frame) * 1000.0 / perf_freq;
        if (elapsed >= target_frame_ms) break;
        if (target_frame_ms - elapsed > 2.0) SDL_Delay(1); // busy-wait only the tail
      }
      prev_frame = SDL_GetPerformanceCounter();
    }

    snes_runFrame(snes);

    // Always queue this frame's audio (the queue is our clock; do not drop it).
    if (audio) {
      snes_setSamples(snes, audio_buf, SAMPLES_PER_FRAME);
      SDL_QueueAudio(audio, audio_buf, sizeof(audio_buf));
    }

    void* pixels; int pitch;
    if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0) {
      snes_setPixels(snes, (uint8_t*)pixels);
      SDL_UnlockTexture(tex);
    }
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren); // vsync paces the loop to the display refresh
  }

  if (audio) SDL_CloseAudioDevice(audio);
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  snes_free(snes);
  free(rom);
  return 0;
}
