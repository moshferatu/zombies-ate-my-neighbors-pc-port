// The two pads' ports, for `zamn`.
//
// It is what LakeSnes's `input.h` declares. A pad is a shift register: while
// the latch line is high it takes the buttons held, and each read gives the
// next bit, B first, and ones once the sixteen are out. The console reads
// both pads this way after each frame for `$4218`-`$421F`, and the game reads
// nothing by hand. `currentState` is the buttons held, bit 0 B, as the
// frontend sets them.
#include <stdlib.h>

#include "input.h"

Input* input_init(Snes* snes) {
  Input* input = calloc(1, sizeof(Input));
  if (!input) return NULL;
  input->snes = snes;
  input->type = 1;  // a pad
  return input;
}

void input_free(Input* input) { free(input); }

void input_reset(Input* input) {
  input->latchLine = false;
  input->latchedState = 0;
}

void input_handleState(Input* input, StateHandler* sh) {
  sh_handleBytes(sh, &input->type, NULL);
  sh_handleBools(sh, &input->latchLine, NULL);
  sh_handleWords(sh, &input->currentState, &input->latchedState, NULL);
}

void input_latch(Input* input, bool value) {
  input->latchLine = value;
  if (value) input->latchedState = input->currentState;
}

uint8_t input_read(Input* input) {
  if (input->latchLine) input->latchedState = input->currentState;
  const uint8_t bit = input->latchedState & 1;
  input->latchedState = (uint16_t)(input->latchedState >> 1 | 0x8000);
  return bit;
}
