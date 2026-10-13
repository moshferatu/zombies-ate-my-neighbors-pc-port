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
  X(80863a, "$80:863A", 0x80863au) /* JSL $80:A64F */ \
  X(808648, "$80:8648", 0x808648u) /* JSL $80:C049 */ \
  X(80864c, "$80:864C", 0x80864cu) /* JSL $82:DF23 */ \
  X(808650, "$80:8650", 0x808650u) /* JSL $82:8000 */ \
  X(808654, "$80:8654", 0x808654u) /* JSL $82:F577 */ \
  X(808658, "$80:8658", 0x808658u) /* JSL $81:817E */ \
  X(808666, "$80:8666", 0x808666u) /* JSL $80:8803 */ \
  X(80866a, "$80:866A", 0x80866au) /* JSL $80:C2DA */ \
  X(80866e, "$80:866E", 0x80866eu) /* RTL */ \
  X(8086cc, "$80:86CC", 0x8086ccu) /* JSL $80:AD2B */ \
  X(80875b, "$80:875B", 0x80875bu) /* JSL $80:A079 */ \
  X(8087ff, "$80:87FF", 0x8087ffu) /* BRA $87D2 */ \
  X(80912c, "$80:912C", 0x80912cu) /* JSR $80:925D */ \
  X(80912f, "$80:912F", 0x80912fu) /* JSR $80:9BEF */ \
  X(809132, "$80:9132", 0x809132u) /* JSL $80:9FB0 */ \
  X(809140, "$80:9140", 0x809140u) /* JSL $83:8000 */ \
  X(809144, "$80:9144", 0x809144u) /* JSL $80:AC7E */ \
  X(809148, "$80:9148", 0x809148u) /* JSR $80:9290 */ \
  X(80914b, "$80:914B", 0x80914bu) /* JSR $80:92F3 */ \
  X(80914e, "$80:914E", 0x80914eu) /* JSL $80:91F7 */ \
  X(809152, "$80:9152", 0x809152u) /* JSL $80:88A9 */ \
  X(809156, "$80:9156", 0x809156u) /* JSL $80:9FB0 */ \
  X(80915a, "$80:915A", 0x80915au) /* JSL $80:AC7E */ \
  X(80915e, "$80:915E", 0x80915eu) /* JSR $80:93AC */ \
  X(809161, "$80:9161", 0x809161u) /* JSR $80:94AF */ \
  X(809164, "$80:9164", 0x809164u) /* JSR $80:96B9 */ \
  X(80916d, "$80:916D", 0x80916du) /* JSR $80:97AC */ \
  X(809170, "$80:9170", 0x809170u) /* JSR $80:9847 */ \
  X(80917e, "$80:917E", 0x80917eu) /* JSL $80:9FB0 */ \
  X(809182, "$80:9182", 0x809182u) /* JSR $80:925D */ \
  X(809185, "$80:9185", 0x809185u) /* JSR $80:9BEF */ \
  X(809188, "$80:9188", 0x809188u) /* JSL $80:AC7E */ \
  X(80918c, "$80:918C", 0x80918cu) /* BRA $915A */ \
  X(8091f7, "$80:91F7", 0x8091f7u) /* JSL $80:9FB0 */ \
  X(8091fb, "$80:91FB", 0x8091fbu) /* JSL $80:C2DA */ \
  X(809249, "$80:9249", 0x809249u) /* JSR $80:9C72 */ \
  X(80925d, "$80:925D", 0x80925du) /* JSL $82:AD44 */ \
  X(8092f2, "$80:92F2", 0x8092f2u) /* RTS */ \
  X(809343, "$80:9343", 0x809343u) /* RTS */ \
  X(8093ac, "$80:93AC", 0x8093acu) /* JSL $80:9FB0 */ \
  X(8094f5, "$80:94F5", 0x8094f5u) /* JSL $80:CC13 */ \
  X(8094f9, "$80:94F9", 0x8094f9u) /* JSR $80:9C52 */ \
  X(80953a, "$80:953A", 0x80953au) /* RTS */ \
  X(809173, "$80:9173", 0x809173u) /* JSR $80:98EA */ \
  X(80978d, "$80:978D", 0x80978du) /* JSL $80:CC27 */ \
  X(8098e3, "$80:98E3", 0x8098e3u) /* JSR $80:9A31 */ \
  X(8098e6, "$80:98E6", 0x8098e6u) /* JSR $80:9C52 */ \
  X(8098e9, "$80:98E9", 0x8098e9u) /* RTS */ \
  X(8099e5, "$80:99E5", 0x8099e5u) /* JSR $80:9C4A */ \
  X(809a51, "$80:9A51", 0x809a51u) /* RTS */ \
  X(809c51, "$80:9C51", 0x809c51u) /* RTS */ \
  X(809bb1, "$80:9BB1", 0x809bb1u) /* JSL $80:895A */ \
  X(809c62, "$80:9C62", 0x809c62u) /* RTS */ \
  X(809c7c, "$80:9C7C", 0x809c7cu) /* RTS */ \
  X(80c2da, "$80:C2DA", 0x80c2dau) /* JSL $80:C2F7 */ \
  X(80a083, "$80:A083", 0x80a083u) /* RTL */ \
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
  X(82ac1b, "$82:AC1B", 0x82ac1bu) /* JSL $80:9F9D */ \
  X(82ac4b, "$82:AC4B", 0x82ac4bu) /* JSL $82:AD44 */ \
  X(82b5a0, "$82:B5A0", 0x82b5a0u) /* JSL $80:BE0C */ \
  X(82de03, "$82:DE03", 0x82de03u) /* JSL $80:BD1F */ \
  X(82e0bb, "$82:E0BB", 0x82e0bbu) /* JSL $80:BE0C */ \
  X(82e0fe, "$82:E0FE", 0x82e0feu) /* JSL $80:9D39 */ \
  X(82ea5a, "$82:EA5A", 0x82ea5au) /* JSL $80:9D39 */ \
  X(82f3b3, "$82:F3B3", 0x82f3b3u) /* JSR $82:F40B */ \
  X(82f40a, "$82:F40A", 0x82f40au) /* RTS */ \
  X(82f49e, "$82:F49E", 0x82f49eu) /* JSL $80:9D5B */ \
  X(83823a, "$83:823A", 0x83823au) /* JSL $80:9F9D */ \
  X(83a1d4, "$83:A1D4", 0x83a1d4u) /* RTS */ \
  X(83b277, "$83:B277", 0x83b277u) /* JSL $80:9D5B */ \
  X(82afff, "$82:AFFF", 0x82afffu) /* RTL */ \
  X(82b231, "$82:B231", 0x82b231u) /* RTS */ \
  X(82b2e7, "$82:B2E7", 0x82b2e7u) /* JSR $82:B396 */ \
  X(82b304, "$82:B304", 0x82b304u) /* JSR $82:B229 */ \
  X(82b331, "$82:B331", 0x82b331u) /* BRA $B2EA */ \
  X(82b395, "$82:B395", 0x82b395u) /* RTS */ \
  X(82b3be, "$82:B3BE", 0x82b3beu) /* JSL $82:B820 */ \
  X(82b3e5, "$82:B3E5", 0x82b3e5u) /* JSL $82:B820 */ \
  X(82b4aa, "$82:B4AA", 0x82b4aau) /* JSL $82:B820 */ \
  X(82b51f, "$82:B51F", 0x82b51fu) /* JSL $82:AD44 */ \
  X(82b584, "$82:B584", 0x82b584u) /* JSL $80:BDF2 */ \
  X(82b592, "$82:B592", 0x82b592u) /* JSL $80:C049 */ \
  X(82b5e2, "$82:B5E2", 0x82b5e2u) /* RTS */ \
  X(82ba49, "$82:BA49", 0x82ba49u) /* JSR $82:B229 */ \
  X(82ba85, "$82:BA85", 0x82ba85u) /* JSL $82:AD44 */ \
  X(82d8f3, "$82:D8F3", 0x82d8f3u) /* JSR $82:D9F6 */ \
  X(82d9ea, "$82:D9EA", 0x82d9eau) /* RTS */ \
  X(82d9f5, "$82:D9F5", 0x82d9f5u) /* RTS */ \
  X(82da9f, "$82:DA9F", 0x82da9fu) /* RTS */ \
  X(8396fb, "$83:96FB", 0x8396fbu) /* BRA $9700 */ \
  X(8397d8, "$83:97D8", 0x8397d8u) /* BRA $97DD */ \
  X(839cc9, "$83:9CC9", 0x839cc9u) /* BRA $9CCE */ \
  X(839e77, "$83:9E77", 0x839e77u) /* BRA $9E7C */ \
  X(83a04f, "$83:A04F", 0x83a04fu) /* BRA $A054 */ \
  X(8399f7, "$83:99F7", 0x8399f7u) /* BRA $9A23 */ \
  X(839b4b, "$83:9B4B", 0x839b4bu) /* BRA $9B77 */ \
  X(83a19a, "$83:A19A", 0x83a19au) /* JSL $80:C863 */ \
  X(83a1b3, "$83:A1B3", 0x83a1b3u) /* RTS */ \
  X(83a1c2, "$83:A1C2", 0x83a1c2u) /* JSL $80:C863 */ \
  X(83a1dd, "$83:A1DD", 0x83a1ddu) /* JSL $80:C863 */ \
  X(83a235, "$83:A235", 0x83a235u) /* JSL $81:8349 */ \
  X(83a249, "$83:A249", 0x83a249u) /* JSL $81:8377 */ \
  X(83a263, "$83:A263", 0x83a263u) /* RTS */ \
  X(83a333, "$83:A333", 0x83a333u) /* RTS */ \
  X(83a0d2, "$83:A0D2", 0x83a0d2u) /* JSL $80:C863 */ \
  X(839d91, "$83:9D91", 0x839d91u) /* BRA $9DC9 */ \
  X(839dac, "$83:9DAC", 0x839dacu) /* JSR $83:A18F */ \
  X(839daf, "$83:9DAF", 0x839dafu) /* JMP $9DC9 */ \
  X(839ed6, "$83:9ED6", 0x839ed6u) /* JSR $83:9F99 */ \
  X(839f99, "$83:9F99", 0x839f99u) /* JSL $80:BE0C */ \
  X(839f60, "$83:9F60", 0x839f60u) /* BRA $9F68 */ \
  X(839f65, "$83:9F65", 0x839f65u) /* JSR $83:A18F */ \
  X(839f83, "$83:9F83", 0x839f83u) /* JSR $83:A21D */ \
  X(81d210, "$81:D210", 0x81d210u) /* JSL $80:BE0C */ \
  X(81d27b, "$81:D27B", 0x81d27bu) /* RTS */ \
  X(82f354, "$82:F354", 0x82f354u) /* JSL $80:BE0C */ \
  X(82f3b6, "$82:F3B6", 0x82f3b6u) /* RTS */ \
  X(82f476, "$82:F476", 0x82f476u) /* RTS */ \
  X(819b3a, "$81:9B3A", 0x819b3au) /* RTS */ \
  X(8084b1, "$80:84B1", 0x8084b1u) /* JSL $80:85CF */ \
  X(8084b5, "$80:84B5", 0x8084b5u) /* JSL $80:9126 */ \
  X(8084b9, "$80:84B9", 0x8084b9u) /* JSL $80:88A9 */ \
  X(8084bd, "$80:84BD", 0x8084bdu) /* JSL $80:8618 */ \
  X(8084c1, "$80:84C1", 0x8084c1u) /* JSL $80:885B */ \
  X(8084c5, "$80:84C5", 0x8084c5u) /* JSL $80:8849 */ \
  X(8084c9, "$80:84C9", 0x8084c9u) /* JSL $80:8632 */ \
  X(8084cd, "$80:84CD", 0x8084cdu) /* JSL $80:86A2 */ \
  X(8084d1, "$80:84D1", 0x8084d1u) /* JSL $80:8516 */ \
  X(8085cf, "$80:85CF", 0x8085cfu) /* JSL $80:88A9 */ \
  X(8085dd, "$80:85DD", 0x8085ddu) /* JSL $80:C049 */ \
  X(80c049, "$80:C049", 0x80c049u) /* JSR $80:B9C7 */ \
  X(808632, "$80:8632", 0x808632u) /* JSL $80:9F9D */ \
  X(809128, "$80:9128", 0x809128u) /* JSL $80:88A9 */ \
  X(8183c3, "$81:83C3", 0x8183c3u) /* RTL */ \
  X(8181f5, "$81:81F5", 0x8181f5u) /* RTS */

#endif
