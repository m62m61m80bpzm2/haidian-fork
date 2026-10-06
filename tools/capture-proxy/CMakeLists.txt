# P0-b: capture-proxy — MITM proxy forwarding client<->upstream bytes
# untouched, capturing per-direction raw streams (+ optional MC VarInt
# frame logs with --parse). Standalone executable; asio/spdlog header-only
# (compile defs + -isystem come from the root CMakeLists).
add_executable(capture-proxy
  main.cpp
)

target_include_directories(capture-proxy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(capture-proxy PRIVATE
  Threads::Threads  # asio standalone on Linux
)
