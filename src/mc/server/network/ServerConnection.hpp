#pragma once
// Server-side connection socket wrapper — the C++ analogue of the netty
// pipeline pieces used by net/minecraft/network/Connection.java:
//
//   inbound : FrameDecoder(varint length) -> CompressionDecoder -> PacketDecoder(varint id -> body)
//   outbound: PacketEncoder -> CompressionEncoder -> FrameEncoder(varint length)
//
// Pipeline order (java ChannelInitializer.initChannel): compression handlers
// sit between the frame codec and the packet codec, so the OUTER varint length
// always counts the COMPRESSED body, and the compression body itself starts
// with the VarInt declared-uncompressed-size (0 = not compressed).
//
// Threading model: one connection is owned by exactly one server thread; all
// reads and writes for a connection happen on that thread (java instead uses
// the netty event loop). enable_compression switches BOTH directions at once,
// matching Connection.setupCompression (decoder + encoder together) which
// vanilla invokes right after the Set Compression packet has been flushed.
//
// Blocking I/O: asio sync read/write on the owning thread. Errors / EOF make
// read_packet return nullopt and send_packet return false; listeners then tear
// the connection down (java: channel inactive -> onDisconnect).

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <asio.hpp>

#include "mc/network/ByteBuffer.hpp"
#include "mc/protocol/ConnectionState.hpp"

namespace mc::server::network {

class ServerConnection {
public:
    explicit ServerConnection(asio::ip::tcp::socket socket);

    bool connected() const { return connected_; }
    const std::string& peer() const { return peer_; }

    protocol::ConnectionState state() const { return state_; }
    void set_state(protocol::ConnectionState new_state);

    // nullopt until Connection.setupCompression (Set Compression handshake)
    std::optional<int32_t> compression_threshold() const { return compression_threshold_; }
    // Connection.setupCompression(threshold, validateDecompressed=true)
    void enable_compression(int32_t threshold);

    // Reads one frame, decompresses when enabled, splits off the packet id.
    // Returns {packet_id, body} or nullopt on EOF / fatal protocol error
    // (java DecoderException -> channel close).
    std::optional<std::pair<int32_t, mc::network::ByteBuffer>> read_packet();

    // Sends a full packet payload (id varint + fields, as produced by the
    // PacketCodec encoders). Applies compression framing when enabled.
    bool send_packet(const mc::network::ByteBuffer& payload);

    // Closes the socket (java channel.close()); idempotent.
    void close();

private:
    bool read_exact(uint8_t* dst, size_t n);
    bool write_all(const uint8_t* src, size_t n);

    asio::ip::tcp::socket socket_;
    std::string peer_;
    protocol::ConnectionState state_ = protocol::ConnectionState::HANDSHAKE;
    std::optional<int32_t> compression_threshold_;
    bool connected_ = true;
};

}  // namespace mc::server::network
