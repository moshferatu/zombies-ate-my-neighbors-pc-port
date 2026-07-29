// zamn_disasm — CDL-driven 65816 disassembler.
//
// A blind linear disassembly of a SNES ROM is worthless: code and data are
// interleaved, and immediate operands change size with the M/X flags. This tool
// instead consumes the Code/Data Log produced by zamn_trace, so it only
// disassembles bytes the CPU actually executed, at the register widths it
// actually executed them with, and dumps everything else as data.
//
// Usage:
//   zamn_disasm <rom.sfc> <zamn.cdl> [options]
//     -o, --out <file>       output listing (default: stdout)
//     -b, --bank <hex>       disassemble one 32K LoROM bank, e.g. -b 00
//         --from <hex>       start SNES address, e.g. --from 0080AE
//         --to <hex>         end SNES address (exclusive)
//     -s, --symbols <file>   extra symbol names ("$00:80AE  reset" per line)
//         --code-only        skip runs of pure data
//         --force            decode the whole range as code, traced or not
//
// `--force` is for the routine no input has ever reached. Phase 3's work list is
// made of those now — a handler the port declines to, an entry in a jump table
// nothing has dispatched to — and the CDL, which is the whole reason this tool
// is trustworthy, says nothing about a byte that never executed. So `--force`
// drops the CDL's opinion about *what is code* while keeping everything else:
// register widths come from the CDL where the byte was traced, and otherwise
// from a running M/X state this scan updates on every `REP`/`SEP` it decodes.
// That is exactly right down a straight-line routine and a guess across a data
// table, which is why it is a flag rather than the default. Lines it decoded on
// its own guess are marked `~` in the byte column.

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analysis/cdl.h"
#include "analysis/w65816.h"

#define MAX_SYMBOLS 8192

typedef struct {
  uint32_t addr;
  char name[48];
} Symbol;

static Symbol g_symbols[MAX_SYMBOLS];
static int g_symbol_count;

// ---------------------------------------------------------------------------
// Symbols
// ---------------------------------------------------------------------------

static const char* hardware_name(uint32_t addr) {
  static const struct { uint16_t adr; const char* name; } kRegs[] = {
      {0x2100, "INIDISP"},  {0x2101, "OBSEL"},    {0x2102, "OAMADDL"},
      {0x2103, "OAMADDH"},  {0x2104, "OAMDATA"},  {0x2105, "BGMODE"},
      {0x2106, "MOSAIC"},   {0x2107, "BG1SC"},    {0x2108, "BG2SC"},
      {0x2109, "BG3SC"},    {0x210a, "BG4SC"},    {0x210b, "BG12NBA"},
      {0x210c, "BG34NBA"},  {0x210d, "BG1HOFS"},  {0x210e, "BG1VOFS"},
      {0x210f, "BG2HOFS"},  {0x2110, "BG2VOFS"},  {0x2111, "BG3HOFS"},
      {0x2112, "BG3VOFS"},  {0x2113, "BG4HOFS"},  {0x2114, "BG4VOFS"},
      {0x2115, "VMAIN"},    {0x2116, "VMADDL"},   {0x2117, "VMADDH"},
      {0x2118, "VMDATAL"},  {0x2119, "VMDATAH"},  {0x211a, "M7SEL"},
      {0x211b, "M7A"},      {0x211c, "M7B"},      {0x211d, "M7C"},
      {0x211e, "M7D"},      {0x211f, "M7X"},      {0x2120, "M7Y"},
      {0x2121, "CGADD"},    {0x2122, "CGDATA"},   {0x2123, "W12SEL"},
      {0x2124, "W34SEL"},   {0x2125, "WOBJSEL"},  {0x2126, "WH0"},
      {0x2127, "WH1"},      {0x2128, "WH2"},      {0x2129, "WH3"},
      {0x212a, "WBGLOG"},   {0x212b, "WOBJLOG"},  {0x212c, "TM"},
      {0x212d, "TS"},       {0x212e, "TMW"},      {0x212f, "TSW"},
      {0x2130, "CGWSEL"},   {0x2131, "CGADSUB"},  {0x2132, "COLDATA"},
      {0x2133, "SETINI"},   {0x2134, "MPYL"},     {0x2135, "MPYM"},
      {0x2136, "MPYH"},     {0x2137, "SLHV"},     {0x2138, "OAMDATAREAD"},
      {0x2139, "VMDATALREAD"}, {0x213a, "VMDATAHREAD"}, {0x213b, "CGDATAREAD"},
      {0x213c, "OPHCT"},    {0x213d, "OPVCT"},    {0x213e, "STAT77"},
      {0x213f, "STAT78"},   {0x2140, "APUIO0"},   {0x2141, "APUIO1"},
      {0x2142, "APUIO2"},   {0x2143, "APUIO3"},   {0x2180, "WMDATA"},
      {0x2181, "WMADDL"},   {0x2182, "WMADDM"},   {0x2183, "WMADDH"},
      {0x4200, "NMITIMEN"}, {0x4201, "WRIO"},     {0x4202, "WRMPYA"},
      {0x4203, "WRMPYB"},   {0x4204, "WRDIVL"},   {0x4205, "WRDIVH"},
      {0x4206, "WRDIVB"},   {0x4207, "HTIMEL"},   {0x4208, "HTIMEH"},
      {0x4209, "VTIMEL"},   {0x420a, "VTIMEH"},   {0x420b, "MDMAEN"},
      {0x420c, "HDMAEN"},   {0x420d, "MEMSEL"},   {0x4210, "RDNMI"},
      {0x4211, "TIMEUP"},   {0x4212, "HVBJOY"},   {0x4213, "RDIO"},
      {0x4214, "RDDIVL"},   {0x4215, "RDDIVH"},   {0x4216, "RDMPYL"},
      {0x4217, "RDMPYH"},   {0x4218, "JOY1L"},    {0x4219, "JOY1H"},
      {0x421a, "JOY2L"},    {0x421b, "JOY2H"},    {0x421c, "JOY3L"},
      {0x421d, "JOY3H"},    {0x421e, "JOY4L"},    {0x421f, "JOY4H"},
  };
  uint16_t low = addr & 0xffff;
  for (size_t i = 0; i < sizeof kRegs / sizeof kRegs[0]; i++)
    if (kRegs[i].adr == low) return kRegs[i].name;

  if (low >= 0x4300 && low < 0x4380) {
    static const char* kDma[16] = {"DMAPn", "BBADn", "A1TnL", "A1TnH", "A1Bn",
                                   "DASnL", "DASnH", "DASBn", "A2AnL", "A2AnH",
                                   "NLTRn", "UNUSEDn", "?", "?", "?", "?"};
    static char buf[24];
    snprintf(buf, sizeof buf, "%s(ch%d)", kDma[low & 0xf], (low >> 4) & 7);
    return buf;
  }
  return NULL;
}

static const char* symbol_name(uint32_t addr) {
  for (int i = 0; i < g_symbol_count; i++)
    if (g_symbols[i].addr == addr) return g_symbols[i].name;
  return NULL;
}

static bool load_symbols(const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) return false;
  char line[256];
  while (fgets(line, sizeof line, f) && g_symbol_count < MAX_SYMBOLS) {
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || *p == '\0') continue;
    unsigned bank = 0, adr = 0;
    char name[48];
    // Accept "$00:80AE name", "00:80AE name", or "$0080AE name".
    if (sscanf(p, "$%2x:%4x %47s", &bank, &adr, name) == 3 ||
        sscanf(p, "%2x:%4x %47s", &bank, &adr, name) == 3) {
      g_symbols[g_symbol_count].addr = (bank << 16) | adr;
    } else if (sscanf(p, "$%6x %47s", &adr, name) == 2) {
      g_symbols[g_symbol_count].addr = adr;
    } else {
      continue;
    }
    snprintf(g_symbols[g_symbol_count].name, sizeof g_symbols[0].name, "%s", name);
    g_symbol_count++;
  }
  fclose(f);
  return true;
}

// ---------------------------------------------------------------------------
// Listing
// ---------------------------------------------------------------------------

// Annotate an instruction's operand with a hardware-register or user symbol
// name, when the operand plainly refers to one.
static void annotate(char* out, size_t out_size, const uint8_t* bytes, uint32_t pc) {
  out[0] = '\0';
  const OpInfo* op = &w65816_ops[bytes[0]];
  uint32_t word = (uint32_t)bytes[1] | ((uint32_t)bytes[2] << 8);
  uint32_t lng = word | ((uint32_t)bytes[3] << 16);
  const char* name = NULL;

  switch (op->mode) {
    case AM_ABS:
    case AM_ABX:
    case AM_ABY:
      // The data bank is not knowable statically, but the register windows are
      // mirrored into every low bank, so a hit here is almost always real.
      if ((word >= 0x2100 && word < 0x2200) || (word >= 0x4200 && word < 0x4400))
        name = hardware_name(word);
      if (!name) name = symbol_name(word);
      if (!name && op->flow != FLOW_NONE) name = symbol_name((pc & 0xff0000) | word);
      break;
    case AM_ABL:
    case AM_ALX:
      name = symbol_name(lng);
      if (!name) name = hardware_name(lng);
      break;
    case AM_DP:
    case AM_DPX:
    case AM_DPY:
    case AM_IDP:
    case AM_IDX:
    case AM_IDY:
    case AM_IDL:
    case AM_IDLY:
      name = symbol_name(bytes[1]);
      break;
    case AM_REL:
    case AM_RELL: {
      uint32_t target;
      if (w65816_static_target(bytes, pc, &target)) name = symbol_name(target);
      break;
    }
    default:
      break;
  }
  if (name) snprintf(out, out_size, "; %s", name);
}

static void emit_label(FILE* out, uint32_t addr, uint8_t flags) {
  const char* sym = symbol_name(addr);
  if (sym) {
    fprintf(out, "\n%s:\n", sym);
    return;
  }
  if (flags & CDL_SUB) fprintf(out, "\nsub_%02X%04X:\n", addr >> 16, addr & 0xffff);
  else if (flags & CDL_JUMP) fprintf(out, "\nloc_%02X%04X:\n", addr >> 16, addr & 0xffff);
}

static void flush_data(FILE* out, const uint8_t* rom, const Cdl* cdl,
                       uint32_t start_off, uint32_t end_off) {
  for (uint32_t off = start_off; off < end_off; off += 16) {
    uint32_t n = end_off - off;
    if (n > 16) n = 16;
    uint32_t addr = rom_to_snes(off);
    fprintf(out, "$%02X:%04X  .db ", addr >> 16, addr & 0xffff);
    for (uint32_t i = 0; i < n; i++)
      fprintf(out, "%s$%02X", i ? "," : "", rom[off + i]);
    for (uint32_t i = n; i < 16; i++) fputs("    ", out);

    bool touched = false;
    for (uint32_t i = 0; i < n; i++)
      if (cdl->flags[off + i] & CDL_DATA) touched = true;
    fprintf(out, "  ; %s |", touched ? "read" : "  ? ");
    for (uint32_t i = 0; i < n; i++) {
      uint8_t c = rom[off + i];
      fputc(isprint(c) ? c : '.', out);
    }
    fputs("|\n", out);
  }
}

static void disassemble(FILE* out, const uint8_t* rom, const Cdl* cdl,
                        uint32_t from, uint32_t to, bool code_only, bool force) {
  uint32_t from_off, to_off;
  if (!snes_to_rom(from, cdl->size, &from_off)) {
    fprintf(stderr, "error: $%06X is not a ROM address\n", from);
    return;
  }
  to_off = from_off + (to - from);
  if (to_off > cdl->size) to_off = cdl->size;

  fprintf(out, "; ZAMN disassembly, generated by zamn_disasm\n");
  fprintf(out, "; Range $%02X:%04X-$%02X:%04X  (ROM $%05X-$%05X)\n", from >> 16,
          from & 0xffff, (to - 1) >> 16, (to - 1) & 0xffff, from_off, to_off - 1);
  if (force) {
    fprintf(out, "; --force: every byte in range is decoded as code. Lines marked\n");
    fprintf(out, "; '~' were never traced, and their M/X widths are this scan's own\n");
    fprintf(out, "; running guess from the REP/SEP it has decoded (16-bit at entry).\n");
  } else {
    fprintf(out, "; Only bytes the tracer saw executed are disassembled; the rest\n");
    fprintf(out, "; are dumped as .db ('read' = seen as data, '?' = never touched).\n");
  }

  uint32_t off = from_off;
  uint32_t data_start = 0;
  bool in_data = false;
  // The forced scan's own idea of the register widths. The game runs `REP #$30`
  // almost everywhere and every ported routine so far is entered 16-bit, so that
  // is the entry assumption; `REP`/`SEP` below correct it as they are decoded.
  bool guess_m8 = false, guess_x8 = false;

  while (off < to_off) {
    uint8_t flags = cdl->flags[off];
    if (!(flags & CDL_CODE) && !force) {
      if (!in_data) { data_start = off; in_data = true; }
      off++;
      continue;
    }
    if (in_data) {
      if (!code_only) flush_data(out, rom, cdl, data_start, off);
      in_data = false;
    }

    uint32_t addr = rom_to_snes(off);
    emit_label(out, addr, flags);

    bool traced = (flags & CDL_CODE) != 0;
    bool mf = traced ? !(flags & CDL_M16) : guess_m8;
    bool xf = traced ? !(flags & CDL_X16) : guess_x8;
    uint8_t bytes[4] = {0};
    for (int i = 0; i < 4 && off + (uint32_t)i < cdl->size; i++) bytes[i] = rom[off + i];

    char text[64], note[64];
    int len = w65816_disasm(text, sizeof text, bytes, addr, mf, xf);
    annotate(note, sizeof note, bytes, addr);

    char raw[20] = {0};
    int w = 0;
    if (!traced) w += snprintf(raw, sizeof raw, "~");
    for (int i = 0; i < len && i < 4; i++)
      w += snprintf(raw + w, sizeof raw - (size_t)w, "%02X ", bytes[i]);

    fprintf(out, "$%02X:%04X  %-13s%-24s%s%s\n", addr >> 16, addr & 0xffff, raw, text,
            note, (flags & CDL_CONFLICT) ? "  ; !! M/X width varies here" : "");
    // `REP`/`SEP` #$xx: bit 5 is M, bit 4 is X. REP clears the flag (16-bit),
    // SEP sets it (8-bit), which is the whole of what the guess tracks.
    if (bytes[0] == 0xC2 || bytes[0] == 0xE2) {
      bool set = bytes[0] == 0xE2;
      if (bytes[1] & 0x20) guess_m8 = set;
      if (bytes[1] & 0x10) guess_x8 = set;
    }
    off += (uint32_t)len;
  }
  if (in_data && !code_only) flush_data(out, rom, cdl, data_start, to_off);
}

// ---------------------------------------------------------------------------

static uint8_t* read_file(const char* path, int* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); return NULL; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* buf = (uint8_t*)malloc((size_t)(len > 0 ? len : 1));
  if (!buf || len <= 0 || fread(buf, 1, (size_t)len, f) != (size_t)len) {
    fclose(f); free(buf); return NULL;
  }
  fclose(f);
  *out_len = (int)len;
  return buf;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr,
            "usage: %s <rom.sfc> <zamn.cdl> [-o out] [-b bank] "
            "[--from hex] [--to hex] [-s symbols] [--code-only]\n",
            argv[0]);
    return 2;
  }
  const char* rom_path = argv[1];
  const char* cdl_path = argv[2];
  const char* out_path = NULL;
  const char* sym_path = NULL;
  uint32_t from = 0x008000, to = 0x010000;
  bool code_only = false, range_set = false, force = false;

  for (int i = 3; i < argc; i++) {
    const char* a = argv[i];
    bool has_next = i + 1 < argc;
    if ((!strcmp(a, "-o") || !strcmp(a, "--out")) && has_next) out_path = argv[++i];
    else if ((!strcmp(a, "-s") || !strcmp(a, "--symbols")) && has_next) sym_path = argv[++i];
    else if ((!strcmp(a, "-b") || !strcmp(a, "--bank")) && has_next) {
      unsigned bank = (unsigned)strtoul(argv[++i], NULL, 16);
      from = (bank << 16) | 0x8000;
      to = from + 0x8000;
      range_set = true;
    } else if (!strcmp(a, "--from") && has_next) {
      from = (uint32_t)strtoul(argv[++i], NULL, 16);
      range_set = true;
    } else if (!strcmp(a, "--to") && has_next) {
      to = (uint32_t)strtoul(argv[++i], NULL, 16);
      range_set = true;
    } else if (!strcmp(a, "--code-only")) code_only = true;
    else if (!strcmp(a, "--force")) force = true;
    else { fprintf(stderr, "error: unknown option '%s'\n", a); return 2; }
  }
  (void)range_set;

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) return 1;

  Cdl cdl;
  if (!cdl_load(&cdl, cdl_path)) {
    fprintf(stderr, "error: cannot load CDL '%s'\n", cdl_path);
    return 1;
  }
  if (cdl.size != (uint32_t)rom_len) {
    fprintf(stderr, "error: CDL is for a %u-byte ROM, this ROM is %d bytes\n",
            cdl.size, rom_len);
    return 1;
  }
  if (sym_path && !load_symbols(sym_path))
    fprintf(stderr, "warning: cannot read symbols '%s'\n", sym_path);

  FILE* out = out_path ? fopen(out_path, "w") : stdout;
  if (!out) { fprintf(stderr, "error: cannot write '%s'\n", out_path); return 1; }
  disassemble(out, rom, &cdl, from, to, code_only, force);
  if (out != stdout) fclose(out);

  cdl_free(&cdl);
  free(rom);
  return 0;
}
