// see ServerConnection.hpp for the pipeline/threading design.
#include "mc/server/network/ServerConnection.hpp"

#include <spdlog/spdlog.h>

#include <string>
#include <vector>

#include "mc/network/ByteBuffer.hpp"
#include "mc/server/network/CompressionCodec.hpp"

namespace mc::server::network {

namespace {
// Sanity cap on a single compressed frame: uncompressed payloads are capped at
// 8 MiB (CompressionDecoder.MAXIMUM_UNCOMPRESSED_LENGTH); an incompressible
// 8 MiB payload plus the size varint stays under 8 MiB + 5.
constexpr uint32_t kMaxFrameLength = CompressionCodec::kMaxUncompressedLength + 5;
constexpr size_t kMaxVarIntBytes = 5;  // net/minecraft/network/VarInt.java
}  // namespace

ServerConnection::ServerConnection(asio::ip::tcp::socket socket)
    : socket_(std::move(socket)) {
    std::error_code ec;
    const asio::ip::tcp::endpoint ep = socket_.remote_endpoint(ec);
    peer_ = ec ? "(unknown)" : ep.address().to_string() + ":" + std::to_string(ep.port());
}

void ServerConnection::set_state(protocol::ConnectionState new_state) {
    state_ = new_state;
}

void ServerConnection::enable_compression(int32_t threshold) {
    compression_threshold_ = threshold;
}

bool ServerConnection::read_exact(uint8_t* dst, size_t n) {
    size_t received = 0;
    while (received < n) {
        std::error_code ec;
        const size_t got = socket_.read_some(asio::buffer(dst + received, n - received), ec);
        if (ec) {
            if (ec != asio::error::eof && ec != asio::error::operation_aborted) {
                spdlog::debug("{}: read error: {}", peer_, ec.message());
            }
            connected_ = false;
            return false;
        }
        received += got;
    }
    return true;
}

bool ServerConnection::write_all(const uint8_t* src, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        std::error_code ec;
        sent += asio::write(socket_, asio::buffer(src + sent, n - sent), ec);
        if (ec) {
            spdlog::debug("{}: write error: {}", peer_, ec.message());
            connected_ = false;
            return false;
        }
    }
    return true;
}

std::optional<std::pair<int32_t, mc::network::ByteBuffer>> ServerConnection::read_packet() {
    if (!connected_) {
        return std::nullopt;
    }

    // --- frame length (MinecraftVarintFrameDecoder) ---
    uint32_t frame_len = 0;
    size_t varint_bytes = 0;
    for (;;) {
        uint8_t byte = 0;
        if (!read_exact(&byte, 1)) {
            return std::nullopt;
        }
        frame_len |= static_cast<uint32_t>(byte & 0x7F) << (7 * varint_bytes);
        if ((byte & 0x80) == 0) {
            break;
        }
        if (++varint_bytes >= kMaxVarIntBytes) {
            // java: VarInt.read throws "VarInt too big" (5 bytes max)
            spdlog::warn("{}: frame VarInt too big", peer_);
            close();
            return std::nullopt;
        }
    }
    if (frame_len == 0 || frame_len > kMaxFrameLength) {
        spdlog::warn("{}: bad frame length {}", peer_, frame_len);
        close();
        return std::nullopt;
    }

    // --- frame payload ---
    std::vector<uint8_t> frame(frame_len);
    if (!read_exact(frame.data(), frame_len)) {
        return std::nullopt;
    }

    // --- CompressionDecoder ---
    const uint8_t* packet_data = frame.data();
    size_t packet_len = frame_len;
    std::vector<uint8_t> inflated;
    if (compression_threshold_.has_value()) {
        mc::network::ByteBuffer prefix = mc::network::ByteBuffer::wrap(frame.data(), frame_len);
        const int32_t declared = prefix.read_varint();
        const size_t consumed = prefix.read_pos();
        if (declared == 0) {
            // uncompressed within threshold: raw bytes follow the size prefix
            packet_data = frame.data() + consumed;
            packet_len = frame_len - consumed;
        } else {
            // CompressionDecoder.decode validation (validateDecompressed=true)
            if (declared < compression_threshold_.value()) {
                spdlog::warn("{}: Badly compressed packet - size of {} is below server threshold of {}",
                             peer_, declared, compression_threshold_.value());
                close();
                return std::nullopt;
            }
            if (declared > CompressionCodec::kMaxUncompressedLength) {
                spdlog::warn("{}: Badly compressed packet - size of {} is larger than protocol maximum of {}",
                             peer_, declared, CompressionCodec::kMaxUncompressedLength);
                close();
                return std::nullopt;
            }
            try {
                inflated = CompressionCodec::decompress(frame.data() + consumed,
                                                        frame_len - consumed,
                                                        static_cast<size_t>(declared));
            } catch (const std::exception& e) {
                spdlog::warn("{}: {}", peer_, e.what());
                close();
                return std::nullopt;
            }
            packet_data = inflated.data();
            packet_len = inflated.size();
        }
    }

    // --- PacketDecoder: packet id varint then body ---
    mc::network::ByteBuffer packet = mc::network::ByteBuffer::wrap(packet_data, packet_len);
    int32_t packet_id = 0;
    try {
        packet_id = packet.read_varint();
    } catch (const std::exception& e) {
        spdlog::warn("{}: bad packet id: {}", peer_, e.what());
        close();
        return std::nullopt;
    }
    return std::make_pair(packet_id, std::move(packet));
}

bool ServerConnection::send_packet(const mc::network::ByteBuffer& payload) {
    if (!connected_) {
        return false;
    }

    std::vector<uint8_t> body;
    mc::network::ByteBuffer body_buf;
    if (compression_threshold_.has_value()) {
        const size_t n = payload.data().size();
        if (static_cast<int32_t>(n) < compression_threshold_.value()) {
            // CompressionEncoder.encode: below threshold -> VarInt(0) + raw
            body_buf.write_varint(0);
            body_buf.write_bytes(payload.data().data(), n);
        } else {
            // else -> VarInt(len) + deflate(payload)
            body_buf.write_varint(static_cast<int32_t>(n));
            try {
                const std::vector<uint8_t> compressed =
                    CompressionCodec::compress(payload.data().data(), n);
                body_buf.write_bytes(compressed.data(), compressed.size());
            } catch (const std::exception& e) {
                spdlog::error("{}: compression failed: {}", peer_, e.what());
                return false;
            }
        }
    } else {
        body_buf.write_bytes(payload.data().data(), payload.data().size());
    }

    // FrameEncoder: outer varint length prefix over the (possibly compressed) body
    mc::network::ByteBuffer frame;
    frame.write_varint(static_cast<int32_t>(body_buf.data().size()));
    frame.write_bytes(body_buf.data().data(), body_buf.data().size());
    return write_all(frame.data().data(), frame.data().size());
}

void ServerConnection::close() {
    if (!connected_) {
        return;
    }
    connected_ = false;
    std::error_code ec;
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
}

}  // namespace mc::server::network
