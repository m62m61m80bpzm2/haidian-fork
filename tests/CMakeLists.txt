# P1-a: mc-tests — pure-assert-style unit tests (no third-party framework).
add_executable(mc-tests
  mc_tests.cpp
)

target_link_libraries(mc-tests PRIVATE mc-core)
