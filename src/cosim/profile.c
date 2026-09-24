#include "cosim/profile.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include "analysis/cdl.h"

// The same capacity as the tracer's: a whole-corpus run has a few thousand
// distinct edges, so this is room to spare rather than a limit anyone meets.
#define EDGE_CAP (1u << 16)

typedef struct {
  uint32_t caller, callee, count;
  bool used;
} Edge;

struct CosimProfile {
  uint32_t size;
  uint32_t* exec;
  uint32_t* call;
  Cdl cdl;
  Edge* edges;
  uint32_t edge_count;
  // Edges that found the table full. Never expected, and said so if it happens
  // rather than left to be discovered as a caller that went missing.
  uint64_t edges_dropped;
};

CosimProfile* cosim_profile_new(uint32_t rom_size) {
  CosimProfile* p = (CosimProfile*)calloc(1, sizeof *p);
  if (!p) return NULL;
  p->size = rom_size;
  p->exec = (uint32_t*)calloc(rom_size, sizeof *p->exec);
  p->call = (uint32_t*)calloc(rom_size, sizeof *p->call);
  p->edges = (Edge*)calloc(EDGE_CAP, sizeof *p->edges);
  if (!p->exec || !p->call || !p->edges || !cdl_alloc(&p->cdl, rom_size)) {
    cosim_profile_free(p);
    return NULL;
  }
  return p;
}

void cosim_profile_free(CosimProfile* p) {
  if (!p) return;
  free(p->exec);
  free(p->call);
  free(p->edges);
  cdl_free(&p->cdl);
  free(p);
}

void cosim_profile_exec(CosimProfile* p, uint32_t off, uint32_t pc24, int len) {
  p->exec[off]++;
  p->cdl.flags[off] |= CDL_CODE;
  for (int i = 1; i < len; i++) {
    uint32_t o;
    if (snes_to_rom((pc24 & 0xff0000) | ((pc24 + i) & 0xffff), p->size, &o))
      p->cdl.flags[o] |= CDL_OPERAND;
  }
}

static void add_edge(CosimProfile* p, uint32_t caller, uint32_t callee, uint32_t n) {
  uint32_t h = (caller * 2654435761u) ^ (callee * 40503u);
  uint32_t i = h & (EDGE_CAP - 1);
  for (uint32_t probe = 0; probe < EDGE_CAP; probe++) {
    Edge* e = &p->edges[i];
    if (!e->used) {
      e->used = true;
      e->caller = caller;
      e->callee = callee;
      e->count = n;
      p->edge_count++;
      return;
    }
    if (e->caller == caller && e->callee == callee) {
      e->count += n;
      return;
    }
    i = (i + 1) & (EDGE_CAP - 1);
  }
  p->edges_dropped++;
}

void cosim_profile_call(CosimProfile* p, uint32_t caller24, uint32_t callee24) {
  uint32_t off;
  if (snes_to_rom(callee24, p->size, &off)) {
    p->cdl.flags[off] |= CDL_SUB;
    p->call[off]++;
  }
  add_edge(p, caller24, callee24, 1);
}

void cosim_profile_entry(CosimProfile* p, uint32_t pc24) {
  uint32_t off;
  if (snes_to_rom(pc24, p->size, &off)) p->cdl.flags[off] |= CDL_SUB;
}

// ---------------------------------------------------------------------------
// Saving, on top of whatever the directory already holds
// ---------------------------------------------------------------------------

static void join(char* out, size_t cap, const char* dir, const char* name) {
  snprintf(out, cap, "%s/%s", dir, name);
}

// The profile already in the directory, added into ours. Absent is fine; a
// file that is there and is not a ZPRF of the same size is not, because adding
// counts from a different ROM would be worse than refusing.
static bool merge_profile(uint32_t* exec, uint32_t* call, uint32_t size,
                          const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return true;
  char magic[4];
  uint32_t hdr[2];
  bool ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "ZPRF", 4) &&
            fread(hdr, 4, 2, f) == 2 && hdr[0] == 1 && hdr[1] == size;
  uint32_t* tmp = ok ? (uint32_t*)malloc((size_t)size * 4) : NULL;
  for (int pass = 0; ok && pass < 2; pass++) {
    uint32_t* dst = pass ? call : exec;
    ok = tmp && fread(tmp, 4, size, f) == size;
    for (uint32_t i = 0; ok && i < size; i++) dst[i] += tmp[i];
  }
  free(tmp);
  fclose(f);
  if (!ok) fprintf(stderr, "error: '%s' is not a profile of this ROM\n", path);
  return ok;
}

static bool merge_cdl(uint8_t* flags, uint32_t size, const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return true;
  fclose(f);
  Cdl old;
  bool ok = cdl_load(&old, path) && old.size == size;
  if (ok)
    for (uint32_t i = 0; i < size; i++) flags[i] |= old.flags[i];
  if (old.flags) cdl_free(&old);
  if (!ok) fprintf(stderr, "error: '%s' is not a code log of this ROM\n", path);
  return ok;
}

static void merge_edges(CosimProfile* p, const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) return;
  char line[128];
  while (fgets(line, sizeof line, f)) {
    unsigned cb, ca, eb, ea, n;
    if (sscanf(line, "$%2X:%4X,$%2X:%4X,%u", &cb, &ca, &eb, &ea, &n) == 5)
      add_edge(p, (cb << 16) | ca, (eb << 16) | ea, n);
  }
  fclose(f);
}

bool cosim_profile_save(const CosimProfile* src, const char* dir) {
#ifdef _WIN32
  if (_mkdir(dir) != 0 && errno != EEXIST) {
#else
  if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
#endif
    fprintf(stderr, "error: cannot create '%s'\n", dir);
    return false;
  }

  // Merged into a copy, so that saving twice in a session cannot count the
  // session twice: what is in memory stays this session's alone.
  CosimProfile* p = cosim_profile_new(src->size);
  if (!p) return false;
  memcpy(p->exec, src->exec, (size_t)src->size * 4);
  memcpy(p->call, src->call, (size_t)src->size * 4);
  memcpy(p->cdl.flags, src->cdl.flags, src->size);
  for (uint32_t i = 0; i < EDGE_CAP; i++)
    if (src->edges[i].used)
      add_edge(p, src->edges[i].caller, src->edges[i].callee, src->edges[i].count);

  char prof[512], cdl[512], graph[512];
  join(prof, sizeof prof, dir, "profile.bin");
  join(cdl, sizeof cdl, dir, "zamn.cdl");
  join(graph, sizeof graph, dir, "callgraph.csv");

  bool ok = merge_profile(p->exec, p->call, p->size, prof) &&
            merge_cdl(p->cdl.flags, p->size, cdl);
  if (ok) merge_edges(p, graph);

  if (ok) {
    FILE* f = fopen(prof, "wb");
    const uint32_t hdr[2] = {1u, p->size};
    ok = f && fwrite("ZPRF", 1, 4, f) == 4 && fwrite(hdr, 4, 2, f) == 2 &&
         fwrite(p->exec, 4, p->size, f) == p->size &&
         fwrite(p->call, 4, p->size, f) == p->size;
    if (f) fclose(f);
    if (!ok) fprintf(stderr, "error: cannot write '%s'\n", prof);
  }
  if (ok && !(ok = cdl_save(&p->cdl, cdl)))
    fprintf(stderr, "error: cannot write '%s'\n", cdl);
  if (ok) {
    FILE* f = fopen(graph, "w");
    ok = f != NULL;
    if (f) {
      fprintf(f, "caller,callee,count\n");
      for (uint32_t i = 0; i < EDGE_CAP; i++) {
        const Edge* e = &p->edges[i];
        if (e->used)
          fprintf(f, "$%02X:%04X,$%02X:%04X,%u\n", e->caller >> 16,
                  e->caller & 0xffff, e->callee >> 16, e->callee & 0xffff, e->count);
      }
      fclose(f);
    } else {
      fprintf(stderr, "error: cannot write '%s'\n", graph);
    }
  }
  if (p->edges_dropped)
    fprintf(stderr, "warning: %llu call edges did not fit in the profile's table\n",
            (unsigned long long)p->edges_dropped);
  cosim_profile_free(p);
  return ok;
}
