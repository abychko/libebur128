/* Optional CPU-time microbenchmark. No timing threshold or external fixtures. */
#include "../ebur128/ebur128.c"
#include <time.h>

int main(void) {
  float input[4096 * 2], output[4096 * 2 * 4];
  unsigned int i, run, factor, order;
  for (i = 0; i < 4096 * 2; ++i) input[i] = (float) sin(i * 0.097);
  for (run = 0; run < 4; ++run) {
    for (factor = 2; factor <= 4; factor += 2) {
      for (order = 0; order < 2; ++order) {
        unsigned int neon = (run + order) % 2;
        unsigned int rate = factor == 2 ? 96000 : 48000;
        size_t remaining = (size_t) (rate * 191.5);
        interpolator *p = interp_create(49, factor, 2);
        clock_t start;
        if (!p) return 2;
        start = clock();
        while (remaining) {
          size_t frames = remaining < 4096 ? remaining : 4096;
          if (neon) interp_process(p, frames, input, output);
          else interp_process_scalar(p, frames, input, output);
          remaining -= frames;
        }
        printf("run=%u rate=%u dispatch=%u cpu_seconds=%.6f sample=%a\n",
               run, rate, neon, (double) (clock() - start) / CLOCKS_PER_SEC,
               (double) output[0]);
        fflush(stdout);
        interp_destroy(p);
      }
    }
  }
  return 0;
}
