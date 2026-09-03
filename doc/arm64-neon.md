# ARM64 stereo loudness kernels

Based on libebur128 1.2.6. CMake detects the target compiler's ARM64 NEON
support; `EBUR128_ENABLE_NEON` defaults to that result. Set it to `OFF` for
the unchanged scalar implementation. Setting it to `ON` on x86_64 still
uses scalar code. No public ABI, filter coefficients, gating, oversampling
factors, or K-filter coefficients/order changes.

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

## Float32 stereo K-filter

A second kernel processes the two recursive K-filter channels in Float64
NEON lanes. Float32 input is converted directly into those lanes. Recursive
state stays in registers within the block, then returns to the original
state array. The manual denormal flush at block end, including leaving
`v[0]` unflushed, matches the scalar implementation. No new allocations.

This path requires ARM64 NEON and Clang/GCC C vector arithmetic. Other
compilers, mono/multichannel, a stereo channel marked `EBUR128_UNUSED`,
zero-length blocks, and short/int/double input use the scalar K-filter.
`EBUR128_ENABLE_NEON=OFF` disables both kernels.

The vector C expressions intentionally preserve the scalar expression
structure and compiler FP contraction. Separate multiply/add intrinsics
caused one-bit differences feeding back into the IIR state. Test any new
compiler configuration before enabling the kernel; do not use fast-math.

`kfilter-test` compares every filtered Double and all five state values
against a separate Float32 instantiation of the original scalar macro.
2520 block cases cover 8/11.025/44.1/48/96/192/352.8 kHz, channel mappings,
nonzero output offsets, tiny/large/empty chunks, signed zero, impulses,
near-Nyquist, subnormal state/input and large/random finite values. Seven
public streaming cases wrap the audio ring and compare LUFS/LRA/momentary/
short-term/true peak exactly. ASan/UBSan and `-ffp-contract=off` also pass
on the validated Apple Clang ARM64 build.

```sh
./build-neon/kfilter-test
./build-neon/kfilter-test --benchmark
```

The optional benchmark uses 191.5 seconds of synthetic stereo PCM at
48/96/192 kHz, CPU time, warm-up run 0 and three alternating backend pairs.
It times K-filter processing without peak/gating work; full-file analysis
must be measured separately. Before/after compiler and architecture flags
must match. The existing FIR differential tests remain enabled.
