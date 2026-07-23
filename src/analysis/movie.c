#include "movie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* kButtonNames[12] = {
    "B", "Y", "Select", "Start", "Up", "Down",
    "Left", "Right", "A", "X", "L", "R",
};

static int button_index(const char* name, int len) {
  for (int i = 0; i < 12; i++) {
    if ((int)strlen(kButtonNames[i]) == len &&
        _strnicmp(kButtonNames[i], name, (size_t)len) == 0) {
      return i;
    }
  }
  return -1;
}

// Parse "Right+B" / "-" / "." into a button mask. Returns false on a bad name.
static bool parse_buttons(const char* s, uint16_t* out) {
  uint16_t mask = 0;
  if (*s == '-' || *s == '.') { *out = 0; return true; }
  while (*s) {
    const char* start = s;
    while (*s && *s != '+') s++;
    int idx = button_index(start, (int)(s - start));
    if (idx < 0) return false;
    mask |= (uint16_t)(1u << idx);
    if (*s == '+') s++;
  }
  *out = mask;
  return true;
}

bool movie_load(Movie* m, const char* path) {
  memset(m, 0, sizeof *m);
  FILE* f = fopen(path, "r");
  if (!f) return false;

  int cap = 64;
  m->events = (MovieEvent*)malloc(sizeof(MovieEvent) * cap);
  if (!m->events) { fclose(f); return false; }

  char line[256];
  int lineno = 0;
  while (fgets(line, sizeof line, f)) {
    lineno++;
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || *p == '\0') continue;

    char buttons[128];
    int frame = 0;
    if (sscanf(p, "%d %127s", &frame, buttons) != 2) {
      fprintf(stderr, "movie: %s:%d: expected '<frame> <buttons>'\n", path, lineno);
      fclose(f);
      movie_free(m);
      return false;
    }
    uint16_t mask = 0;
    if (!parse_buttons(buttons, &mask)) {
      fprintf(stderr, "movie: %s:%d: unknown button in '%s'\n", path, lineno, buttons);
      fclose(f);
      movie_free(m);
      return false;
    }
    if (m->count == cap) {
      cap *= 2;
      MovieEvent* grown = (MovieEvent*)realloc(m->events, sizeof(MovieEvent) * cap);
      if (!grown) { fclose(f); movie_free(m); return false; }
      m->events = grown;
    }
    m->events[m->count].frame = frame;
    m->events[m->count].buttons = mask;
    m->count++;
  }
  fclose(f);
  return true;
}

void movie_free(Movie* m) {
  free(m->events);
  memset(m, 0, sizeof *m);
}

uint16_t movie_state(Movie* m, int frame) {
  while (m->next < m->count && m->events[m->next].frame <= frame) {
    m->current = m->events[m->next].buttons;
    m->next++;
  }
  return m->current;
}

const char* movie_format(uint16_t buttons, char* out, int out_size) {
  if (buttons == 0) { snprintf(out, (size_t)out_size, "-"); return out; }
  int n = 0;
  out[0] = '\0';
  for (int i = 0; i < 12; i++) {
    if (!(buttons & (1u << i))) continue;
    n += snprintf(out + n, (size_t)(out_size - n), "%s%s", n ? "+" : "", kButtonNames[i]);
    if (n >= out_size) break;
  }
  return out;
}
