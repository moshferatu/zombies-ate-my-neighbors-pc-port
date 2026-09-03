// Putting the game into a state instead of playing it into one.
//
// The corpus is fifty movies of real input, and that is the right shape for
// most of what it proves. It is the wrong shape for one thing: a branch that
// needs a *rare state* rather than a long sequence. `d990b_bubble` is the
// standing example — no level record places both the bubble gun and the
// `$81:983A` creature, so the only legitimate route to it collects a weapon on
// level 21 and carries it across three level transitions, and four fitted
// attempts at the last of those died to a health budget the route was never
// priced in. That is a round of work for one branch.
//
// The observation that makes this header possible is about what `verify`
// actually checks. It intercepts a call, runs the ROM's routine and the port's
// against the machine state at that instant, and diffs the answers. It does not
// read, and cannot read, how that state came about. `d990b_bubble` asks the
// inventory what the player is holding; it does not ask where he got it. So a
// poked word is exactly as good an oracle test as a walked one, because the
// oracle is the ROM either way and the ROM is looking at the same WRAM.
//
// What this does not do, and the reason the movies stay:
//
//   * It proves nothing about *reachability*. A poked state may be one the game
//     can never produce, and a divergence found on an impossible state is a
//     divergence about code that never runs. The ROM is still the oracle, so an
//     agreement is still an agreement — but time spent chasing a disagreement
//     there is time wasted, and "fixing" the port toward it is a regression
//     waiting to happen. Believe a poked branch about the routine, not about
//     the game.
//   * It proves nothing about the *game*. Every movie in the corpus is a
//     legitimate input sequence from boot, so the corpus quietly doubles as
//     evidence that the port is playable and not merely per-call correct. No
//     poke tests that, and no number of pokes ever will.
//
// So this is the emulator-development trick rather than the decomp one: nobody
// plays a game through to check a PPU edge case, they write a ROM that provokes
// it. Sites reached only under `--poke` should be read as "the port's code for
// this agrees with the ROM's, on a state we asserted" — which is a weaker claim
// than the rest of the corpus makes, and worth more than the nothing that was
// there before.
//
// Spec: `<frame>[+]:<addr>=<value>[.b]`, the frame in decimal and the address
// and value in hex, because that is how every other flag in these tools reads
// them. `+` holds the value from that frame on instead of writing it once;
// `.b` writes one byte instead of a word. The address is a `Wram` offset, so
// `$7E:xxxx` is `xxxx` and `$7F:xxxx` is `1xxxx`.
//
//   --poke 10350:1CB8=000A      health to ten, once, at frame 10350
//   --poke 10350+:1CB8=000A     ...and hold it there
//   --poke 10350:1D0C=0004      four keys in hand
//
// Port code: libc only, no core dependency. The apply takes the raw WRAM
// pointer so both drivers can call it without agreeing on anything else.

#ifndef ZAMN_POKE_H
#define ZAMN_POKE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define POKE_MAX 32

typedef struct {
  int frame;      // the frame it is due on
  uint32_t addr;  // a Wram offset: $7E:xxxx is xxxx, $7F:xxxx is 1xxxx
  uint16_t value;
  bool hold;      // write it every frame from `frame` on, not once
  bool byte;      // one byte rather than a word
} Poke;

typedef struct {
  Poke at[POKE_MAX];
  int count;
} PokeList;

// Parse one `--poke` argument. Returns false and explains itself on stderr;
// every failure here is a typo in a command line, so the message names the
// piece that did not parse rather than repeating the grammar.
static inline bool poke_parse(PokeList* l, const char* spec) {
  if (l->count >= POKE_MAX) {
    fprintf(stderr, "error: at most %d --poke entries\n", POKE_MAX);
    return false;
  }
  Poke p;
  p.hold = false;
  p.byte = false;

  const char* s = spec;
  char* end = NULL;
  long frame = strtol(s, &end, 10);
  if (end == s || frame < 0) {
    fprintf(stderr, "error: --poke '%s': wanted a frame number first\n", spec);
    return false;
  }
  p.frame = (int)frame;
  s = end;
  if (*s == '+') {
    p.hold = true;
    s++;
  }
  if (*s != ':') {
    fprintf(stderr, "error: --poke '%s': wanted ':' after the frame\n", spec);
    return false;
  }
  s++;

  if (*s == '$') s++;
  long addr = strtol(s, &end, 16);
  if (end == s || addr < 0 || addr > 0x1FFFF) {
    fprintf(stderr, "error: --poke '%s': '%.*s' is not a WRAM offset\n", spec,
            (int)(end == s ? 1 : end - s), s);
    return false;
  }
  p.addr = (uint32_t)addr;
  s = end;
  if (*s != '=') {
    fprintf(stderr, "error: --poke '%s': wanted '=' after the address\n", spec);
    return false;
  }
  s++;

  if (*s == '$') s++;
  long value = strtol(s, &end, 16);
  if (end == s || value < 0 || value > 0xFFFF) {
    fprintf(stderr, "error: --poke '%s': '%s' is not a 16-bit value\n", spec, s);
    return false;
  }
  p.value = (uint16_t)value;
  s = end;

  if (*s == '.' && (s[1] == 'b' || s[1] == 'B')) {
    p.byte = true;
    s += 2;
    if (value > 0xFF) {
      fprintf(stderr, "error: --poke '%s': %lX does not fit in a byte\n", spec,
              value);
      return false;
    }
  }
  if (*s) {
    fprintf(stderr, "error: --poke '%s': trailing '%s'\n", spec, s);
    return false;
  }

  l->at[l->count++] = p;
  return true;
}

// Write whatever is due this frame. Called with the frame's inputs already
// applied and before the frame runs, so the game sees the value for the whole
// of the frame — the same position in the loop a cheat device's every-frame
// write would land in.
//
// `ram` is `snes->ram`, which `Wram` is layout-compatible with by construction.
static inline void poke_apply(const PokeList* l, uint8_t* ram, int frame) {
  for (int i = 0; i < l->count; i++) {
    const Poke* p = &l->at[i];
    if (p->hold ? frame < p->frame : frame != p->frame) continue;
    ram[p->addr & 0x1FFFF] = (uint8_t)p->value;
    if (!p->byte) ram[(p->addr + 1) & 0x1FFFF] = (uint8_t)(p->value >> 8);
  }
}

// Say out loud what a run has been told to assert.
//
// Not decoration. A coverage figure from a poked run and one from a walked run
// are different claims, and the difference is invisible in the number — so the
// run that made the weaker claim prints its reason next to it.
static inline void poke_report(const PokeList* l) {
  if (l->count == 0) return;
  printf("\n%d state assertion%s, so what follows is checked against the ROM on\n"
         "a state that was asserted rather than played into:\n",
         l->count, l->count == 1 ? "" : "s");
  for (int i = 0; i < l->count; i++) {
    const Poke* p = &l->at[i];
    printf("  frame %-6d $7%c:%04X = $%0*X%s\n", p->frame,
           p->addr >= 0x10000 ? 'F' : 'E', (unsigned)(p->addr & 0xFFFF),
           p->byte ? 2 : 4, p->value, p->hold ? ", held" : "");
  }
}

#endif  // ZAMN_POKE_H
