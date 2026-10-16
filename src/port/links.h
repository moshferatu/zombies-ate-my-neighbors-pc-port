// Calls, jumps and returns of the ROM's that stand between two ports.
//
// Where one port returns and the next instruction is a call to another, that
// instruction is all there is between them. It is the ROM's, it only moves
// control, and the harness makes it: see `CosimRoutine::link`. These are the
// ones that belong to no port's own table, named for where they are. Each
// was found by running every session and listing where the 65816 was asked
// for exactly one instruction.
//
// **A `WAI` is one too.** It moves control to the interrupt and back. The
// ones here stand where an interrupt returns to a second `WAI`, or where a
// call comes back to one, so no port leaves by them.
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
  X(808533, "$80:8533", 0x808533u) /* JSL hud_refresh, after a pause */ \
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
  X(80d9a3, "$80:D9A3", 0x80d9a3u) /* JSR $80:F36C, the monster potion */ \
  X(80ef85, "$80:EF85", 0x80ef85u) /* JSR $80:F0D7, the punch's tile */ \
  X(80efa0, "$80:EFA0", 0x80efa0u) /* JSR $80:F0D7 */ \
  X(80efaf, "$80:EFAF", 0x80efafu) /* JSR $80:F0D7 */ \
  X(80d9a6, "$80:D9A6", 0x80d9a6u) /* JSR $80:ECFD */ \
  X(80d9a9, "$80:D9A9", 0x80d9a9u) /* JSR $80:F354 */ \
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
  X(80fb15, "$80:FB15", 0x80fb15u) /* JSR $80:F354, a player out by the exit */ \
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
  X(8084d7, "$80:84D7", 0x8084d7u) /* JSL $80:866F */ \
  X(8084fc, "$80:84FC", 0x8084fcu) /* JSL $80:8A00, the game over */ \
  X(8085cf, "$80:85CF", 0x8085cfu) /* JSL $80:88A9 */ \
  X(8085dd, "$80:85DD", 0x8085ddu) /* JSL $80:C049 */ \
  X(80c049, "$80:C049", 0x80c049u) /* JSR $80:B9C7 */ \
  X(808632, "$80:8632", 0x808632u) /* JSL $80:9F9D */ \
  X(809128, "$80:9128", 0x809128u) /* JSL $80:88A9 */ \
  X(8183c3, "$81:83C3", 0x8183c3u) /* RTL */ \
  X(8181f5, "$81:81F5", 0x8181f5u) /* RTS */ \
  X(8381cb, "$83:81CB", 0x8381cbu) /* WAI */ \
  X(8087d2, "$80:87D2", 0x8087d2u) /* WAI */ \
  X(8087d3, "$80:87D3", 0x8087d3u) /* WAI */ \
  X(8087d4, "$80:87D4", 0x8087d4u) /* WAI */ \
  X(82b82a, "$82:B82A", 0x82b82au) /* WAI */ \
  X(82ae3e, "$82:AE3E", 0x82ae3eu) /* WAI */ \
  X(8093b0, "$80:93B0", 0x8093b0u) /* WAI */ \
  X(809c50, "$80:9C50", 0x809c50u) /* WAI */ \
  X(80931e, "$80:931E", 0x80931eu) /* WAI */ \
  X(808371, "$80:8371", 0x808371u) /* WAI */ \
  X(82b9b0, "$82:B9B0", 0x82b9b0u) /* WAI */ \
  X(82b230, "$82:B230", 0x82b230u) /* WAI */ \
  X(82b565, "$82:B565", 0x82b565u) /* WAI */ \
  X(82b30e, "$82:B30E", 0x82b30eu) /* WAI */ \
  X(82b2f1, "$82:B2F1", 0x82b2f1u) /* WAI */ \
  X(809796, "$80:9796", 0x809796u) /* WAI */ \
  X(828013, "$82:8013", 0x828013u) /* RTL */ \
  X(81e077, "$81:E077", 0x81e077u) /* RTS */ \
  X(8086a1, "$80:86A1", 0x8086a1u) /* RTL */ \
  X(80e1ea, "$80:E1EA", 0x80e1eau) /* JMP $E2C5 */ \
  X(83ad33, "$83:AD33", 0x83ad33u) /* JSR $83:AAB2 */ \
  X(81d8b5, "$81:D8B5", 0x81d8b5u) /* RTS */ \
  X(81c94d, "$81:C94D", 0x81c94du) /* RTS */ \
  X(82dc9f, "$82:DC9F", 0x82dc9fu) /* RTL */ \
  X(80c1f4, "$80:C1F4", 0x80c1f4u) /* RTL */ \
  /* The game over's, and the main thread's round it: see port/game_over.h */ \
  X(808a07, "$80:8A07", 0x808a07u) /* JSR $80:8A78 */ \
  X(808a78, "$80:8A78", 0x808a78u) /* WAI */ \
  X(808a79, "$80:8A79", 0x808a79u) /* JSL $82:AD44 */ \
  X(808a91, "$80:8A91", 0x808a91u) /* WAI */ \
  X(808a9c, "$80:8A9C", 0x808a9cu) /* WAI */ \
  X(808aa7, "$80:8AA7", 0x808aa7u) /* WAI */ \
  X(808ab2, "$80:8AB2", 0x808ab2u) /* WAI */ \
  X(808abd, "$80:8ABD", 0x808abdu) /* WAI */ \
  X(808a4e, "$80:8A4E", 0x808a4eu) /* BRA $8A45 */ \
  X(808a57, "$80:8A57", 0x808a57u) /* RTL */ \
  X(808500, "$80:8500", 0x808500u) /* JSL $80:CC27 */ \
  X(808508, "$80:8508", 0x808508u) /* JSL $82:BBED */ \
  X(80850c, "$80:850C", 0x80850cu) /* JSL $82:BA11 */ \
  X(808510, "$80:8510", 0x808510u) /* JSL $80:895A */ \
  X(808514, "$80:8514", 0x808514u) /* BRA $84B1 */ \
  /* The demonstration's: see port/demo_thread.h */ \
  X(809b3a, "$80:9B3A", 0x809b3au) /* JSR $80:9C4A */ \
  X(809b3d, "$80:9B3D", 0x809b3du) /* JSL $80:8618 */ \
  X(809b41, "$80:9B41", 0x809b41u) /* JSL $80:88A9 */ \
  X(809b45, "$80:9B45", 0x809b45u) /* JSL $80:8632 */ \
  X(809b49, "$80:9B49", 0x809b49u) /* JSL $80:86A2 */ \
  X(809b87, "$80:9B87", 0x809b87u) /* JSL $80:8516 */ \
  X(809b91, "$80:9B91", 0x809b91u) /* JSR $80:9C72 */ \
  X(809bad, "$80:9BAD", 0x809badu) /* JSL $80:88A9 */ \
  X(809bb1, "$80:9BB1", 0x809bb1u) /* JSL $80:895A */ \
  X(809bb5, "$80:9BB5", 0x809bb5u) /* JSL $80:8992 */ \
  X(809bb9, "$80:9BB9", 0x809bb9u) /* JSL $80:85CF */ \
  /* Single calls, returns and waits round ports, found by the survey */ \
  X(808169, "$80:8169", 0x808169u) /* JMP $80:8371 */ \
  X(80a09d, "$80:A09D", 0x80a09du) /* RTS */ \
  X(80c218, "$80:C218", 0x80c218u) /* RTL */ \
  X(80e942, "$80:E942", 0x80e942u) /* RTS */ \
  X(80ea1d, "$80:EA1D", 0x80ea1du) /* RTS */ \
  X(80ebc1, "$80:EBC1", 0x80ebc1u) /* RTS */ \
  X(80f08a, "$80:F08A", 0x80f08au) /* RTS */ \
  X(818437, "$81:8437", 0x818437u) /* JSL $81:8349 */ \
  X(818442, "$81:8442", 0x818442u) /* JSL $81:8377 */ \
  X(81f25c, "$81:F25C", 0x81f25cu) /* RTS */ \
  X(81f8fb, "$81:F8FB", 0x81f8fbu) /* RTS */ \
  X(81faa0, "$81:FAA0", 0x81faa0u) /* RTS */ \
  X(828199, "$82:8199", 0x828199u) /* RTL */ \
  X(82bae4, "$82:BAE4", 0x82bae4u) /* WAI */ \
  X(82bae5, "$82:BAE5", 0x82bae5u) /* WAI */ \
  X(82bae6, "$82:BAE6", 0x82bae6u) /* WAI */ \
  X(82bae7, "$82:BAE7", 0x82bae7u) /* WAI */ \
  X(82bae8, "$82:BAE8", 0x82bae8u) /* WAI */ \
  X(82dc76, "$82:DC76", 0x82dc76u) /* BRA $DC9F */ \
  X(82dddd, "$82:DDDD", 0x82ddddu) /* JSL $80:C07F, the bystander's panel */ \
  X(82de02, "$82:DE02", 0x82de02u) /* WAI */ \
  X(82def6, "$82:DEF6", 0x82def6u) /* WAI, a frame of ten */ \
  X(82de07, "$82:DE07", 0x82de07u) /* WAI */ \
  X(82de82, "$82:DE82", 0x82de82u) /* RTS */ \
  X(82f5f9, "$82:F5F9", 0x82f5f9u) /* RTS */ \
  X(82f63f, "$82:F63F", 0x82f63fu) /* JSL $80:BE0C */

#endif
