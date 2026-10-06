#pragma once
// from net/minecraft/network/FriendlyByteBuf.java
//   + net/minecraft/network/VarInt.java
//   + net/minecraft/network/VarLong.java
//   + net/minecraft/network/Utf8String.java
//   (decompiled source: /home/z/my-project/mcsrc/, Vineflower 1.12.0,
//    official Mojang names; ByteBufUtil constants verified against the
//    netty-buffer 4.2.15.Final bundled inside server-26.2.jar)
//
// C++ port of the FriendlyByteBuf byte primitives (plan §2 rule 1: logic 1:1,
// memory redesigned — java wraps a netty ByteBuf; we own a flat byte vector
// with an advancing read cursor: writes append, reads move the cursor).
//
// Byte order: netty ByteBuf defaults to BIG_ENDIAN and FriendlyByteBuf never
// swaps it, so every multi-byte primitive below is big-endian.
//
// Strings (read_utf/write_utf = FriendlyByteBuf.readUtf/writeUtf -> Utf8String):
//   - wire prefix is a VarInt counting BYTES;
//   - max_len counts UTF-16 code units (java String.length());
//   - netty ByteBufUtil.utf8MaxBytes(n) == 3 * n — measured on
//     netty-buffer 4.2.15.Final: utf8MaxBytes(32767) == 98301. One UTF-16 unit
//     is at most 3 encoded bytes (4-byte UTF-8 only arises from surrogate
//     pairs, which are 2 units).
// NBT strings do NOT use this format — they are java DataOutput.writeUTF
// (u16 length + modified UTF-8); see src/mc/nbt/Tag.hpp.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "mc/util/UUID.hpp"

namespace mc::network {

// java: netty DecoderException / EncoderException / RuntimeException("VarInt too big")
struct McProtocolError : std::runtime_error {
    explicit McProtocolError(const std::string& what_arg) : std::runtime_error(what_arg) {}
};

class ByteBuffer {
public:
    ByteBuffer() = default;
    explicit ByteBuffer(std::vector<uint8_t> data) : data_(std::move(data)) {}

    // decoder helper: wrap existing bytes, read cursor starts at 0
    static ByteBuffer wrap(const uint8_t* p, size_t n) {
        return ByteBuffer(std::vector<uint8_t>(p, p + n));
    }

    // ---- raw access (FriendlyByteBuf is a ByteBuf view; tests + NBT io need it) ----
    const std::vector<uint8_t>& data() const { return data_; }
    size_t read_pos() const { return read_pos_; }
    void set_read_pos(size_t pos);                       // java ByteBuf.readerIndex(int)
    size_t readable_bytes() const { return data_.size() - read_pos_; }

    void write_bytes(const uint8_t* p, size_t n);        // ByteBuf.writeBytes
    void read_bytes(uint8_t* dst, size_t n);             // ByteBuf.readBytes(byte[])
    void skip_bytes(size_t n);                           // ByteBuf.skipBytes

    // ---- fixed-width primitives, all big-endian ----
    void write_byte(int8_t v);       int8_t   read_byte();
    void write_u16(uint16_t v);      uint16_t read_u16();  // DataOutput.writeUTF length prefix
    void write_short(int16_t v);     int16_t  read_short();
    void write_int(int32_t v);       int32_t  read_int();
    void write_long(int64_t v);      int64_t  read_long();
    void write_float(float v);       float    read_float();  // raw IEEE-754 bits
    void write_double(double v);     double   read_double();

    // ---- VarInt / VarLong (from VarInt.java / VarLong.java) ----
    void write_varint(int32_t value);   // negatives keep 32-bit two's complement, 5 bytes max
    void write_varlong(int64_t value);  // 10 bytes max
    int32_t read_varint();              // >5 bytes -> McProtocolError("VarInt too big")
    int64_t read_varlong();             // >10 bytes -> McProtocolError("VarLong too big")
    static size_t varint_size(int32_t value);   // VarInt.getByteSize
    static size_t varlong_size(int64_t value);  // VarLong.getByteSize

    // ---- UTF strings (from Utf8String.java) ----
    void write_utf(const std::string& value, size_t max_len = 32767);
    std::string read_utf(size_t max_len = 32767);

    // ---- UUID (from FriendlyByteBuf.writeUUID/readUUID: two big-endian longs) ----
    void write_uuid(const util::UUID& uuid);
    util::UUID read_uuid();

    // ---- byte array (from FriendlyByteBuf.writeByteArray/readByteArray) ----
    void write_byte_array(const void* bytes, size_t n);   // VarInt length + raw bytes
    std::vector<uint8_t> read_byte_array();               // maxSize = readable_bytes()
    std::vector<uint8_t> read_byte_array(size_t max_size);

    // java String.length() for a UTF-8 string: number of UTF-16 code units
    // (surrogate pair counts as 2). Malformed/truncated bytes count 1 each,
    // mirroring netty's decoder-replace behaviour for toString(UTF_8).
    static size_t utf16_length(std::string_view utf8);

private:
    void ensure_readable(size_t n) const;  // java ByteBuf underflow -> IndexOutOfBoundsException
    std::vector<uint8_t> data_;
    size_t read_pos_ = 0;
};

}  // namespace mc::network
