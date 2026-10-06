# P1-a: mc-tests — pure-assert-style unit tests (no third-party framework).
add_executable(mc-tests
  mc_tests.cpp
)

target_link_libraries(mc-tests PRIVATE mc-core)

# P1-b: protocol-layer unit tests (MD5 / offline UUID / compression codec /
# packet codecs) — same assert style, separate binary.
add_executable(mc-protocol-tests
  protocol_tests.cpp
)

target_link_libraries(mc-protocol-tests PRIVATE mc-protocol-server)
