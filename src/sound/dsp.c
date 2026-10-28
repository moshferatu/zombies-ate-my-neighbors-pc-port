#include "sound/dsp.h"

#include <string.h>

// The envelope and the noise step on a tick when the counter, plus an offset,
// is a multiple of the rate's period. Rate 0 never steps; 31 steps every tick.
static const uint16_t rate_period[32] = {
    0,  2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256,
    192, 160, 128,  96,   80,   64,  48,  40,  32,  24,  20,
    16,  12,  10,   8,    6,    5,   4,   3,   2,   1,
};
static const uint16_t rate_offset[32] = {
    0, 0,    1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536,
    0, 1040, 536,  0,   1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0,
};

// The chip's interpolation: four decoded samples weighed by a bell curve,
// indexed by how far between them the pitch counter is.
static const int16_t gauss[512] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3,
    3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9,
    9, 10, 10, 10, 11, 11, 11, 12, 12, 13, 13, 14,
    14, 15, 15, 15, 16, 16, 17, 17, 18, 19, 19, 20,
    20, 21, 21, 22, 23, 23, 24, 24, 25, 26, 27, 27,
    28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 36, 36,
    37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 58, 59, 60, 61,
    62, 64, 65, 66, 67, 69, 70, 71, 73, 74, 76, 77,
    78, 80, 81, 83, 84, 86, 87, 89, 90, 92, 94, 95,
    97, 99, 100, 102, 104, 106, 107, 109, 111, 113, 115, 117,
    118, 120, 122, 124, 126, 128, 130, 132, 134, 137, 139, 141,
    143, 145, 147, 150, 152, 154, 156, 159, 161, 163, 166, 168,
    171, 173, 175, 178, 180, 183, 186, 188, 191, 193, 196, 199,
    201, 204, 207, 210, 212, 215, 218, 221, 224, 227, 230, 233,
    236, 239, 242, 245, 248, 251, 254, 257, 260, 263, 267, 270,
    273, 276, 280, 283, 286, 290, 293, 297, 300, 304, 307, 311,
    314, 318, 321, 325, 328, 332, 336, 339, 343, 347, 351, 354,
    358, 362, 366, 370, 374, 378, 381, 385, 389, 393, 397, 401,
    405, 410, 414, 418, 422, 426, 430, 434, 439, 443, 447, 451,
    456, 460, 464, 469, 473, 477, 482, 486, 491, 495, 499, 504,
    508, 513, 517, 522, 527, 531, 536, 540, 545, 550, 554, 559,
    563, 568, 573, 577, 582, 587, 592, 596, 601, 606, 611, 615,
    620, 625, 630, 635, 640, 644, 649, 654, 659, 664, 669, 674,
    678, 683, 688, 693, 698, 703, 708, 713, 718, 723, 728, 732,
    737, 742, 747, 752, 757, 762, 767, 772, 777, 782, 787, 792,
    797, 802, 806, 811, 816, 821, 826, 831, 836, 841, 846, 851,
    855, 860, 865, 870, 875, 880, 884, 889, 894, 899, 904, 908,
    913, 918, 923, 927, 932, 937, 941, 946, 951, 955, 960, 965,
    969, 974, 978, 983, 988, 992, 997, 1001, 1005, 1010, 1014, 1019,
    1023, 1027, 1032, 1036, 1040, 1045, 1049, 1053, 1057, 1061, 1066, 1070,
    1074, 1078, 1082, 1086, 1090, 1094, 1098, 1102, 1106, 1109, 1113, 1117,
    1121, 1125, 1128, 1132, 1136, 1139, 1143, 1146, 1150, 1153, 1157, 1160,
    1164, 1167, 1170, 1174, 1177, 1180, 1183, 1186, 1190, 1193, 1196, 1199,
    1202, 1205, 1207, 1210, 1213, 1216, 1219, 1221, 1224, 1227, 1229, 1232,
    1234, 1237, 1239, 1241, 1244, 1246, 1248, 1251, 1253, 1255, 1257, 1259,
    1261, 1263, 1265, 1267, 1269, 1270, 1272, 1274, 1275, 1277, 1279, 1280,
    1282, 1283, 1284, 1286, 1287, 1288, 1290, 1291, 1292, 1293, 1294, 1295,
    1296, 1297, 1297, 1298, 1299, 1300, 1300, 1301, 1302, 1302, 1303, 1303,
    1303, 1304, 1304, 1304, 1304, 1304, 1305, 1305,
};

static int clamp16(int v) { return v < -0x8000 ? -0x8000 : v > 0x7FFF ? 0x7FFF : v; }
// Keeps the low 16 bits as a signed value: the chip's adders overflow there.
static int wrap16(int v) { return (int16_t)(v & 0xFFFF); }

static bool rate_due(const SoundDsp* d, int rate) {
  if (rate == 0) return false;
  return (d->counter + rate_offset[rate]) % rate_period[rate] == 0;
}

static uint16_t ram16(const SoundDsp* d, uint16_t adr) {
  return (uint16_t)(d->ram[adr] | d->ram[(uint16_t)(adr + 1)] << 8);
}

void sound_dsp_reset(SoundDsp* d, uint8_t* ram) {
  memset(d, 0, sizeof *d);
  d->ram = ram;
  d->reg[DSP_ENDX] = 0xFF;
  d->even_tick = true;
  d->mute = true;
  d->reset = true;
  d->noise_value = 0x4000;
}

uint8_t sound_dsp_read(const SoundDsp* d, uint8_t adr) { return d->reg[adr & 0x7F]; }

void sound_dsp_write(SoundDsp* d, uint8_t adr, uint8_t val) {
  DspVoice* v = &d->voice[adr >> 4];
  switch (adr & 0x0F) {
    case DSP_VOL_L: v->vol_l = (int8_t)val; break;
    case DSP_VOL_R: v->vol_r = (int8_t)val; break;
    case DSP_PITCH_L: v->pitch = (uint16_t)((v->pitch & 0x3F00) | val); break;
    case DSP_PITCH_H: v->pitch = (uint16_t)(((v->pitch & 0xFF) | val << 8) & 0x3FFF); break;
    case DSP_SRCN: v->srcn = val; break;
    case DSP_ADSR1:
      v->attack_rate = (uint8_t)((val & 0x0F) * 2 + 1);
      v->decay_rate = (uint8_t)((val >> 4 & 7) * 2 + 16);
      v->use_gain = !(val & 0x80);
      break;
    case DSP_ADSR2:
      v->sustain_rate = val & 0x1F;
      v->sustain_level = val >> 5;
      break;
    case DSP_GAIN:
      v->direct_gain = !(val & 0x80);
      v->gain_mode = val >> 5 & 3;
      v->gain_rate = val & 0x1F;
      v->gain_value = (uint16_t)((val & 0x7F) * 16);
      v->gain_sustain_level = val >> 5;
      break;
    case DSP_FIR: d->fir[adr >> 4] = (int8_t)val; break;
    default: break;
  }
  // The registers that are the chip's, not a voice's. Each bit of the four
  // per-voice masks is one voice's.
  switch (adr) {
    case DSP_MVOL_L: d->main_vol_l = (int8_t)val; break;
    case DSP_MVOL_R: d->main_vol_r = (int8_t)val; break;
    case DSP_EVOL_L: d->echo_vol_l = (int8_t)val; break;
    case DSP_EVOL_R: d->echo_vol_r = (int8_t)val; break;
    case DSP_EFB: d->feedback = (int8_t)val; break;
    case DSP_KON: for (int i = 0; i < 8; i++) d->voice[i].key_on = val >> i & 1; break;
    case DSP_KOFF: for (int i = 0; i < 8; i++) d->voice[i].key_off = val >> i & 1; break;
    case DSP_PMON: for (int i = 0; i < 8; i++) d->voice[i].pitch_mod = val >> i & 1; break;
    case DSP_NON: for (int i = 0; i < 8; i++) d->voice[i].noise = val >> i & 1; break;
    case DSP_EON: for (int i = 0; i < 8; i++) d->voice[i].echo = val >> i & 1; break;
    case DSP_FLG:
      d->reset = val & 0x80;
      d->mute = val & 0x40;
      d->echo_writes = !(val & 0x20);
      d->noise_rate = val & 0x1F;
      break;
    case DSP_ENDX: val = 0; break;  // any write clears it
    case DSP_DIR: d->dir = (uint16_t)(val << 8); break;
    case DSP_ESA: d->echo_start = (uint16_t)(val << 8); break;
    case DSP_EDL: d->echo_delay = (uint16_t)((val & 0x0F) * 2048); break;
    default: break;
  }
  d->reg[adr] = val;
}

// Four more samples of the voice's BRR block, into its ring. A block is a
// header and eight bytes of two 4-bit samples each: the header gives the
// shift and which of four filters adds back the samples before.
static void decode_brr(SoundDsp* d, DspVoice* v) {
  const int shift = v->header >> 4;
  const int filter = v->header >> 2 & 3;
  const int at = v->decoded_at;
  int prev = v->decoded[at == 0 ? 11 : at - 1] >> 1;
  int prev2 = v->decoded[at == 0 ? 10 : at - 2] >> 1;
  for (int i = 0; i < 4; i++) {
    const uint8_t byte = d->ram[(uint16_t)(v->block + v->block_byte + i / 2)];
    int s = i & 1 ? byte & 0x0F : byte >> 4;
    if (s > 7) s -= 16;
    // Shifts past 12 leave only the sign.
    s = shift <= 12 ? (s << shift) >> 1 : (s >> 3) << 12;
    switch (filter) {
      case 1: s += prev + (-prev >> 4); break;
      case 2: s += 2 * prev + ((3 * -prev) >> 5) - prev2 + (prev2 >> 4); break;
      case 3: s += 2 * prev + ((13 * -prev) >> 6) - prev2 + ((3 * prev2) >> 4); break;
    }
    // Clamped to 16 bits, then doubled into 16 bits, which drops bit 15.
    v->decoded[at + i] = (int16_t)(clamp16(s) * 2);
    prev2 = prev;
    prev = v->decoded[at + i] >> 1;
  }
  v->decoded_at = (uint8_t)(at + 4 >= 12 ? 0 : at + 4);
}

// The voice's sample at its pitch counter, between four decoded ones.
static int interpolate(const DspVoice* v) {
  const int pos = (v->pitch_counter >> 12) + v->decoded_at;
  const int frac = v->pitch_counter >> 4 & 0xFF;
  int out = (gauss[0xFF - frac] * v->decoded[pos % 12]) >> 11;
  out += (gauss[0x1FF - frac] * v->decoded[(pos + 1) % 12]) >> 11;
  out += (gauss[0x100 + frac] * v->decoded[(pos + 2) % 12]) >> 11;
  // The last product is added after the sum so far has overflowed 16 bits.
  out = wrap16(out) + ((gauss[frac] * v->decoded[(pos + 3) % 12]) >> 11);
  return clamp16(out) & ~1;
}

// One step of the voice's envelope, taken when its rate is due.
static void step_envelope(SoundDsp* d, DspVoice* v) {
  int level = v->level;
  int rate;
  if (v->phase == ENV_RELEASE) {
    rate = 31;
    level -= 8;
  } else if (!v->use_gain) {
    switch (v->phase) {
      case ENV_ATTACK:
        rate = v->attack_rate;
        level += rate == 31 ? 1024 : 32;
        break;
      case ENV_DECAY:
        rate = v->decay_rate;
        level -= ((level - 1) >> 8) + 1;
        break;
      default:
        rate = v->sustain_rate;
        level -= ((level - 1) >> 8) + 1;
        break;
    }
  } else if (v->direct_gain) {
    rate = 31;
    level = v->gain_value;
  } else {
    rate = v->gain_rate;
    switch (v->gain_mode) {
      case 0: level -= 32; break;
      case 1: level -= ((level - 1) >> 8) + 1; break;
      case 2: level += 32; break;
      default: level += v->unclamped < 0x600 ? 32 : 8; break;
    }
  }
  // Decay gives way to sustain at the sustain level, which under GAIN is
  // GAIN's own top bits.
  const int sustain = v->use_gain ? v->gain_sustain_level : v->sustain_level;
  if (v->phase == ENV_DECAY && level >> 8 == sustain) v->phase = ENV_SUSTAIN;
  v->unclamped = (uint16_t)level;
  if (level < 0 || level > 0x7FF) {
    level = level < 0 ? 0 : 0x7FF;
    if (v->phase == ENV_ATTACK) v->phase = ENV_DECAY;
  }
  if (rate_due(d, rate)) v->level = (uint16_t)level;
}

static void run_voice(SoundDsp* d, int n) {
  DspVoice* v = &d->voice[n];
  int pitch = v->pitch;
  if (n > 0 && v->pitch_mod) pitch += ((d->voice[n - 1].out >> 5) * pitch) >> 10;

  // The directory entry is the sample's start, then where it loops to. A
  // voice keyed on is pointed at the start; one playing loops to the other.
  v->header = d->ram[v->block];
  const uint16_t entry = (uint16_t)(d->dir + 4 * v->srcn + (v->start_delay ? 0 : 2));
  const uint16_t sample_at = ram16(d, entry);

  // Five ticks pass between key-on and sound.
  if (v->start_delay > 0) {
    if (v->start_delay == 5) {
      v->block = sample_at;
      v->block_byte = 1;
      v->decoded_at = 0;
      v->header = 0;
      d->reg[DSP_ENDX] &= (uint8_t)~(1 << n);
    }
    v->level = 0;
    v->start_delay--;
    v->pitch_counter = v->start_delay > 0 && v->start_delay < 4 ? 0x4000 : 0;
    pitch = 0;
  }

  int s = v->noise ? wrap16(d->noise_value * 2) : interpolate(v);
  s = ((s * v->level) >> 11) & ~1;

  // A block that ends without looping releases the voice at once, and so does
  // the chip's soft reset.
  if (d->reset || (v->header & 3) == 1) {
    v->phase = ENV_RELEASE;
    v->level = 0;
  }
  if (d->even_tick) {
    if (v->key_off) v->phase = ENV_RELEASE;
    if (v->key_on) {
      v->start_delay = 5;
      v->phase = ENV_ATTACK;
      v->key_on = false;
    }
  }
  if (v->start_delay == 0) step_envelope(d, v);

  // Past the fourth sample, the next four are decoded. After the block's
  // eighth byte comes the next block, or the loop if the header says it ends.
  if (v->pitch_counter >= 0x4000) {
    decode_brr(d, v);
    if (v->block_byte >= 7) {
      if (v->header & 1) {
        v->block = sample_at;
        d->reg[DSP_ENDX] |= (uint8_t)(1 << n);
      } else {
        v->block = (uint16_t)(v->block + 9);
      }
      v->block_byte = 1;
    } else {
      v->block_byte += 2;
    }
  }
  int counter = (v->pitch_counter & 0x3FFF) + pitch;
  v->pitch_counter = (uint16_t)(counter > 0x7FFF ? 0x7FFF : counter);

  d->reg[n << 4 | DSP_ENVX] = (uint8_t)(v->level >> 4);
  d->reg[n << 4 | DSP_OUTX] = (uint8_t)(s >> 8);
  v->out = (int16_t)s;
  if (d->voice_mute & (1 << n)) return;
  const int l = (s * v->vol_l) >> 7, r = (s * v->vol_r) >> 7;
  d->out_l = (int16_t)clamp16(d->out_l + l);
  d->out_r = (int16_t)clamp16(d->out_r + r);
  if (v->echo) {
    d->echo_l = (int16_t)clamp16(d->echo_l + l);
    d->echo_r = (int16_t)clamp16(d->echo_r + r);
  }
}

// The echo: read the delay line, filter it, mix it in under the main volume,
// and write the voices fed to it, plus feedback, back where it was read.
static void run_echo(SoundDsp* d) {
  d->fir_at = (d->fir_at + 1) & 7;
  const uint16_t at = (uint16_t)(d->echo_start + d->echo_at);
  d->fir_l[d->fir_at] = (int16_t)((int16_t)ram16(d, at) >> 1);
  d->fir_r[d->fir_at] = (int16_t)((int16_t)ram16(d, (uint16_t)(at + 2)) >> 1);

  int sum_l = 0, sum_r = 0;
  for (int i = 0; i < 8; i++) {
    const int k = (d->fir_at + i + 1) & 7;  // oldest first
    sum_l += (d->fir_l[k] * d->fir[i]) >> 6;
    sum_r += (d->fir_r[k] * d->fir[i]) >> 6;
    // The sum overflows 16 bits before the last tap is added.
    if (i == 6) { sum_l = wrap16(sum_l); sum_r = wrap16(sum_r); }
  }
  sum_l = clamp16(sum_l) & ~1;
  sum_r = clamp16(sum_r) & ~1;

  d->out_l = (int16_t)clamp16(((d->out_l * d->main_vol_l) >> 7) + ((sum_l * d->echo_vol_l) >> 7));
  d->out_r = (int16_t)clamp16(((d->out_r * d->main_vol_r) >> 7) + ((sum_r * d->echo_vol_r) >> 7));

  const int back_l = clamp16(d->echo_l + wrap16((sum_l * d->feedback) >> 7)) & ~1;
  const int back_r = clamp16(d->echo_r + wrap16((sum_r * d->feedback) >> 7)) & ~1;
  if (d->echo_writes) {
    d->ram[at] = (uint8_t)back_l;
    d->ram[(uint16_t)(at + 1)] = (uint8_t)(back_l >> 8);
    d->ram[(uint16_t)(at + 2)] = (uint8_t)back_r;
    d->ram[(uint16_t)(at + 3)] = (uint8_t)(back_r >> 8);
  }

  // A new EDL takes effect only when the line comes round to its start.
  if (d->echo_at == 0) d->echo_length = d->echo_delay;
  d->echo_at = (uint16_t)(d->echo_at + 4);
  if (d->echo_at >= d->echo_length) d->echo_at = 0;
}

void sound_dsp_sample(SoundDsp* d) {
  d->out_l = d->out_r = 0;
  d->echo_l = d->echo_r = 0;
  for (int n = 0; n < 8; n++) run_voice(d, n);
  run_echo(d);
  d->counter = d->counter == 0 ? 30720 : d->counter - 1;
  if (rate_due(d, d->noise_rate)) {
    const int bit = (d->noise_value ^ d->noise_value >> 1) & 1;
    d->noise_value = (int16_t)((d->noise_value >> 1 & 0x3FFF) | bit << 14);
  }
  d->even_tick = !d->even_tick;
  if (d->mute) d->out_l = d->out_r = 0;
  d->ring[(d->ring_at & 0x3FF) * 2] = d->out_l;
  d->ring[(d->ring_at & 0x3FF) * 2 + 1] = d->out_r;
  d->ring_at++;
}

void sound_dsp_get_samples(const SoundDsp* d, int16_t* out, int count) {
  // The arithmetic is LakeSnes's to the bit, float and double both, so a
  // frame's audio is the same whichever DSP made it.
  const float per_frame = 534.0f;
  const double step = per_frame / count;
  double at = d->ring_at - per_frame;
  for (int i = 0; i < count; i++) {
    const int k = (int)at & 0x3FF;
    out[i * 2] = d->ring[k * 2];
    out[i * 2 + 1] = d->ring[k * 2 + 1];
    at += step;
  }
}
