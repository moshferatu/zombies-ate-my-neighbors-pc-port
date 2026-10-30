// A saved state, written and read a field at a time, for `zamn`.
//
// It is what LakeSnes's `statehandler.h` declares, so the console and the
// port's own pieces (`video/state.c`, `sound/console.c`) save through it in
// either build, and a quick save from one loads in the other. The layout is
// the plain one: each field little-endian at its own size, a bool as one
// byte, a float or a double as its bits. A list of fields ends with NULL.
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "statehandler.h"

StateHandler* sh_init(bool saving, const uint8_t* data, int size) {
  StateHandler* sh = malloc(sizeof(StateHandler));
  if (!sh) return NULL;
  sh->saving = saving;
  sh->offset = 0;
  sh->allocSize = saving ? 1024 : size;
  sh->data = malloc((size_t)(sh->allocSize > 0 ? sh->allocSize : 1));
  if (!sh->data) {
    free(sh);
    return NULL;
  }
  if (!saving) memcpy(sh->data, data, (size_t)size);
  return sh;
}

void sh_free(StateHandler* sh) {
  free(sh->data);
  free(sh);
}

static void put(StateHandler* sh, uint8_t v) {
  if (sh->offset >= sh->allocSize) {
    sh->allocSize *= 2;
    sh->data = realloc(sh->data, (size_t)sh->allocSize);
  }
  sh->data[sh->offset++] = v;
}

// Past the end reads as nought.
static uint8_t get(StateHandler* sh) {
  return sh->offset < sh->allocSize ? sh->data[sh->offset++] : 0;
}

static void put_bits(StateHandler* sh, uint64_t v, int bytes) {
  for (int i = 0; i < bytes; i++) put(sh, (uint8_t)(v >> (8 * i)));
}

static uint64_t get_bits(StateHandler* sh, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; i++) v |= (uint64_t)get(sh) << (8 * i);
  return v;
}

// One of these for each width: each field in the list, saved or loaded.
#define FIELDS(name, type, bits_type)                                  \
  void name(StateHandler* sh, ...) {                                   \
    va_list args;                                                      \
    va_start(args, sh);                                                \
    for (type* v; (v = va_arg(args, type*)) != NULL;) {                \
      if (sh->saving) {                                                \
        bits_type b;                                                   \
        memcpy(&b, v, sizeof b);                                       \
        put_bits(sh, b, (int)sizeof b);                                \
      } else {                                                         \
        const bits_type b = (bits_type)get_bits(sh, (int)sizeof(type)); \
        memcpy(v, &b, sizeof b);                                       \
      }                                                                \
    }                                                                  \
    va_end(args);                                                      \
  }

FIELDS(sh_handleBytes, uint8_t, uint8_t)
FIELDS(sh_handleBytesS, int8_t, uint8_t)
FIELDS(sh_handleWords, uint16_t, uint16_t)
FIELDS(sh_handleWordsS, int16_t, uint16_t)
FIELDS(sh_handleInts, uint32_t, uint32_t)
FIELDS(sh_handleIntsS, int32_t, uint32_t)
FIELDS(sh_handleLongLongs, uint64_t, uint64_t)
FIELDS(sh_handleFloats, float, uint32_t)
FIELDS(sh_handleDoubles, double, uint64_t)
#undef FIELDS

// A bool is a byte, and any byte but nought loads as true.
void sh_handleBools(StateHandler* sh, ...) {
  va_list args;
  va_start(args, sh);
  for (bool* v; (v = va_arg(args, bool*)) != NULL;) {
    if (sh->saving) put(sh, *v ? 1 : 0);
    else *v = get(sh) != 0;
  }
  va_end(args);
}

void sh_handleByteArray(StateHandler* sh, uint8_t* data, int size) {
  for (int i = 0; i < size; i++) {
    if (sh->saving) put(sh, data[i]);
    else data[i] = get(sh);
  }
}

void sh_handleWordArray(StateHandler* sh, uint16_t* data, int size) {
  for (int i = 0; i < size; i++) {
    if (sh->saving) put_bits(sh, data[i], 2);
    else data[i] = (uint16_t)get_bits(sh, 2);
  }
}

// A word written at `location`, which the state is first made long enough
// to hold: the header's sizes, filled in once they are known.
void sh_placeInt(StateHandler* sh, int location, uint32_t value) {
  while (sh->offset < location + 4) put(sh, 0);
  for (int i = 0; i < 4; i++) sh->data[location + i] = (uint8_t)(value >> (8 * i));
}
