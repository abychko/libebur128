/* Independent scalar Float32 instantiation: compare filtered doubles and
 * recursive state, not only rounded loudness results. No public test API. */
#include "../ebur128/ebur128.c"
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef float reference_float;
EBUR128_FILTER(reference_float, -1.0f, 1.0f, 0)
EBUR128_ADD_FRAMES(reference_float)

static uint32_t rng = 17;
static float input_sample(size_t i, unsigned int pattern) {
  rng = rng * 1664525u + 1013904223u;
  switch (pattern) {
    case 0: return i % 31 == 0 ? 1.3f : 0.0f;
    case 1: return (float) sin(i * 3.13);
    case 2: return i & 1 ? -0.0f : 0.0f;
    case 3: return (int32_t) rng * 0x1p-149f;
    case 4: return (int32_t) rng * 0x1p60f;
    default: return (int32_t) rng * 0x1p-30f;
  }
}

static int same(double a, double b) { return memcmp(&a, &b, sizeof(double)) == 0; }

static int benchmark(void) {
  float pcm[4096 * 2];
  unsigned int rates[] = {48000, 96000, 192000};
  unsigned int i, run, r, order;
  for (i = 0; i < 8192; ++i) pcm[i] = input_sample(i, 5);
  for (run = 0; run < 4; ++run) for (r = 0; r < 3; ++r) for (order = 0; order < 2; ++order) {
    unsigned int vector = (run + order) % 2;
    size_t remaining = (size_t) (191.5 * rates[r]);
    ebur128_state *s = ebur128_init(2, rates[r], EBUR128_MODE_S);
    clock_t start;
    if (!s) return 2;
    start = clock();
    while (remaining) {
      size_t frames = remaining < 4096 ? remaining : 4096;
      if (vector) ebur128_filter_float(s, pcm, frames);
      else ebur128_filter_reference_float(s, pcm, frames);
      remaining -= frames;
    }
    printf("run=%u rate=%u dispatch=%u cpu_seconds=%.6f sample=%a\n", run, rates[r], vector,
           (double) (clock() - start) / CLOCKS_PER_SEC, s->d->audio_data[0]);
    fflush(stdout); ebur128_destroy(&s);
  }
  return 0;
}

int main(int argc, char** argv) {
  const unsigned int rates[] = {8000, 11025, 44100, 48000, 96000, 192000, 352800};
  const unsigned int layouts[] = {1, 2, 3, 6, 2, 2};
  const size_t chunks[] = {0, 1, 2, 17, 137, 2048, 3, 4096, 1, 0};
  float pcm[4096 * 6];
  unsigned int r, layout, pattern, block, c, cases = 0;
  if (argc == 2 && !strcmp(argv[1], "--benchmark")) return benchmark();
  for (r = 0; r < 7; ++r) for (layout = 0; layout < 6; ++layout) for (pattern = 0; pattern < 6; ++pattern) {
    unsigned int channels = layouts[layout];
    ebur128_state *a = ebur128_init(channels, rates[r], EBUR128_MODE_S);
    ebur128_state *b = ebur128_init(channels, rates[r], EBUR128_MODE_S);
    if (!a || !b) return 2;
    if (layout >= 4) {
      ebur128_set_channel(a, layout - 4, EBUR128_UNUSED);
      ebur128_set_channel(b, layout - 4, EBUR128_UNUSED);
    }
    /* Manual FTZ happens at block end, and does not flush v[0]. */
    if (pattern == 3) for (c = 0; c < channels; ++c) for (block = 0; block < 5; ++block) {
      a->d->v[c][block] = b->d->v[c][block] = DBL_MIN * 0.25;
    }
    for (block = 0; block < 10; ++block) {
      size_t i, frames = chunks[block];
      size_t offset = (block % 2) * channels * 7;
      if (pattern == 2 && block == 1) for (c = 0; c < channels; ++c) {
        for (i = 0; i < 5; ++i) a->d->v[c][i] = b->d->v[c][i] = DBL_MIN * 0.25;
      }
      for (i = 0; i < frames * channels; ++i) pcm[i] = input_sample(i, pattern);
      a->d->audio_data_index = b->d->audio_data_index = offset;
      ebur128_filter_reference_float(a, pcm, frames);
      ebur128_filter_float(b, pcm, frames);
      if (memcmp(a->d->audio_data, b->d->audio_data, (offset + frames * channels) * sizeof(double)) ||
          memcmp(a->d->v, b->d->v, channels * sizeof(filter_state))) {
        fprintf(stderr, "K-filter mismatch rate=%u layout=%u pattern=%u block=%u\n", rates[r], layout, pattern, block);
        return 1;
      }
      ++cases;
    }
    ebur128_destroy(&a); ebur128_destroy(&b);
  }
  /* Public streaming API: wrap the 3 s ring and compare all metrics. */
  for (r = 0; r < 7; ++r) {
    int mode = EBUR128_MODE_I | EBUR128_MODE_LRA | EBUR128_MODE_TRUE_PEAK;
    ebur128_state *a = ebur128_init(2, rates[r], mode), *b = ebur128_init(2, rates[r], mode);
    size_t offset = 0, total = (size_t) (rates[r] * 4.13);
    double x, y;
    if (!a || !b) return 2;
    while (offset < total) {
      size_t i, n = (offset % 2) ? 137 : 4096;
      if (n > total - offset) n = total - offset;
      for (i = 0; i < n * 2; ++i) pcm[i] = input_sample(offset * 2 + i, 5);
      if (ebur128_add_frames_reference_float(a, pcm, n) || ebur128_add_frames_float(b, pcm, n)) return 2;
      if (memcmp(a->d->v, b->d->v, 2 * sizeof(filter_state))) return 1;
      offset += n;
    }
    if (memcmp(a->d->audio_data, b->d->audio_data, a->d->audio_data_frames * 2 * sizeof(double))) return 1;
#define COMPARE(metric) do { if (metric(a, &x) || metric(b, &y) || !same(x, y)) return 1; } while (0)
    COMPARE(ebur128_loudness_global);
    COMPARE(ebur128_loudness_range);
    COMPARE(ebur128_loudness_momentary);
    COMPARE(ebur128_loudness_shortterm);
#undef COMPARE
    for (c = 0; c < 2; ++c) {
      if (ebur128_true_peak(a, c, &x) || ebur128_true_peak(b, c, &y) || !same(x, y)) return 1;
    }
    ebur128_destroy(&a); ebur128_destroy(&b);
  }
  printf("K-filter: %u bit-exact block cases + 7 streaming metric cases passed\n", cases);
  return 0;
}
