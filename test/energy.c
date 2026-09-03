/* Scalar oracle copied from 08a0694 before the energy optimization.
 * Kept independent of the vector helper, including weighting and gating. */
#include "../ebur128/ebur128.c"
#include <stdint.h>
#include <string.h>
#include <time.h>

static int reference_gating_block(ebur128_state* st,
                                     size_t frames_per_block,
                                     double* optional_output) {
  size_t i, c;
  double sum = 0.0;
  double channel_sum;
  for (c = 0; c < st->channels; ++c) {
    if (st->d->channel_map[c] == EBUR128_UNUSED) {
      continue;
    }
    channel_sum = 0.0;
    if (st->d->audio_data_index < frames_per_block * st->channels) {
      for (i = 0; i < st->d->audio_data_index / st->channels; ++i) {
        channel_sum += st->d->audio_data[i * st->channels + c] *
                       st->d->audio_data[i * st->channels + c];
      }
      for (i = st->d->audio_data_frames -
               (frames_per_block - st->d->audio_data_index / st->channels);
           i < st->d->audio_data_frames; ++i) {
        channel_sum += st->d->audio_data[i * st->channels + c] *
                       st->d->audio_data[i * st->channels + c];
      }
    } else {
      for (i = st->d->audio_data_index / st->channels - frames_per_block;
           i < st->d->audio_data_index / st->channels; ++i) {
        channel_sum += st->d->audio_data[i * st->channels + c] *
                       st->d->audio_data[i * st->channels + c];
      }
    }
    if (st->d->channel_map[c] == EBUR128_Mp110 ||
        st->d->channel_map[c] == EBUR128_Mm110 ||
        st->d->channel_map[c] == EBUR128_Mp060 ||
        st->d->channel_map[c] == EBUR128_Mm060 ||
        st->d->channel_map[c] == EBUR128_Mp090 ||
        st->d->channel_map[c] == EBUR128_Mm090) {
      channel_sum *= 1.41;
    } else if (st->d->channel_map[c] == EBUR128_DUAL_MONO) {
      channel_sum *= 2.0;
    }
    sum += channel_sum;
  }

  sum /= (double) frames_per_block;

  if (optional_output) {
    *optional_output = sum;
    return EBUR128_SUCCESS;
  }

  if (sum >= histogram_energy_boundaries[0]) {
    if (st->d->use_histogram) {
      ++st->d->block_energy_histogram[find_histogram_index(sum)];
    } else {
      struct ebur128_dq_entry* block;
      if (st->d->block_list_size == st->d->block_list_max) {
        block = STAILQ_FIRST(&st->d->block_list);
        STAILQ_REMOVE_HEAD(&st->d->block_list, entries);
      } else {
        block =
            (struct ebur128_dq_entry*) malloc(sizeof(struct ebur128_dq_entry));
        if (!block) {
          return EBUR128_ERROR_NOMEM;
        }
        st->d->block_list_size++;
      }
      block->z = sum;
      STAILQ_INSERT_TAIL(&st->d->block_list, block, entries);
    }
  }

  return EBUR128_SUCCESS;
}

static uint32_t rng = 19;
static double sample(unsigned int pattern, size_t i) {
  rng = rng * 1664525u + 1013904223u;
  switch (pattern) {
    case 0: return i & 1 ? -0.0 : 0.0;
    case 1: return i % 7 ? 0.0 : 1.3;
    case 2: return (int32_t) rng * 0x1p-1050;
    case 3: return (int32_t) rng * 0x1p460;
    case 4: return sqrt(histogram_energy_boundaries[0]) * (i % 3 ? 1.000000001 : 0.999999999);
    default: return (int32_t) rng * (i % 5 ? 0x1p-40 : 0x1p-20);
  }
}
static int compare(ebur128_state* a, ebur128_state* b, size_t window) {
  double x, y;
  if (reference_gating_block(a, window, &x) || ebur128_calc_gating_block(b, window, &y) ||
      memcmp(&x, &y, sizeof(double))) {
    fprintf(stderr, "energy mismatch channels=%u index=%zu window=%zu\n", a->channels, a->d->audio_data_index, window);
    return 1;
  }
  if (reference_gating_block(a, window, NULL) || ebur128_calc_gating_block(b, window, NULL)) return 1;
  if (a->d->block_list_size != b->d->block_list_size) return 1;
  if (a->d->use_histogram && memcmp(a->d->block_energy_histogram, b->d->block_energy_histogram,
                                   1000 * sizeof(*a->d->block_energy_histogram))) return 1;
  return 0;
}

static int benchmark(void) {
  unsigned int rates[] = {48000, 96000, 192000};
  unsigned int run, r, order;
  for (run = 0; run < 4; ++run) for (r = 0; r < 3; ++r) for (order = 0; order < 2; ++order) {
    unsigned int vector = (run + order) % 2, block, step = rates[r] / 10;
    ebur128_state *s = ebur128_init(2, rates[r], EBUR128_MODE_S);
    size_t i;
    double output = 0.0, checksum = 0.0;
    clock_t start;
    if (!s) return 2;
    for (i = 0; i < s->d->audio_data_frames * 2; ++i) s->d->audio_data[i] = sin(i * .097);
    start = clock();
    for (block = 3; block < 1915; ++block) {
      s->d->audio_data_index = ((size_t) (block + 1) * step % s->d->audio_data_frames) * 2;
      if (vector) ebur128_calc_gating_block(s, step * 4, &output);
      else reference_gating_block(s, step * 4, &output);
      checksum += output;
      if (block >= 29 && (block - 29) % 10 == 0) {
        if (vector) ebur128_calc_gating_block(s, step * 30, &output);
        else reference_gating_block(s, step * 30, &output);
        checksum += output;
      }
    }
    printf("run=%u rate=%u dispatch=%u cpu_seconds=%.6f checksum=%a\n",run,rates[r],vector,
           (double)(clock()-start)/CLOCKS_PER_SEC,checksum);
    fflush(stdout);ebur128_destroy(&s);
  }
  return 0;
}

int main(int argc, char** argv) {
  unsigned int layouts[] = {1,2,3,6,2,2,2,1};
  unsigned int rates[] = {48000,96000,192000,352800};
  unsigned int layout, pattern, w, e, cases = 0;
  if (argc == 2 && !strcmp(argv[1], "--benchmark")) return benchmark();
  for (layout = 0; layout < 8; ++layout) for (pattern = 0; pattern < 6; ++pattern) {
    int mode = EBUR128_MODE_I | EBUR128_MODE_S | (layout % 2 ? EBUR128_MODE_HISTOGRAM : 0);
    ebur128_state *a = ebur128_init(layouts[layout],8000,mode), *b = ebur128_init(layouts[layout],8000,mode);
    size_t windows[] = {1,7,32,127,256,257}, ends[] = {0,1,7,32,127,256,257}, i;
    if (!a || !b) return 2;
    /* Compact odd-length ring tests indexing independently of rate rounding. */
    a->d->audio_data_frames = b->d->audio_data_frames = 257;
    if (layout == 4 || layout == 5) {
      ebur128_set_channel(a, layout - 4, EBUR128_UNUSED);
      ebur128_set_channel(b, layout - 4, EBUR128_UNUSED);
    }
    if (layout == 6) {
      ebur128_set_channel(a, 0, EBUR128_Mp110); ebur128_set_channel(b, 0, EBUR128_Mp110);
      ebur128_set_channel(a, 1, EBUR128_Mm110); ebur128_set_channel(b, 1, EBUR128_Mm110);
    }
    if (layout == 7) {
      ebur128_set_channel(a, 0, EBUR128_DUAL_MONO); ebur128_set_channel(b, 0, EBUR128_DUAL_MONO);
    }
    for (i = 0; i < 257 * layouts[layout]; ++i) a->d->audio_data[i] = b->d->audio_data[i] = sample(pattern,i);
    for (w = 0; w < 6; ++w) for (e = 0; e < 7; ++e) {
      a->d->audio_data_index = b->d->audio_data_index = ends[e] * layouts[layout];
      if (compare(a,b,windows[w])) return 1;
      ++cases;
    }
    if (!a->d->use_histogram) {
      struct ebur128_dq_entry *x = STAILQ_FIRST(&a->d->block_list), *y = STAILQ_FIRST(&b->d->block_list);
      while (x && y) {
        if (memcmp(&x->z,&y->z,sizeof(double))) return 1;
        x = STAILQ_NEXT(x,entries); y = STAILQ_NEXT(y,entries);
      }
      if (x || y) return 1;
    }
    ebur128_destroy(&a);ebur128_destroy(&b);
  }
  for (layout = 0; layout < 4; ++layout) {
    unsigned int step = rates[layout]/10;
    ebur128_state *a = ebur128_init(2,rates[layout],EBUR128_MODE_I|EBUR128_MODE_S);
    ebur128_state *b = ebur128_init(2,rates[layout],EBUR128_MODE_I|EBUR128_MODE_S);
    size_t i, windows[] = {1,step*4,step*30}, ends[] = {0,1,step*4-1,step*4,step*30-1,step*30};
    if (!a || !b) return 2;
    for (i = 0; i < a->d->audio_data_frames*2; ++i) a->d->audio_data[i] = b->d->audio_data[i] = sample(5,i);
    for (w = 0; w < 3; ++w) for (e = 0; e < 6; ++e) {
      a->d->audio_data_index = b->d->audio_data_index = ends[e]*2;
      if (compare(a,b,windows[w])) return 1;
      ++cases;
    }
    ebur128_destroy(&a);ebur128_destroy(&b);
  }
  printf("Energy: %u bit-exact window/weight/gating cases passed\n",cases);
  return 0;
}
