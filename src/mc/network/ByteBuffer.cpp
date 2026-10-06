// from net/minecraft/network/FriendlyByteBuf.java (+ VarInt.java, VarLong.java,
// Utf8String.java) — see ByteBuffer.hpp for the full mapping notes.

#include "mc/network/ByteBuffer.hpp"

#include <cstring>

namespace mc::network {

// ---- raw access ----

void ByteBuffer::set_read_pos(size_t pos) {
    if (pos > data_.size()) {
        // java readerIndex(int) throws IndexOutOfBoundsException
        throw McProtocolError("readerIndex out of bounds: " + std::to_string(pos) + " > " + std::to_string(data_.size()));
    }
    read_pos_ = pos;
}

void ByteBuffer::write_bytes(const uint8_t* p, size_t n) {
    data_.insert(data_.end(), p, p + n);
}

void ByteBuffer::read_bytes(uint8_t* dst, size_t n) {
    ensure_readable(n);
    std::memcpy(dst, data_.data() + read_pos_, n);
    read_pos_ += n;
}

void ByteBuffer::skip_bytes(size_t n) {
    ensure_readable(n);
    read_pos_ += n;
}

void ByteBuffer::ensure_readable(size_t n) const {
    if (readable_bytes() < n) {
        // java: ByteBuf.readByte() at EOF -> IndexOutOfBoundsException
        throw McProtocolError("buffer underflow: wanted " + std::to_string(n) + " bytes, got " + std::to_string(readable_bytes()));
    }
}

// ---- fixed-width primitives (big-endian, per netty ByteBuf default order) ----

void ByteBuffer::write_byte(int8_t v) {
    data_.push_back(static_cast<uint8_t>(v));
}

int8_t ByteBuffer::read_byte() {
    ensure_readable(1);
    return static_cast<int8_t>(data_[read_pos_++]);
}

void ByteBuffer::write_u16(uint16_t v) {
    data_.push_back(static_cast<uint8_t>(v >> 8));
    data_.push_back(static_cast<uint8_t>(v));
}

uint16_t ByteBuffer::read_u16() {
    ensure_readable(2);
    const uint16_t v = static_cast<uint16_t>((static_cast<uint16_t>(data_[read_pos_]) << 8) | data_[read_pos_ + 1]);
    read_pos_ += 2;
    return v;
}

void ByteBuffer::write_short(int16_t v) {
    write_u16(static_cast<uint16_t>(v));
}

int16_t ByteBuffer::read_short() {
    return static_cast<int16_t>(read_u16());
}

void ByteBuffer::write_int(int32_t v) {
    const uint32_t u = static_cast<uint32_t>(v);
    data_.push_back(static_cast<uint8_t>(u >> 24));
    data_.push_back(static_cast<uint8_t>(u >> 16));
    data_.push_back(static_cast<uint8_t>(u >> 8));
    data_.push_back(static_cast<uint8_t>(u));
}

int32_t ByteBuffer::read_int() {
    ensure_readable(4);
    const uint32_t u = (static_cast<uint32_t>(data_[read_pos_]) << 24) | (static_cast<uint32_t>(data_[read_pos_ + 1]) << 16) |
                       (static_cast<uint32_t>(data_[read_pos_ + 2]) << 8) | static_cast<uint32_t>(data_[read_pos_ + 3]);
    read_pos_ += 4;
    return static_cast<int32_t>(u);
}

void ByteBuffer::write_long(int64_t v) {
    const uint64_t u = static_cast<uint64_t>(v);
    for (int shift = 56; shift >= 0; shift -= 8) {
        data_.push_back(static_cast<uint8_t>(u >> shift));
    }
}

int64_t ByteBuffer::read_long() {
    ensure_readable(8);
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u = (u << 8) | data_[read_pos_ + i];
    }
    read_pos_ += 8;
    return static_cast<int64_t>(u);
}

void ByteBuffer::write_float(float v) {
    // java DataOutput.writeFloat / netty ByteBuf.writeFloat: raw IEEE-754 bits
    // (netty uses floatToRawIntBits — NaN payloads survive, unlike
    // Float.floatToIntBits which canonicalises).
    uint32_t bits = 0;
    static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32-bit");
    std::memcpy(&bits, &v, sizeof(bits));
    const int32_t as_int = static_cast<int32_t>(bits);
    write_int(as_int);
}

float ByteBuffer::read_float() {
    const uint32_t bits = static_cast<uint32_t>(read_int());
    float v = 0.0f;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

void ByteBuffer::write_double(double v) {
    // java DataOutput.writeDouble / netty ByteBuf.writeDouble: raw bits
    uint64_t bits = 0;
    static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64-bit");
    std::memcpy(&bits, &v, sizeof(bits));
    write_long(static_cast<int64_t>(bits));
}

double ByteBuffer::read_double() {
    const uint64_t bits = static_cast<uint64_t>(read_long());
    double v = 0.0;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

// ---- VarInt (from net/minecraft/network/VarInt.java) ----

void ByteBuffer::write_varint(int32_t value) {
    // VarInt.write: 7 bits per byte, low group first, continuation bit 0x80.
    // java keeps `value` an int and uses >>>= (logical shift), so negative
    // values stay 32-bit two's complement and always take 5 bytes.
    uint32_t v = static_cast<uint32_t>(value);
    while ((v & 0xFFFFFF80u) != 0) {  // java: (value & -128) != 0
        write_byte(static_cast<int8_t>((v & 127) | 128));
        v >>= 7;
    }
    write_byte(static_cast<int8_t>(v));
}

int32_t ByteBuffer::read_varint() {
    // VarInt.read: throws "VarInt too big" once a 6th byte has been consumed
    // (java checks bytes > 5 AFTER shifting each byte in).
    uint32_t out = 0;
    int bytes = 0;
    for (;;) {
        const uint8_t in = static_cast<uint8_t>(read_byte());
        out |= static_cast<uint32_t>(in & 127) << (bytes * 7);
        ++bytes;
        if (bytes > 5) {
            throw McProtocolError("VarInt too big");
        }
        if ((in & 128) == 0) {
            break;
        }
    }
    return static_cast<int32_t>(out);
}

size_t ByteBuffer::varint_size(int32_t value) {
    // VarInt.getByteSize: first i in [1,5) with (value & (-1 << i*7)) == 0, else 5
    const uint32_t v = static_cast<uint32_t>(value);
    for (size_t i = 1; i < 5; ++i) {
        if ((v & (0xFFFFFFFFu << (i * 7))) == 0) {
            return i;
        }
    }
    return 5;
}

// ---- VarLong (from net/minecraft/network/VarLong.java) ----

void ByteBuffer::write_varlong(int64_t value) {
    // VarLong.write: same scheme as VarInt over 64 bits, 10 bytes max.
    uint64_t v = static_cast<uint64_t>(value);
    while ((v & 0xFFFFFFFFFFFFFF80u) != 0) {  // java: (value & -128L) != 0
        write_byte(static_cast<int8_t>((v & 127) | 128));
        v >>= 7;
    }
    write_byte(static_cast<int8_t>(v));
}

int64_t ByteBuffer::read_varlong() {
    // VarLong.read: "VarLong too big" once an 11th byte has been consumed.
    uint64_t out = 0;
    int bytes = 0;
    for (;;) {
        const uint8_t in = static_cast<uint8_t>(read_byte());
        out |= static_cast<uint64_t>(in & 127) << (bytes * 7);
        ++bytes;
        if (bytes > 10) {
            throw McProtocolError("VarLong too big");
        }
        if ((in & 128) == 0) {
            break;
        }
    }
    return static_cast<int64_t>(out);
}

size_t ByteBuffer::varlong_size(int64_t value) {
    // VarLong.getByteSize: first i in [1,10) with (value & (-1L << i*7)) == 0, else 10
    const uint64_t v = static_cast<uint64_t>(value);
    for (size_t i = 1; i < 10; ++i) {
        if ((v & (0xFFFFFFFFFFFFFFFFu << (i * 7))) == 0) {
            return i;
        }
    }
    return 10;
}

// ---- UTF strings (from net/minecraft/network/Utf8String.java) ----

void ByteBuffer::write_utf(const std::string& value, size_t max_len) {
    // Utf8String.write: two guards (char count, encoded byte count), then
    // VarInt byte length + raw UTF-8. value must be valid UTF-8 — callers
    // pass MC strings, which always are (netty encodes from UTF-16 with the
    // same result).
    const size_t units = utf16_length(value);
    if (units > max_len) {
        throw McProtocolError("String too big (was " + std::to_string(units) + " characters, max " + std::to_string(max_len) + ")");
    }
    const size_t encoded = value.size();
    const size_t max_encoded = max_len * 3;  // ByteBufUtil.utf8MaxBytes(max_len) == 3*n
    if (encoded > max_encoded) {
        throw McProtocolError("String too big (was " + std::to_string(encoded) + " bytes encoded, max " + std::to_string(max_encoded) + ")");
    }
    write_varint(static_cast<int32_t>(encoded));
    write_bytes(reinterpret_cast<const uint8_t*>(value.data()), encoded);
}

std::string ByteBuffer::read_utf(size_t max_len) {
    // Utf8String.read — check order matches java exactly:
    // len > 3*max_len -> error; len < 0 -> error; len > available -> error;
    // decoded char count > max_len -> error.
    const int64_t max_encoded = static_cast<int64_t>(max_len) * 3;
    const int64_t buffer_len = read_varint();
    if (buffer_len > max_encoded) {
        throw McProtocolError("The received encoded string buffer length is longer than maximum allowed (" + std::to_string(buffer_len) +
                              " > " + std::to_string(max_encoded) + ")");
    }
    if (buffer_len < 0) {
        throw McProtocolError("The received encoded string buffer length is less than zero! Weird string!");
    }
    const size_t available = readable_bytes();
    if (static_cast<size_t>(buffer_len) > available) {
        throw McProtocolError("Not enough bytes in buffer, expected " + std::to_string(buffer_len) + ", but got " + std::to_string(available));
    }
    std::string result(reinterpret_cast<const char*>(data_.data() + read_pos_), static_cast<size_t>(buffer_len));
    read_pos_ += static_cast<size_t>(buffer_len);
    const size_t units = utf16_length(result);
    if (units > max_len) {
        throw McProtocolError("The received string length is longer than maximum allowed (" + std::to_string(units) + " > " +
                              std::to_string(max_len) + ")");
    }
    return result;
}

// ---- UUID (from FriendlyByteBuf.writeUUID/readUUID) ----

void ByteBuffer::write_uuid(const util::UUID& uuid) {
    // writeUUID: output.writeLong(uuid.getMostSignificantBits());
    //            output.writeLong(uuid.getLeastSignificantBits());
    write_long(static_cast<int64_t>(uuid.most()));
    write_long(static_cast<int64_t>(uuid.least()));
}

util::UUID ByteBuffer::read_uuid() {
    // readUUID: new UUID(input.readLong(), input.readLong())
    const uint64_t most = static_cast<uint64_t>(read_long());
    const uint64_t least = static_cast<uint64_t>(read_long());
    return util::UUID(most, least);
}

// ---- byte array (from FriendlyByteBuf.writeByteArray/readByteArray) ----

void ByteBuffer::write_byte_array(const void* bytes, size_t n) {
    // writeByteArray: VarInt.write(output, bytes.length); output.writeBytes(bytes);
    write_varint(static_cast<int32_t>(n));
    write_bytes(static_cast<const uint8_t*>(bytes), n);
}

std::vector<uint8_t> ByteBuffer::read_byte_array() {
    // readByteArray() defaults maxSize to the remaining readable bytes
    return read_byte_array(readable_bytes());
}

std::vector<uint8_t> ByteBuffer::read_byte_array(size_t max_size) {
    // readByteArray: VarInt length; size > maxSize -> DecoderException;
    // negative size -> java NegativeArraySizeException.
    const int32_t size = read_varint();
    if (size < 0) {
        throw McProtocolError("NegativeArraySizeException: " + std::to_string(size));
    }
    if (static_cast<size_t>(size) > max_size) {
        throw McProtocolError("ByteArray with size " + std::to_string(size) + " is bigger than allowed " + std::to_string(max_size));
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0) {
        read_bytes(bytes.data(), bytes.size());
    }
    return bytes;
}

// ---- UTF-16 code-unit counting ----

size_t ByteBuffer::utf16_length(std::string_view utf8) {
    // java String.length() == number of UTF-16 code units. Valid UTF-8:
    // 1 unit for 1-3 byte sequences, 2 units for 4-byte (surrogate pair).
    // Malformed bytes are counted 1-for-1, matching the REPLACE behaviour of
    // netty's `buf.toString(idx, len, StandardCharsets.UTF_8)` decode.
    size_t units = 0;
    size_t i = 0;
    const size_t n = utf8.size();
    while (i < n) {
        const uint8_t b0 = static_cast<uint8_t>(utf8[i]);
        size_t seq = 1;
        if (b0 < 0x80) {
            seq = 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            seq = 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            seq = 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            units += 2;  // code point > 0xFFFF -> surrogate pair
            i += 4;
            continue;
        }
        units += 1;
        i += seq;
    }
    return units;
}

}  // namespace mc::network
