#include "assets/lzss.h"

#include <stdbool.h>
#include <string.h>

void lzss_ring_init(LzssRing* ring) {
  memset(ring->bytes, 0, sizeof ring->bytes);
}

LzssResult lzss_decompress(const uint8_t* src, uint32_t src_avail,
                           uint8_t* dst, uint32_t dst_cap, LzssRing* ring) {
  LzssResult res = {LZSS_OK, 0, 0};

  if (src_avail < 2) {
    res.status = LZSS_ERR_TRUNCATED;
    return res;
  }

  // $80:CD32-CD45. `LDA #$2020 / STA $7E6F00` then an MVN of $0FED+1 bytes
  // from $6F00 to $6F01 propagates the space across the window — but only as
  // far as $7E:7EEE, leaving the 17 bytes above the initial write position
  // untouched. Okumura's original fills exactly the same range, so this is
  // faithful rather than a bug; see the header for why we keep it.
  memset(ring->bytes, 0x20, LZSS_RING_START + 1);

  // $80:CD46-CD53. The stream heads with the number of bytes that follow it.
  uint32_t remaining = (uint32_t)(src[0] | ((uint32_t)src[1] << 8));
  uint32_t pos = 2;
  if (remaining > src_avail - 2) {
    res.status = LZSS_ERR_TRUNCATED;
    return res;
  }

  uint16_t r = LZSS_RING_START;
  uint32_t flags = 0;
  int bits = 0;

  for (;;) {
    // $80:CD56. Out of flag bits: fetch the next flag byte. Running out of
    // input here is the normal end of the stream, not an error.
    if (bits == 0) {
      if (remaining == 0) break;
      remaining--;
      flags = src[pos++];
      bits = 8;
    }
    bits--;
    bool literal = (flags & 1) != 0;
    flags >>= 1;

    if (literal) {
      // $80:CD65. Copy one byte straight through, and into the window.
      if (remaining == 0) break;
      remaining--;
      uint8_t b = src[pos++];
      if (res.written >= dst_cap) {
        res.status = LZSS_ERR_OVERFLOW;
        return res;
      }
      dst[res.written++] = b;
      ring->bytes[r] = b;
      r = (uint16_t)((r + 1) & (LZSS_RING_SIZE - 1));
    } else {
      // $80:CD82. A match token: 12-bit window position, 4-bit length.
      if (remaining < 2) break;
      remaining -= 2;
      uint8_t b0 = src[pos++];
      uint8_t b1 = src[pos++];
      uint16_t match = (uint16_t)(b0 | ((uint16_t)(b1 & 0xf0) << 4));
      int len = (b1 & 0x0f) + 3;

      // $80:CDA8. Byte at a time through the window, so a match may overlap
      // the bytes it is itself producing.
      for (int i = 0; i < len; i++) {
        uint8_t b = ring->bytes[match];
        ring->bytes[r] = b;
        if (res.written >= dst_cap) {
          res.status = LZSS_ERR_OVERFLOW;
          return res;
        }
        dst[res.written++] = b;
        r = (uint16_t)((r + 1) & (LZSS_RING_SIZE - 1));
        match = (uint16_t)((match + 1) & (LZSS_RING_SIZE - 1));
      }
    }
  }

  res.read = pos;
  return res;
}
