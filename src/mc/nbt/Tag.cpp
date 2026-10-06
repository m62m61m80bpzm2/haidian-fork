// from net/minecraft/nbt/StringTag.java (skipString), java.io.DataOutputStream
// / DataInputStream (writeUTF / readUTF semantics — reached on the network
// path via io.netty.buffer.ByteBufOutputStream.writeUTF /
// ByteBufInputStream.readUTF, verified byte-for-byte against
// netty-buffer 4.2.15.Final), and NumericTag cast semantics for
// FloatTag/DoubleTag (from FloatTag.java / DoubleTag.java).

#include "mc/nbt/Tag.hpp"

#include <cmath>
#include <limits>

namespace mc::nbt {

namespace {

void append_utf8_codepoint(std::string& out, uint32_t cp) {
    // standard UTF-8 (NOT modified UTF-8) — this is our in-memory std::string
    // representation; write_java_utf8 converts back to modified UTF-8.
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void append_modified_utf8_unit(std::string& out, uint32_t unit) {
    // one UTF-16 code unit in modified UTF-8 (java writeUTF inner loop)
    if (unit == 0) {
        out.push_back(static_cast<char>(0xC0));  // NUL -> C0 80
        out.push_back(static_cast<char>(0x80));
    } else if (unit < 0x80) {
        out.push_back(static_cast<char>(unit));
    } else if (unit < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (unit >> 6)));
        out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (unit >> 12)));
        out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
    }
}

// decode one standard-UTF-8 code point from `value` at `i`; advances `i` by
// the sequence length. Malformed/truncated bytes decode to U+FFFD (the
// replacement char java strings end up with after a decoder-with-replace
// round trip; writeUTF never sees invalid java Strings).
uint32_t decode_utf8_codepoint(const std::string& value, size_t& i) {
    const size_t n = value.size();
    const uint8_t b0 = static_cast<uint8_t>(value[i]);
    size_t seq = 1;
    uint32_t cp = b0;
    if (b0 < 0x80) {
        seq = 1;
    } else if ((b0 & 0xE0) == 0xC0) {
        seq = 2;
        cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        seq = 3;
        cp = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        seq = 4;
        cp = b0 & 0x07;
    } else {
        i += 1;
        return 0xFFFD;
    }
    if (i + seq > n) {
        i += 1;
        return 0xFFFD;
    }
    for (size_t k = 1; k < seq; ++k) {
        const uint8_t b = static_cast<uint8_t>(value[i + k]);
        if ((b & 0xC0) != 0x80) {
            i += 1;
            return 0xFFFD;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    // overlong / out-of-range -> replacement
    if ((seq == 2 && cp < 0x80) || (seq == 3 && cp < 0x800) || (seq == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        i += 1;
        return 0xFFFD;
    }
    i += seq;
    return cp;
}

}  // namespace

// ---- java modified UTF-8 io (see Tag.hpp for the format note) ----

void write_java_utf8(network::ByteBuffer& out, const std::string& value) {
    // java DataOutputStream.writeUTF: build the encoded body first, throw
    // UTFDataFormatException when > 65535 bytes (nothing is written), then
    // prefix a 2-byte unsigned byte length.
    std::string encoded;
    encoded.reserve(value.size() + value.size() / 4 + 2);
    size_t i = 0;
    while (i < value.size()) {
        const uint32_t cp = decode_utf8_codepoint(value, i);
        if (cp < 0x10000) {
            append_modified_utf8_unit(encoded, cp);
        } else {
            // supplementary code point: java iterates UTF-16 code units, so a
            // 4-byte UTF-8 char becomes TWO 3-byte surrogate sequences.
            const uint32_t x = cp - 0x10000;
            append_modified_utf8_unit(encoded, 0xD800 | (x >> 10));
            append_modified_utf8_unit(encoded, 0xDC00 | (x & 0x3FF));
        }
    }
    if (encoded.size() > 0xFFFF) {
        throw NbtFormatError("encoded string too long: " + std::to_string(encoded.size()) + " bytes");
    }
    out.write_u16(static_cast<uint16_t>(encoded.size()));
    out.write_bytes(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
}

std::string read_java_utf8(network::ByteBuffer& in) {
    // java DataInputStream.readUTF (ByteBufInputStream.readUTF delegates to
    // it): 2-byte unsigned byte length, then STRICT 1/2/3-byte modified
    // UTF-8 decode — 4-byte lead bytes are "malformed input" because writeUTF
    // never emits them. Surrogate pairs are recombined into standard UTF-8
    // for storage; lone surrogates become U+FFFD.
    const size_t len = in.read_u16();
    std::vector<uint8_t> buf(len);
    if (len > 0) {
        in.read_bytes(buf.data(), len);  // java EOFException at underflow
    }
    std::string out;
    out.reserve(len);
    uint32_t pending_high_surrogate = 0;  // java String holds lone surrogates; we recombine
    const auto flush_pending = [&](std::string& s) {
        if (pending_high_surrogate != 0) {
            append_utf8_codepoint(s, 0xFFFD);
            pending_high_surrogate = 0;
        }
    };
    size_t i = 0;
    while (i < len) {
        const size_t byte_index = i;
        const uint8_t b = buf[i];
        uint32_t unit = 0;
        if (b < 0x80) {
            unit = b;  // note: java readUTF accepts a raw 0x00 byte here
            i += 1;
        } else if ((b & 0xE0) == 0xC0) {
            if (i + 2 > len || (buf[i + 1] & 0xC0) != 0x80) {
                throw NbtFormatError("malformed input around byte " + std::to_string(byte_index));
            }
            unit = (static_cast<uint32_t>(b & 0x1F) << 6) | (buf[i + 1] & 0x3F);
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            if (i + 3 > len || (buf[i + 1] & 0xC0) != 0x80 || (buf[i + 2] & 0xC0) != 0x80) {
                throw NbtFormatError("malformed input around byte " + std::to_string(byte_index));
            }
            unit = (static_cast<uint32_t>(b & 0x0F) << 12) | (static_cast<uint32_t>(buf[i + 1] & 0x3F) << 6) | (buf[i + 2] & 0x3F);
            i += 3;
        } else {
            throw NbtFormatError("malformed input around byte " + std::to_string(byte_index));
        }
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            flush_pending(out);  // two high surrogates in a row
            pending_high_surrogate = unit;
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            if (pending_high_surrogate != 0) {
                const uint32_t cp = 0x10000 + ((pending_high_surrogate - 0xD800) << 10) + (unit - 0xDC00);
                append_utf8_codepoint(out, cp);
                pending_high_surrogate = 0;
            } else {
                append_utf8_codepoint(out, 0xFFFD);
            }
        } else {
            flush_pending(out);
            append_utf8_codepoint(out, unit);
        }
    }
    flush_pending(out);
    return out;
}

void skip_java_utf8(network::ByteBuffer& in) {
    // StringTag.skipString: input.skipBytes(input.readUnsignedShort());
    in.skip_bytes(in.read_u16());
}

// ---- FloatTag / DoubleTag java cast semantics ----

namespace {

// Mth.floor(double/float): (int)Math.floor(v) — java (int) cast AFTER flooring:
// NaN -> 0, out-of-range saturates at Integer.MIN/MAX_VALUE. Note this is
// floor (towards -inf), NOT C++ truncation: -2.5 -> -3.
int32_t mth_floor_int(double v) {
    if (std::isnan(v)) {
        return 0;
    }
    const double f = std::floor(v);
    if (f >= static_cast<double>(std::numeric_limits<int32_t>::max())) {
        return std::numeric_limits<int32_t>::max();
    }
    if (f <= static_cast<double>(std::numeric_limits<int32_t>::min())) {
        return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(f);
}

// java (long) cast from floating point: NaN -> 0, otherwise saturating.
// (FloatTag.longValue()/DoubleTag.longValue() = (long)value — NO floor.)
int64_t java_long_cast(double v) {
    if (std::isnan(v)) {
        return 0;
    }
    if (v >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
        return std::numeric_limits<int64_t>::max();
    }
    if (v <= -9.2233720368547758e18) {  // 2^63 exactly representable edge
        return std::numeric_limits<int64_t>::min();
    }
    return static_cast<int64_t>(v);
}

}  // namespace

// FloatTag.java / DoubleTag.java:
//   intValue()  = Mth.floor(value)                       (floor, then saturating int cast)
//   longValue() = (long) value                           (plain saturating cast)
//   shortValue()= (short) (Mth.floor(value) & 65535)     (low 16 bits, wraps)
//   byteValue() = (byte)  (Mth.floor(value) & 0xFF)      (low 8 bits, wraps)
int8_t FloatTag::as_byte() const {
    return static_cast<int8_t>(static_cast<uint8_t>(mth_floor_int(value_) & 0xFF));
}
int16_t FloatTag::as_short() const {
    return static_cast<int16_t>(static_cast<uint16_t>(mth_floor_int(value_) & 0xFFFF));
}
int32_t FloatTag::as_int() const {
    return mth_floor_int(value_);
}
int64_t FloatTag::as_long() const {
    return java_long_cast(value_);
}

int8_t DoubleTag::as_byte() const {
    return static_cast<int8_t>(static_cast<uint8_t>(mth_floor_int(value_) & 0xFF));
}
int16_t DoubleTag::as_short() const {
    return static_cast<int16_t>(static_cast<uint16_t>(mth_floor_int(value_) & 0xFFFF));
}
int32_t DoubleTag::as_int() const {
    return mth_floor_int(value_);
}
int64_t DoubleTag::as_long() const {
    return java_long_cast(value_);
}

}  // namespace mc::nbt
