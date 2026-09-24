// What the 65816 still ran, while the port stood in for the rest.
//
// `tools/native_share.py` ranks what is left to port, and until this existed it
// could only rank what `zamn_trace` had seen: eleven movies over ten levels,
// each run on the stock core with nothing substituted, and a call-graph closure
// to work out which of it the port would have taken. That answers the question
// for the levels the corpus reaches and says nothing about the forty-six
// records it does not, and those are the ones a play-test actually visits.
//
// So the game can keep the same books. With `--profile <dir>` every instruction
// the core executes is counted at its opcode byte, every call at its target,
// every call edge by caller and callee, and the lot is written on the way out
// in `zamn_trace`'s own formats: `profile.bin`, `zamn.cdl`, `callgraph.csv`.
// What is *not* in it is anything the port served, because under substitution
// those instructions never execute. A profile of a played session is therefore
// the residue itself, measured rather than inferred, and
// `tools/native_share.py --residue` ranks it.
//
// **The directory accumulates.** Counts already there are added to and flags
// OR'd, so one directory can hold a week of play-tests, and a session that
// visited one level adds its rows to the ones that visited others. Start a new
// directory when the port changes enough that the old residue no longer
// describes it.
//
// Harness code: it knows the ROM's layout, and nothing in `src/port/` may.

#ifndef COSIM_PROFILE_H
#define COSIM_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct CosimProfile CosimProfile;

// Counters for a ROM of `rom_size` bytes, all zero. NULL if out of memory.
CosimProfile* cosim_profile_new(uint32_t rom_size);
void cosim_profile_free(CosimProfile* p);

// One instruction executed, its opcode byte at ROM offset `off` and its
// operands in the `len - 1` bytes after it (same bank, as the CPU fetches).
void cosim_profile_exec(CosimProfile* p, uint32_t off, uint32_t pc24, int len);

// A `JSR`/`JSL` at `caller24` that arrived at `callee24`.
void cosim_profile_call(CosimProfile* p, uint32_t caller24, uint32_t callee24);

// Somewhere the CPU arrived without being called: an interrupt vector's
// target, or reset's. An entry for attribution, as the tracer marks it.
void cosim_profile_entry(CosimProfile* p, uint32_t pc24);

// Add this session into `dir`, creating it if need be. False, with a message
// on stderr, if any of the three files could not be read back or written.
bool cosim_profile_save(const CosimProfile* p, const char* dir);

#endif
