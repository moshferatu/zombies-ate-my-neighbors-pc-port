// How evenly frames actually reach the screen.
//
// "Runs at 60 fps" and "looks smooth" are different claims, and only the first
// one is easy to check: 600 frames in ten seconds is 60 fps however unevenly
// those 600 arrived. A loop that emits three frames back to back and then
// stalls for 40 ms averages a flawless 60 and looks to the eye like it is
// running at 23 — with the game speed, the music and the input latency all
// still correct, which is exactly why the mean is the one number that cannot
// tell you anything is wrong.
//
// So this records the interval between one frame reaching the screen and the
// next, and reports the *distribution*. A perfectly paced run is a spike at the
// frame period and nothing anywhere else; every other shape is a defect, and
// the shape says which one:
//
//   * a second spike at some multiple of the period — frames being missed
//     wholesale, usually because something upstream blocks in chunks;
//   * a smear either side of the period — jitter, usually a sleep with worse
//     granularity than the thing it is trying to time;
//   * a spike *below* the period — frames emitted faster than they can be
//     shown, which is the burst half of a burst-and-stall cadence.
//
// It is a header of static inline functions with no SDL and no clock of its
// own: the caller passes milliseconds in, from whatever timer it trusts. That
// keeps it testable and keeps the measurement out of the thing being measured.

#ifndef ZAMN_PACE_H
#define ZAMN_PACE_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Half a millisecond is finer than the eye and finer than any frame period this
// will ever see, and 160 of them covers a stall five frames long. Anything
// worse than that is off the end of the scale and lands in the last bucket,
// where `max` still reports it exactly.
#define PACE_BUCKET_MS 0.5
#define PACE_BUCKETS   160

typedef struct {
  long n;
  double sum, min, max;
  long bucket[PACE_BUCKETS];
} PaceHist;

static inline void pace_reset(PaceHist* h) {
  memset(h, 0, sizeof *h);
  h->min = 1e30;
}

static inline void pace_add(PaceHist* h, double ms) {
  if (ms < 0.0) return;  // a clock that went backwards is not a measurement
  h->n++;
  h->sum += ms;
  if (ms < h->min) h->min = ms;
  if (ms > h->max) h->max = ms;
  int b = (int)(ms / PACE_BUCKET_MS);
  if (b >= PACE_BUCKETS) b = PACE_BUCKETS - 1;
  h->bucket[b]++;
}

static inline double pace_mean(const PaceHist* h) {
  return h->n ? h->sum / h->n : 0.0;
}

// Percentiles come from the buckets rather than from a kept list of samples, so
// they are quantised to PACE_BUCKET_MS and no run needs memory proportional to
// its length. `min` and `max` are exact, and they are the two that matter for
// spotting a stall.
static inline double pace_pct(const PaceHist* h, double p) {
  if (h->n <= 0) return 0.0;
  long want = (long)(p * (double)h->n);
  if (want >= h->n) want = h->n - 1;
  long seen = 0;
  for (int i = 0; i < PACE_BUCKETS; i++) {
    seen += h->bucket[i];
    if (seen > want) return (i + 0.5) * PACE_BUCKET_MS;
  }
  return h->max;
}

// The share of frames that arrived within `tol` of `target`. This is the single
// number to watch across a pacing change: 100% is an even cadence, and the mean
// staying at 16.6 ms while this figure is 40% is precisely the failure the mean
// cannot see.
static inline double pace_within(const PaceHist* h, double target, double tol) {
  if (h->n <= 0) return 0.0;
  long in = 0;
  for (int i = 0; i < PACE_BUCKETS; i++) {
    const double centre = (i + 0.5) * PACE_BUCKET_MS;
    if (centre >= target - tol && centre <= target + tol) in += h->bucket[i];
  }
  return (double)in / (double)h->n;
}

static inline void pace_line(const PaceHist* h, const char* label) {
  if (h->n <= 0) return;
  printf("  %-8s mean %6.2f   min %6.2f  p50 %6.2f  p90 %6.2f  p99 %6.2f"
         "  max %6.2f ms\n",
         label, pace_mean(h), h->min, pace_pct(h, 0.50), pace_pct(h, 0.90),
         pace_pct(h, 0.99), h->max);
}

// The distribution, on caller-supplied coarse edges. A frame interval and an
// audio backlog want completely different scales — one is interesting to a
// millisecond around 16.6, the other only to ten around 50 — and a shared set
// of edges would flatten one of them into a single bar.
static inline void pace_shape_on(const PaceHist* h, const double* edge,
                                 int edges, double target_ms,
                                 const char* target_note) {
  long count[24];
  memset(count, 0, sizeof count);
  for (int i = 0; i < PACE_BUCKETS; i++) {
    if (!h->bucket[i]) continue;
    const double centre = (i + 0.5) * PACE_BUCKET_MS;
    int row = edges;  // the overflow row, for everything past the last edge
    for (int e = 0; e < edges; e++) {
      if (centre < edge[e]) { row = e; break; }
    }
    count[row > 0 ? row - 1 : 0] += h->bucket[i];
  }
  long peak = 1;
  for (int r = 0; r < edges; r++) if (count[r] > peak) peak = count[r];

  for (int r = 0; r < edges; r++) {
    if (!count[r]) continue;
    char lo[16], hi[16];
    snprintf(lo, sizeof lo, "%.1f", edge[r]);
    if (r + 1 < edges) snprintf(hi, sizeof hi, "%.1f", edge[r + 1]);
    else               snprintf(hi, sizeof hi, "%s", "  up");
    const int bar = (int)(40.0 * (double)count[r] / (double)peak);
    // Never round a non-empty row down to nothing: a handful of 40 ms stalls in
    // a run of thousands is the whole finding, and an empty bar would hide it.
    const int width = bar > 0 ? bar : 1;
    const bool on_target = target_ms >= edge[r] &&
                           (r + 1 >= edges || target_ms < edge[r + 1]);
    printf("  %6s-%-6s %7ld  %.*s%s\n", lo, hi, count[r], width,
           "########################################",
           on_target && target_note ? target_note : "");
  }
}

// The two scales worth having: one around a frame period, one around an audio
// backlog measured in whole frames of sound.
static inline void pace_shape(const PaceHist* h, double target_ms) {
  static const double edge[] = {0.0,  2.0,  4.0,  8.0,  12.0, 14.0,
                                15.5, 16.5, 17.5, 19.0, 22.0, 26.0,
                                33.0, 42.0, 60.0};
  pace_shape_on(h, edge, (int)(sizeof edge / sizeof *edge), target_ms,
                "   <- the frame period");
}

static inline void pace_shape_audio(const PaceHist* h, double target_ms) {
  static const double edge[] = {0.0,  5.0,  10.0, 17.0, 25.0,  33.0, 42.0,
                                50.0, 58.0, 67.0, 83.0, 100.0, 133.0};
  pace_shape_on(h, edge, (int)(sizeof edge / sizeof *edge), target_ms,
                "   <- what rate control aims for");
}

// ---------------------------------------------------------------------------
// Choosing a frame period, and holding to it.
// ---------------------------------------------------------------------------
//
// The SNES does not run at 60 Hz. NTSC is 60.0988 frames a second and PAL is
// 50.007, and neither is a rate any monitor offers — so *something* has to give
// and the only choice is what. Three clocks want to be the master:
//
//   * the display, which shows a frame at its own refresh whatever we do;
//   * the audio device, which consumes 48000 samples a second regardless;
//   * the console's own timing, which is what the game was written against.
//
// Picking the display is what makes motion smooth, because a frame that misses
// a vblank is shown late no matter how correct its timestamp was. So the
// display wins, and the other two are corrected *to* it: the frame period comes
// from the refresh rate when the refresh rate is a near-multiple of the console
// rate, and the audio is resampled by a fraction of a percent to match. That is
// what emulator frontends mean by dynamic rate control, and it is why they look
// smoother than a loop that gates on an audio queue.
//
// The alternative — holding the console's exact rate and letting frames land
// where they may — is the right choice for a recording and the wrong one for
// something being played, because 0.16% of pitch is inaudible and a frame shown
// one refresh late is not invisible.

#define PACE_FPS_NTSC 60.0988
#define PACE_FPS_PAL  50.007

// The period to aim for, in ms, given the display's refresh rate. Falls back to
// the console's own rate when the panel is not a sensible multiple of it — a
// 50 Hz mode must not drag an NTSC game to 50 fps, and 144 Hz (2.4 console
// frames) has no whole-number relationship to lock onto.
static inline double pace_period_ms(int refresh_hz, double content_hz) {
  const double content_ms = 1000.0 / content_hz;
  if (refresh_hz <= 0 || content_hz <= 0.0) return content_ms;
  const int k = (int)((double)refresh_hz / content_hz + 0.5);
  if (k < 1) return content_ms;
  const double locked_ms = 1000.0 * (double)k / (double)refresh_hz;
  if (locked_ms < content_ms * 0.98 || locked_ms > content_ms * 1.02)
    return content_ms;
  return locked_ms;
}

typedef struct {
  double period;  // ms
  double next;    // the deadline, on the caller's own monotonic ms clock
  bool started;
} Pacer;

static inline void pacer_init(Pacer* p, double period_ms) {
  p->period = period_ms;
  p->next = 0.0;
  p->started = false;
}

// The moment the next frame should be released. Advancing by a fixed step from
// the *previous deadline* rather than from "now" is the whole trick: measuring
// each wait from when the last one happened to end folds every overshoot into
// the next period, and a millisecond of scheduler noise per frame is a
// millisecond of jitter per frame forever.
static inline double pacer_next(Pacer* p, double now_ms) {
  if (!p->started) {
    p->started = true;
    p->next = now_ms + p->period;
    return p->next;
  }
  p->next += p->period;
  // More than a whole period late means time was lost that cannot be got back —
  // a window drag, a stall, a machine that went to sleep. Catching up would
  // emit a burst of frames as fast as they can be produced, which is precisely
  // the artifact this file exists to prevent, so write the loss off instead.
  if (now_ms > p->next + p->period) p->next = now_ms + p->period;
  return p->next;
}

// How many samples to ask the APU for this frame, to hold the audio queue at
// `target_bytes` without the queue ever becoming the video clock.
//
// The console produces a frame's worth of sound in 1/60.0988 s and the device
// eats 800 samples in 1/60 s; the difference is small but it never stops, so
// left alone the queue drains or grows without limit and the run ends in either
// silence or seconds of latency. Nudging the resample ratio by a fraction of a
// percent absorbs it — and the clamp is what keeps it inaudible, because a
// correction large enough to hear is a correction that has stopped being a fix
// and started being a pitch bend.
static inline int pace_audio_samples(int base, long queued_bytes,
                                     long target_bytes, int bytes_per_sample,
                                     int max_adj) {
  if (bytes_per_sample <= 0) return base;
  const long err = (queued_bytes - target_bytes) / bytes_per_sample;
  // Spread the correction over ~32 frames so it is a drift and not a step.
  long adj = -err / 32;
  if (adj > max_adj) adj = max_adj;
  if (adj < -max_adj) adj = -max_adj;
  const int n = base + (int)adj;
  return n < 1 ? 1 : n;
}

// Everything above, for one run.
static inline void pace_report(const PaceHist* interval, const PaceHist* wait,
                               const PaceHist* emulate, const PaceHist* draw,
                               const PaceHist* audio, double target_ms,
                               double audio_target_ms, bool was_paced) {
  if (interval->n <= 0) return;
  printf("\nFrame cadence over %ld intervals (frame period %.3f ms):\n",
         interval->n, target_ms);
  if (!was_paced) {
    printf("  ...but this run was not paced, so the numbers below describe how\n"
           "  fast frames could be produced and not how evenly they arrived.\n");
  }
  pace_line(interval, "arrival");
  pace_line(wait, "waiting");
  pace_line(emulate, "emulate");
  pace_line(draw, "drawing");
  // Queued audio, in milliseconds of sound rather than bytes. This is the
  // evidence that rate control is working: video is no longer paced off this
  // number, so nothing stops it drifting except the correction, and a `min`
  // near zero or a `max` climbing toward the ceiling is that correction failing.
  pace_line(audio, "audioQ");
  printf("  within 1 ms of the period: %.1f%%   (100%% is a perfectly even"
         " cadence)\n",
         100.0 * pace_within(interval, target_ms, 1.0));
  pace_shape(interval, target_ms);
  if (audio->n > 0) {
    printf("  audio backlog:\n");
    pace_shape_audio(audio, audio_target_ms);
  }
}

#endif
