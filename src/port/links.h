// Calls, jumps and returns of the ROM's that stand between two ports.
//
// Where one port returns and the next instruction is a call to another, that
// instruction is all there is between them. It is the ROM's, it only moves
// control, and the harness makes it: see `CosimRoutine::link`. These are the
// ones that belong to no port's own table, named for where they are. Each
// was found by running every session and listing where the 65816 was asked
// for exactly one instruction.
//
// A link says nothing about what is round it. Most of these are in routines
// nobody has ported whole, and are here so that the list of what is left to
// port is a list of code, and not of calls to code that is ported already.
//
// Port code: libc only.

#ifndef PORT_LINKS_H
#define PORT_LINKS_H

#define LINKS(X) \
  X(808504, "$80:8504", 0x808504u) /* JSL $80:866F */ \
  X(80851d, "$80:851D", 0x80851du) /* JSL $80:CC13 */ \
  X(808521, "$80:8521", 0x808521u) /* JSL $80:891A */ \
  X(808636, "$80:8636", 0x808636u) /* JSL $80:8947 */ \
  X(80866f, "$80:866F", 0x80866fu) /* JSL $80:820A */ \
  X(808673, "$80:8673", 0x808673u) /* JSL $80:8992 */ \
  X(808677, "$80:8677", 0x808677u) /* JSL $80:BDF2 */ \
  X(8086cc, "$80:86CC", 0x8086ccu) /* JSL $80:AD2B */ \
  X(809148, "$80:9148", 0x809148u) /* JSR $80:9290 */ \
  X(809156, "$80:9156", 0x809156u) /* JSL $80:9FB0 */ \
  X(80915a, "$80:915A", 0x80915au) /* JSL $80:AC7E */ \
  X(80916d, "$80:916D", 0x80916du) /* JSR $80:97AC */ \
  X(809170, "$80:9170", 0x809170u) /* JSR $80:9847 */ \
  X(80978d, "$80:978D", 0x80978du) /* JSL $80:CC27 */ \
  X(809bb1, "$80:9BB1", 0x809bb1u) /* JSL $80:895A */ \
  X(80c8f6, "$80:C8F6", 0x80c8f6u) /* JSR $80:C9A5 */ \
  X(80cdf4, "$80:CDF4", 0x80cdf4u) /* JSR $80:D13A */ \
  X(80ce16, "$80:CE16", 0x80ce16u) /* JSR $80:F327 */ \
  X(80ce8b, "$80:CE8B", 0x80ce8bu) /* JSR $80:ECFD */ \
  X(80ce8e, "$80:CE8E", 0x80ce8eu) /* JSR $80:F366 */ \
  X(80ce91, "$80:CE91", 0x80ce91u) /* JSR $80:F36C */ \
  X(80d081, "$80:D081", 0x80d081u) /* RTS */ \
  X(80d1db, "$80:D1DB", 0x80d1dbu) /* JMP $80:F382 */ \
  X(80d460, "$80:D460", 0x80d460u) /* RTS */ \
  X(80d557, "$80:D557", 0x80d557u) /* RTS */ \
  X(80d700, "$80:D700", 0x80d700u) /* RTS */ \
  X(80d9a0, "$80:D9A0", 0x80d9a0u) /* JMP $80:F382 */ \
  X(80d9a6, "$80:D9A6", 0x80d9a6u) /* JSR $80:ECFD */ \
  X(80d9e6, "$80:D9E6", 0x80d9e6u) /* JSR $80:F382 */ \
  X(80d9e9, "$80:D9E9", 0x80d9e9u) /* JMP $80:D4E9 */ \
  X(80da72, "$80:DA72", 0x80da72u) /* JSR $80:F377 */ \
  X(80da75, "$80:DA75", 0x80da75u) /* JSR $80:F382 */ \
  X(80da78, "$80:DA78", 0x80da78u) /* JMP $80:D4E9 */ \
  X(80dc6d, "$80:DC6D", 0x80dc6du) /* JMP $80:D4F4 */ \
  X(80dca1, "$80:DCA1", 0x80dca1u) /* RTS */ \
  X(80dd1a, "$80:DD1A", 0x80dd1au) /* JSR $80:F366 */ \
  X(80dd9e, "$80:DD9E", 0x80dd9eu) /* JSR $80:F377 */ \
  X(80ddc4, "$80:DDC4", 0x80ddc4u) /* JSR $80:F3B4 */ \
  X(80ddd1, "$80:DDD1", 0x80ddd1u) /* JSR $80:F366 */ \
  X(80de54, "$80:DE54", 0x80de54u) /* JSR $80:F377 */ \
  X(80e12e, "$80:E12E", 0x80e12eu) /* JSR $80:F366 */ \
  X(80e2e1, "$80:E2E1", 0x80e2e1u) /* JSR $80:F382 */ \
  X(80e3de, "$80:E3DE", 0x80e3deu) /* JSR $80:F382 */ \
  X(80e3e1, "$80:E3E1", 0x80e3e1u) /* JMP $80:D4E9 */ \
  X(80f12e, "$80:F12E", 0x80f12eu) /* RTS */ \
  X(80f9ef, "$80:F9EF", 0x80f9efu) /* JSR $80:F366 */ \
  X(80f9f9, "$80:F9F9", 0x80f9f9u) /* JSR $80:F382 */ \
  X(80f9fc, "$80:F9FC", 0x80f9fcu) /* JSR $80:F377 */ \
  X(80f9ff, "$80:F9FF", 0x80f9ffu) /* JMP $80:D4F4 */ \
  X(80fb0f, "$80:FB0F", 0x80fb0fu) /* JSR $80:F377 */ \
  X(80fb12, "$80:FB12", 0x80fb12u) /* JSR $80:ECFD */ \
  X(819e13, "$81:9E13", 0x819e13u) /* JSL $80:B22A */ \
  X(819e58, "$81:9E58", 0x819e58u) /* JSL $80:B22A */ \
  X(81b2e7, "$81:B2E7", 0x81b2e7u) /* JSR $81:B377 */ \
  X(81b9f9, "$81:B9F9", 0x81b9f9u) /* JSL $81:8000 */ \
  X(81befc, "$81:BEFC", 0x81befcu) /* JSL $80:B22A */ \
  X(81c050, "$81:C050", 0x81c050u) /* JSL $80:BE0C */ \
  X(81d704, "$81:D704", 0x81d704u) /* JSL $80:9D5B */ \
  X(81e481, "$81:E481", 0x81e481u) /* JSL $80:9D5B */ \
  X(81e8b7, "$81:E8B7", 0x81e8b7u) /* JSR $81:E979 */ \
  X(81ed12, "$81:ED12", 0x81ed12u) /* RTS */ \
  X(81f9d8, "$81:F9D8", 0x81f9d8u) /* RTL */ \
  X(81fb80, "$81:FB80", 0x81fb80u) /* JSL $80:BE0C */ \
  X(82958f, "$82:958F", 0x82958fu) /* JSR $82:92D6 */ \
  X(82990f, "$82:990F", 0x82990fu) /* JSL $80:9D5B */ \
  X(82ac4b, "$82:AC4B", 0x82ac4bu) /* JSL $82:AD44 */ \
  X(82b5a0, "$82:B5A0", 0x82b5a0u) /* JSL $80:BE0C */ \
  X(82de03, "$82:DE03", 0x82de03u) /* JSL $80:BD1F */ \
  X(82e0bb, "$82:E0BB", 0x82e0bbu) /* JSL $80:BE0C */ \
  X(82e0fe, "$82:E0FE", 0x82e0feu) /* JSL $80:9D39 */ \
  X(82ea5a, "$82:EA5A", 0x82ea5au) /* JSL $80:9D39 */ \
  X(82f3b3, "$82:F3B3", 0x82f3b3u) /* JSR $82:F40B */ \
  X(82f40a, "$82:F40A", 0x82f40au) /* RTS */ \
  X(82f49e, "$82:F49E", 0x82f49eu) /* JSL $80:9D5B */ \
  X(83a1d4, "$83:A1D4", 0x83a1d4u) /* RTS */ \
  X(83b277, "$83:B277", 0x83b277u) /* JSL $80:9D5B */

#endif
