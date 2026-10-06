# P1-a/P1-b: mc-core static lib (byte primitives + NBT + protocol + util)
# shared by mc-server and the test executables; mc-server itself wires the
# P1-b protocol state machine (src/mc/server/network + mcserver_main.cpp).
add_library(mc-core STATIC
  mc/util/Mth.cpp
  mc/util/Md5.cpp
  mc/core/UUIDUtil.cpp
  mc/network/ByteBuffer.cpp
  mc/nbt/Tag.cpp
  mc/nbt/CompoundTag.cpp
  mc/nbt/NbtIo.cpp
  mc/protocol/ConnectionState.cpp
  mc/protocol/PacketCodec.cpp
  mc/protocol/ServerStatus.cpp
)

target_include_directories(mc-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mc-core PUBLIC
  ZLIB::ZLIB        # P1-b compression protocol (RFC 1950 framing)
  Threads::Threads  # asio standalone on Linux
)

# P1-b: server-side protocol engine (asio sockets + spdlog logging + the
# three listener classes). Own library so mc-tests can use mc-core alone.
add_library(mc-protocol-server STATIC
  mc/server/level/ClientInformation.cpp
  mc/server/network/CompressionCodec.cpp
  mc/server/network/ServerConnection.cpp
  mc/server/network/ServerHandshakePacketListener.cpp
  mc/server/network/ServerLoginPacketListener.cpp
  mc/server/network/ServerConfigurationPacketListener.cpp
  mc/server/network/Server.cpp
)

target_link_libraries(mc-protocol-server PUBLIC mc-core)

add_executable(mc-server
  mcserver_main.cpp
)

target_link_libraries(mc-server PRIVATE mc-protocol-server)
