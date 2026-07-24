#include "port/coverage.h"

#ifdef PORT_COVERAGE

unsigned long port_cover_hits[PORT_COVER_COUNT];

static const char* const kNames[PORT_COVER_COUNT] = {
#define PORT_COVER_NAME_(id, routine, what) #id,
    PORT_COVER_SITES(PORT_COVER_NAME_)
#undef PORT_COVER_NAME_
};

static const char* const kRoutines[PORT_COVER_COUNT] = {
#define PORT_COVER_ROUTINE_(id, routine, what) routine,
    PORT_COVER_SITES(PORT_COVER_ROUTINE_)
#undef PORT_COVER_ROUTINE_
};

static const char* const kWhat[PORT_COVER_COUNT] = {
#define PORT_COVER_WHAT_(id, routine, what) what,
    PORT_COVER_SITES(PORT_COVER_WHAT_)
#undef PORT_COVER_WHAT_
};

const char* port_cover_name(int id) {
  return (id >= 0 && id < PORT_COVER_COUNT) ? kNames[id] : "?";
}

const char* port_cover_routine(int id) {
  return (id >= 0 && id < PORT_COVER_COUNT) ? kRoutines[id] : "?";
}

const char* port_cover_what(int id) {
  return (id >= 0 && id < PORT_COVER_COUNT) ? kWhat[id] : "?";
}

void port_cover_reset(void) {
  for (int i = 0; i < PORT_COVER_COUNT; i++) port_cover_hits[i] = 0;
}

void port_cover_save(unsigned long* buf) {
  for (int i = 0; i < PORT_COVER_COUNT; i++) buf[i] = port_cover_hits[i];
}

void port_cover_restore(const unsigned long* buf) {
  for (int i = 0; i < PORT_COVER_COUNT; i++) port_cover_hits[i] = buf[i];
}

#else

// The instrument is compiled out. Give the translation unit something to hold
// so linkers that dislike an empty object file stay quiet.
extern int port_cover_disabled;
int port_cover_disabled = 1;

#endif
