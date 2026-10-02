#include "movie.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* kButtonNames[12] = {
    "B", "Y", "Select", "Start", "Up", "Down",
    "Left", "Right", "A", "X", "L", "R",
};

// True when the first `len` characters of `name` spell `want`, in any case.
static bool same_name(const char* want, const char* name, int len) {
  if ((int)strlen(want) != len) return false;
  for (int i = 0; i < len; i++)
    if (tolower((unsigned char)want[i]) != tolower((unsigned char)name[i])) return false;
  return true;
}

static int button_index(const char* name, int len) {
  for (int i = 0; i < 12; i++) {
    if (same_name(kButtonNames[i], name, len)) {
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

static bool track_push(MovieTrack* t, int frame, uint16_t mask, int* cap) {
  if (t->count == *cap) {
    int grown_cap = *cap ? *cap * 2 : 64;
    MovieEvent* grown =
        (MovieEvent*)realloc(t->events, sizeof(MovieEvent) * (size_t)grown_cap);
    if (!grown) return false;
    t->events = grown;
    *cap = grown_cap;
  }
  t->events[t->count].frame = frame;
  t->events[t->count].buttons = mask;
  t->count++;
  return true;
}

bool movie_load(Movie* m, const char* path) {
  memset(m, 0, sizeof *m);
  FILE* f = fopen(path, "r");
  if (!f) return false;

  int cap[MOVIE_PORTS] = {0, 0};
  char line[256];
  int lineno = 0;
  while (fgets(line, sizeof line, f)) {
    lineno++;
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || *p == '\0') continue;

    // An optional `<n>:` prefix aims the line at a controller; default is 1.
    int port = 0;
    if (p[0] >= '1' && p[0] <= '0' + MOVIE_PORTS && p[1] == ':') {
      port = p[0] - '1';
      p += 2;
    }

    char buttons[128];
    int frame = 0;
    if (sscanf(p, "%d %127s", &frame, buttons) != 2) {
      fprintf(stderr, "movie: %s:%d: expected '[<port>:]<frame> <buttons>'\n",
              path, lineno);
      goto fail;
    }
    uint16_t mask = 0;
    if (!parse_buttons(buttons, &mask)) {
      fprintf(stderr, "movie: %s:%d: unknown button in '%s'\n", path, lineno,
              buttons);
      goto fail;
    }
    // Replay walks each port's list once, forwards, so an out-of-order line
    // would silently never be applied. Refuse it instead.
    MovieTrack* t = &m->track[port];
    if (t->count && frame < t->events[t->count - 1].frame) {
      fprintf(stderr,
              "movie: %s:%d: frame %d precedes %d on controller %d "
              "(frames must ascend within a port)\n",
              path, lineno, frame, t->events[t->count - 1].frame, port + 1);
      goto fail;
    }
    if (!track_push(t, frame, mask, &cap[port])) goto fail;
  }
  fclose(f);
  return true;

fail:
  fclose(f);
  movie_free(m);
  return false;
}

void movie_free(Movie* m) {
  for (int i = 0; i < MOVIE_PORTS; i++) free(m->track[i].events);
  memset(m, 0, sizeof *m);
}

MovieInput movie_state(Movie* m, int frame) {
  MovieInput in;
  for (int i = 0; i < MOVIE_PORTS; i++) {
    MovieTrack* t = &m->track[i];
    while (t->next < t->count && t->events[t->next].frame <= frame) {
      t->current = t->events[t->next].buttons;
      t->next++;
    }
    in.port[i] = t->current;
  }
  return in;
}

bool movie_uses_port(const Movie* m, int port) {
  if (port < 0 || port >= MOVIE_PORTS) return false;
  const MovieTrack* t = &m->track[port];
  for (int i = 0; i < t->count; i++) {
    if (t->events[i].buttons) return true;
  }
  return false;
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
