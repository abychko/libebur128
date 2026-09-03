cmake_minimum_required(VERSION 2.8.12)

set(ENABLE_TESTS OFF CACHE BOOL "Build test binaries, needs libsndfile")
set(ENABLE_FUZZER OFF CACHE BOOL "Build fuzzer binary")

if(ENABLE_TESTS)
  find_package(PkgConfig REQUIRED)
  find_pkg_config(SNDFILE sndfile REQUIRED)

  include_directories(${EBUR128_INCLUDE_DIR})
  include_directories(SYSTEM ${SNDFILE_INCLUDE_DIRS})

  add_executable(r128-test-library tests)
  add_executable(minimal-example minimal-example)
  set_property(TARGET r128-test-library APPEND_STRING PROPERTY
      COMPILE_FLAGS " ${SNDFILE_CFLAGS}")
  set_property(TARGET minimal-example APPEND_STRING PROPERTY
      COMPILE_FLAGS " ${SNDFILE_CFLAGS}")
  target_link_libraries(r128-test-library ebur128 ${SNDFILE_LIBRARIES})
  target_link_libraries(minimal-example ebur128 ${SNDFILE_LIBRARIES})
endif()

if(ENABLE_FUZZER)
  include_directories(${EBUR128_INCLUDE_DIR})

  add_executable(fuzzer fuzzer)
  target_link_libraries(fuzzer ebur128)
  target_compile_options(fuzzer PUBLIC "${FUZZER_FLAGS}")
  target_link_libraries(fuzzer "${FUZZER_FLAGS}")
endif()

# Self-contained differential tests; no audio fixtures or libsndfile needed.
option(ENABLE_NEON_TESTS "Build scalar/NEON interpolation equivalence tests" OFF)
if(ENABLE_NEON_TESTS)
  add_executable(neon-test neon.c)
  add_executable(neon-benchmark neon-benchmark.c)
  if(EBUR128_ENABLE_NEON AND EBUR128_HAVE_ARM64_NEON)
    target_compile_definitions(neon-test PRIVATE EBUR128_NEON=1)
    target_compile_definitions(neon-benchmark PRIVATE EBUR128_NEON=1)
  endif()
  if(MATH_LIBRARY)
    target_link_libraries(neon-test ${MATH_LIBRARY})
    target_link_libraries(neon-benchmark ${MATH_LIBRARY})
  endif()
  enable_testing()
  add_test(NAME neon-equivalence COMMAND neon-test)
endif()
