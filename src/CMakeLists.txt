# P1-a: mc-core static lib (ByteBuffer + NBT) shared by mc-server and mc-tests;
# mc-server sources themselves unchanged (mcserver_main.cpp + Mth.cpp via lib).
add_library(mc-core STATIC
  mc/util/Mth.cpp
  mc/network/ByteBuffer.cpp
  mc/nbt/Tag.cpp
  mc/nbt/CompoundTag.cpp
  mc/nbt/NbtIo.cpp
)

target_include_directories(mc-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mc-core PUBLIC
  ZLIB::ZLIB        # P1 compression placeholder (占位)
  Threads::Threads  # asio standalone on Linux
)

add_executable(mc-server
  mcserver_main.cpp
)

target_link_libraries(mc-server PRIVATE mc-core)
