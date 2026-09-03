# ARM64 stereo true-peak interpolation

Based on libebur128 1.2.6. CMake detects the target compiler's ARM64 NEON
support; `EBUR128_ENABLE_NEON` defaults to that result. Set it to `OFF` for
the unchanged scalar implementation. Setting it to `ON` on x86_64 still
uses scalar code. No public ABI, filter coefficients, gating, oversampling
factors, or K-filter changes.

The two double-precision NEON lanes process the left and right FIR channels.
The delay buffers, tap order, accumulation precision and compiler FP
contraction policy are retained. There is no extra allocation or PCM buffer.
Only stereo dispatches to the vector loop; mono and other channel counts
use the original loop. The library interpolates 4x below 96 kHz, 2x below
192 kHz, and does not interpolate at or above 192 kHz.

## Build and verify

```sh
cmake -S . -B build-neon -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DENABLE_NEON_TESTS=ON -DEBUR128_ENABLE_NEON=ON
cmake --build build-neon
ctest --test-dir build-neon --output-on-failure
./build-neon/neon-benchmark
```

Repeat in a separate build directory with `-DEBUR128_ENABLE_NEON=OFF`.
The differential executable requires neither libsndfile nor audio fixtures.
624 cases compare every output bit, delay sample and ring index, including
zero-length calls, tiny/large chunks, both interpolation factors, impulses,
near-Nyquist signals, signed zero, subnormal and large/random finite values,
and mono/3/6-channel fallback. Sanitizer and `-ffp-contract=off` builds were
also checked with Apple Clang on ARM64; x86_64 cross-build was checked.

The optional microbenchmark measures CPU time for 191.5 seconds of synthetic
stereo PCM at 48/96 kHz. Run 0 is warm-up; runs 1–3 alternate backend order.
`dispatch=1` means the production dispatch (NEON only in a supported build),
not an assertion that NEON is active. No timing threshold is a test gate.
Full decoder/analyzer wall time must be measured separately; a FIR speedup
is not the same as full-file speedup.

Do not use `-ffast-math` for equivalence validation. The tested production
compiler uses normal FP contraction; the vector expression allows the same
policy as the scalar multiply/add expression. Other compiler/target changes
should rerun the bitwise tests before enabling this backend.
