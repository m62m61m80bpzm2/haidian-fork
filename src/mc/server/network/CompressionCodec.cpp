// from net/minecraft/network/CompressionDecoder.java / CompressionEncoder.java
// (see header for the threshold logic).
#include "mc/server/network/CompressionCodec.hpp"

#include <stdexcept>
#include <string>

#include <zlib.h>

#include "mc/network/ByteBuffer.hpp"

namespace mc::server::network {

std::vector<uint8_t> CompressionCodec::compress(const uint8_t* data, size_t len) {
    // CompressionEncoder.encode first line:
    //   if (uncompressedLength > 8388608) throw IllegalArgumentException
    if (len > static_cast<size_t>(kMaxUncompressedLength)) {
        throw std::runtime_error("Packet too big (is " + std::to_string(len) +
                                 ", should be less than 8388608)");
    }

    z_stream zs{};
    // java new Deflater() == deflateInit(level = Z_DEFAULT_COMPRESSION,
    // windowBits = 15 -> RFC 1950 zlib wrapper)
    if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK) {
        throw std::runtime_error("deflateInit failed");
    }

    std::vector<uint8_t> out;
    out.resize(deflateBound(&zs, static_cast<uLong>(len)));

    zs.next_in = const_cast<Bytef*>(data);
    zs.avail_in = static_cast<uInt>(len);
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());

    // java: deflater.finish(); while (!finished()) deflate(buf);
    const int rc = deflate(&zs, Z_FINISH);
    const size_t produced = out.size() - zs.avail_out;
    deflateEnd(&zs);
    if (rc != Z_STREAM_END) {
        throw std::runtime_error("deflate did not finish (rc=" + std::to_string(rc) + ")");
    }
    out.resize(produced);
    return out;
}

std::vector<uint8_t> CompressionCodec::decompress(const uint8_t* data, size_t len,
                                                  size_t expected) {
    z_stream zs{};
    // java new Inflater() == inflateInit(windowBits = 15, RFC 1950)
    if (inflateInit(&zs) != Z_OK) {
        throw std::runtime_error("inflateInit failed");
    }

    // java Inflater.inflate(new byte[0]) returns 0 without touching the
    // stream — zlib would return Z_BUF_ERROR here, so mirror java by
    // short-circuiting the empty-size case.
    if (expected == 0) {
        inflateEnd(&zs);
        return std::vector<uint8_t>();
    }

    std::vector<uint8_t> out(expected);
    zs.next_in = const_cast<Bytef*>(data);
    zs.avail_in = static_cast<uInt>(len);
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(expected);

    // java inflate(): one call into the fixed-size buffer; produced length
    // must equal the declared size.
    const int rc = inflate(&zs, Z_NO_FLUSH);
    const size_t produced = expected - zs.avail_out;
    inflateEnd(&zs);
    if (rc < 0) {
        throw mc::network::McProtocolError(std::string("Inflater data error: ") + (zs.msg != nullptr ? zs.msg : "unknown"));
    }
    if (produced != expected) {
        // CompressionDecoder.inflate DecoderException (sic — keeps java's wording)
        throw mc::network::McProtocolError(
            "Badly compressed packet - actual length of uncompressed payload " +
            std::to_string(produced) + " is does not match declared size " +
            std::to_string(expected));
    }
    return out;
}

}  // namespace mc::server::network
