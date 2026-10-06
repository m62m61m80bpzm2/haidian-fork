# P0-a: empty server — asio listener only; protocol handling lands in P1.
add_executable(mc-server
  mcserver_main.cpp
  mc/util/Mth.cpp
)

target_include_directories(mc-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mc-server PRIVATE
  ZLIB::ZLIB        # P1 compression placeholder (占位)
  Threads::Threads  # asio standalone on Linux
)
