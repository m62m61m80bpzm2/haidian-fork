#pragma once
// from net/minecraft/network/CompressionDecoder.java
//   + net/minecraft/network/CompressionEncoder.java
//
// zlib framing: java.util.zip.Deflater/Inflater default = RFC 1950 (zlib
// wrapper, 2-byte header + adler32) — matches the system zlib functions used
// here with default windowBits (deflateInit/inflateInit, NOT raw).
//
// Threshold logic (server side, threshold = 256 by default):
//  ENCODE (CompressionEncoder.encode):
//    len > 8388608                 -> IllegalArgumentException
//    len <  threshold              -> VarInt(0)  + raw payload
//    else                          -> VarInt(len)+ deflate(payload)  (default level)
//  DECODE (CompressionDecoder.decode, validateDecompressed=true on the server):
//    VarInt(0)                     -> rest of the frame is the raw payload
//    expected < threshold          -> DecoderException("Badly compressed packet - size of X is below server threshold of Y")
//    expected > 8388608            -> DecoderException("... larger than protocol maximum of 8388608")
//    else                          -> inflate into exactly `expected` bytes;
//                                     produced < expected -> DecoderException
//                                     ("Badly compressed packet - actual length of
//                                     uncompressed payload N is does not match
//                                     declared size M"); surplus compressed
//                                     input is ignored (java does not check
//                                     needsInput after inflating a full buffer).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mc::server::network {

class CompressionCodec {
public:
    // CompressionDecoder.MAXIMUM_UNCOMPRESSED_LENGTH
    static constexpr int32_t kMaxUncompressedLength = 8388608;   // 8 MiB
    // CompressionDecoder.MAXIMUM_COMPRESSED_LENGTH (declared cap constant)
    static constexpr int32_t kMaxCompressedLength = 2097152;     // 2 MiB

    // java new Deflater(): default level (-1 == Z_DEFAULT_COMPRESSION == 6),
    // zlib wrapper. Per-packet deflate with Z_FINISH mirrors the java encoder
    // (setInput + finish + drain + reset for every packet).
    static std::vector<uint8_t> compress(const uint8_t* data, size_t len);

    // Inflates the stream into exactly `expected` bytes; throws
    // network::McProtocolError with java's DecoderException text on a short
    // result. (Java: inflate into a buffer of uncompressedLength; produced
    // size must equal it.)
    static std::vector<uint8_t> decompress(const uint8_t* data, size_t len, size_t expected);
};

}  // namespace mc::server::network
