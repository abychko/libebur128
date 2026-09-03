/* Bitwise differential test against the unchanged scalar FIR, including
 * state across calls. Includes the implementation to test every output sample
 * and delay buffer without adding a public API or runtime backend switch. */
#include "../ebur128/ebur128.c"
#include <stdint.h>
#include <string.h>

static uint32_t random_state = 0x12345678;
static uint32_t next_random(void) {
  random_state = random_state * 1664525u + 1013904223u;
  return random_state;
}

static float sample(unsigned int pattern, size_t frame, unsigned int channel) {
  switch (pattern) {
    case 0: return frame % 29 == channel ? (channel ? -1.3f : 1.2f) : 0.0f;
    case 1: return (float) (sin(frame * (channel ? 3.13 : 2.91)) * 0.99);
    case 2: return frame & 1 ? -0.0f : 0.0f;
    case 3: return (float) ((int32_t) next_random()) * 0x1p-149f;
    case 4: return (float) ((int32_t) next_random()) * 0x1p60f;
    default: return (float) ((int32_t) next_random()) * 0x1p-30f;
  }
}

int main(void) {
  const unsigned int channels[] = {1, 2, 3, 6};
  const size_t chunks[] = {0, 1, 2, 7, 12, 24, 25, 26, 137, 4096, 8192, 1, 0};
  const size_t capacity = 8192 * 6 * 4;
  float *input = (float*) malloc(8192 * 6 * sizeof(float));
  float *scalar = (float*) malloc((capacity + 2) * sizeof(float));
  float *vector = (float*) malloc((capacity + 2) * sizeof(float));
  unsigned int ci, factor, pattern, block, c, cases = 0;
  if (!input || !scalar || !vector) return 2;
  for (ci = 0; ci < sizeof(channels) / sizeof(channels[0]); ++ci) {
    for (factor = 2; factor <= 4; factor += 2) {
      for (pattern = 0; pattern < 6; ++pattern) {
        unsigned int count = channels[ci];
        interpolator *a = interp_create(49, factor, count);
        interpolator *b = interp_create(49, factor, count);
        size_t offset = 0;
        if (!a || !b) return 2;
        for (block = 0; block < sizeof(chunks) / sizeof(chunks[0]); ++block) {
          size_t i, frames = chunks[block], samples = frames * count * factor;
          for (i = 0; i < frames; ++i) {
            for (c = 0; c < count; ++c) input[i * count + c] = sample(pattern, offset + i, c);
          }
          scalar[0] = vector[0] = 123.5f;
          scalar[samples + 1] = vector[samples + 1] = -456.5f;
          if (interp_process_scalar(a, frames, input, scalar + 1) != frames * factor ||
              interp_process(b, frames, input, vector + 1) != frames * factor ||
              memcmp(scalar, vector, (samples + 2) * sizeof(float)) ||
              vector[0] != 123.5f || vector[samples + 1] != -456.5f || a->zi != b->zi) {
            fprintf(stderr, "output mismatch channels=%u factor=%u pattern=%u block=%u\n", count, factor, pattern, block);
            return 1;
          }
          for (c = 0; c < count; ++c) {
            if (memcmp(a->z[c], b->z[c], a->delay * sizeof(float))) {
              fprintf(stderr, "delay state mismatch\n");
              return 1;
            }
          }
          offset += frames;
          ++cases;
        }
        interp_destroy(a);
        interp_destroy(b);
      }
    }
  }
  free(input); free(scalar); free(vector);
#ifdef EBUR128_NEON
  printf("NEON/scalar: %u bit-exact cases passed\n", cases);
#else
  printf("scalar fallback: %u cases passed (NEON disabled)\n", cases);
#endif
  return 0;
}
