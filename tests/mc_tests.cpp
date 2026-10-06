// P1-a unit tests: mc::network::ByteBuffer (VarInt/VarLong/UTF/UUID/BE
// primitives) + mc::nbt (13 tags, disk named-root + network nameless-root
// formats, NbtAccounter depth/quota limits).
//
// Reference: /home/z/my-project/mcsrc/net/minecraft/{network,nbt}/*.java
// (Minecraft 26.2, Vineflower decompile, official names). Hand-computed byte
// expectations below were derived from VarInt.java / VarLong.java /
// Utf8String.java / NbtIo.java / StringTag.java (DataOutput.writeUTF).
//
// Framework: pure assert-style main, no third-party deps. NOTE: assert() is
// compiled out under Release (-DNDEBUG), so checks throw std::runtime_error
// instead; each case runs in its own try block and prints [PASS]/[FAIL],
// ending with a "PASS N/M" summary line.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "mc/nbt/NbtIo.hpp"
#include "mc/nbt/Tag.hpp"
#include "mc/network/ByteBuffer.hpp"

// ---------------------------------------------------------------- harness --

namespace network = mc::network;
namespace nbt = mc::nbt;
namespace util = mc::util;

namespace {

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

[[noreturn]] void fail(const std::string& msg) { throw std::runtime_error(msg); }

void check(bool cond, const std::string& msg) {
    if (!cond) {
        fail(msg);
    }
}

// expect an exception of exactly type Ex (or a type derived from Ex alone)
template <typename Ex, typename F>
void expect_throw(F&& f, const std::string& what) {
    try {
        f();
    } catch (const Ex&) {
        return;
    } catch (const std::exception& e) {
        fail(what + ": wrong exception type: " + e.what());
    }
    fail(what + ": expected exception, got none");
}

using Bytes = std::vector<uint8_t>;

Bytes bytes(std::initializer_list<uint8_t> init) { return Bytes(init); }

Bytes written(const std::function<void(network::ByteBuffer&)>& fn) {
    network::ByteBuffer buf;
    fn(buf);
    return buf.data();
}

// ---- MC_TEST: register a named test case ----
#define MC_TEST(fn)                      \
    static void fn();                    \
    static Registrar reg_##fn(#fn, &fn); \
    static void fn()

// -------------------------------------------------------------- VarInt ----

MC_TEST(varint_roundtrip) {
    const std::vector<int32_t> values = {
        0, 1, 127, 128, 2097152, std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min(), -1, 300, -123456,
    };
    for (const int32_t v : values) {
        network::ByteBuffer buf;
        buf.write_varint(v);
        check(network::ByteBuffer::varint_size(v) == buf.data().size(), "varint_size mismatch for " + std::to_string(v));
        buf.set_read_pos(0);
        check(buf.read_varint() == v, "varint roundtrip failed for " + std::to_string(v));
    }
}

// java VarInt.write byte shapes, hand-computed (7 bits/group, lowest group
// first, 0x80 continuation bit; negatives keep 32-bit two's complement)
MC_TEST(varint_exact_bytes) {
    check(written([](network::ByteBuffer& b) { b.write_varint(0); }) == bytes({0x00}), "0");
    check(written([](network::ByteBuffer& b) { b.write_varint(1); }) == bytes({0x01}), "1");
    check(written([](network::ByteBuffer& b) { b.write_varint(127); }) == bytes({0x7F}), "127");
    check(written([](network::ByteBuffer& b) { b.write_varint(128); }) == bytes({0x80, 0x01}), "128");
    check(written([](network::ByteBuffer& b) { b.write_varint(2097152); }) == bytes({0x80, 0x80, 0x80, 0x01}), "2097152");
    check(written([](network::ByteBuffer& b) { b.write_varint(std::numeric_limits<int32_t>::max()); }) ==
              bytes({0xFF, 0xFF, 0xFF, 0xFF, 0x07}),
          "INT32_MAX");
    check(written([](network::ByteBuffer& b) { b.write_varint(std::numeric_limits<int32_t>::min()); }) ==
              bytes({0x80, 0x80, 0x80, 0x80, 0x08}),
          "INT32_MIN");
    check(written([](network::ByteBuffer& b) { b.write_varint(-1); }) == bytes({0xFF, 0xFF, 0xFF, 0xFF, 0x0F}), "-1");
}

MC_TEST(varint_rejects) {
    // 6 continuation bytes -> VarInt.read must throw "VarInt too big" (java
    // checks bytes > 5 after shifting each byte in, before EOF is reached)
    {
        network::ByteBuffer buf;
        for (int i = 0; i < 6; ++i) {
            buf.write_byte(static_cast<int8_t>(0xFF));
        }
        buf.write_byte(0x01);
        buf.set_read_pos(0);
        expect_throw<network::McProtocolError>([&] { (void)buf.read_varint(); }, "6-byte VarInt");
    }
    // truncated buffer -> underflow error (java IndexOutOfBoundsException)
    {
        network::ByteBuffer buf;
        buf.write_byte(static_cast<int8_t>(0x80));  // continuation, then EOF
        buf.set_read_pos(0);
        expect_throw<network::McProtocolError>([&] { (void)buf.read_varint(); }, "truncated VarInt");
    }
}

// -------------------------------------------------------------- VarLong ----

MC_TEST(varlong_roundtrip_and_bytes) {
    const std::vector<int64_t> values = {
        0, 1, 127, 128, 2097152, std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::min(), -1, -1234567890123LL,
    };
    for (const int64_t v : values) {
        network::ByteBuffer buf;
        buf.write_varlong(v);
        check(buf.data().size() == network::ByteBuffer::varlong_size(v), "varlong_size mismatch for " + std::to_string(v));
        buf.set_read_pos(0);
        check(buf.read_varlong() == v, "varlong roundtrip failed for " + std::to_string(v));
    }
    // INT64_MAX = 0x7FFF_FFFF_FFFF_FFFF: 63 set bits -> 8 full groups + 0x7F
    check(written([](network::ByteBuffer& b) { b.write_varlong(std::numeric_limits<int64_t>::max()); }) ==
              bytes({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F}),
          "INT64_MAX is 9 bytes FF*8 7F");
    // INT64_MIN = 0x8000_0000_0000_0000: only bit 63 set -> 10 bytes
    check(written([](network::ByteBuffer& b) { b.write_varlong(std::numeric_limits<int64_t>::min()); }) ==
              bytes({0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01}),
          "INT64_MIN is 10 bytes 80*9 01");
    // > 10 bytes -> "VarLong too big"
    network::ByteBuffer buf;
    for (int i = 0; i < 11; ++i) {
        buf.write_byte(static_cast<int8_t>(0xFF));
    }
    buf.write_byte(0x01);
    buf.set_read_pos(0);
    expect_throw<network::McProtocolError>([&] { (void)buf.read_varlong(); }, "11-byte VarLong");
}

// ----------------------------------------------------------------- UTF ----

MC_TEST(utf_roundtrip_and_multibyte) {
    const std::vector<std::string> values = {
        "", "hello", "你好世界", "emoji 😀 mixed 日本語", "tab\tnewline\n", std::string("nul\0inside", 10),
    };
    for (const std::string& v : values) {
        network::ByteBuffer buf;
        buf.write_utf(v);
        buf.set_read_pos(0);
        check(buf.read_utf() == v, "utf roundtrip failed for byte len " + std::to_string(v.size()));
    }
    // wire form: VarInt byte-count prefix + UTF-8 body
    check(written([](network::ByteBuffer& b) { b.write_utf("A"); }) == bytes({0x01, 0x41}), "utf \"A\" bytes");
    check(written([](network::ByteBuffer& b) { b.write_utf(""); }) == bytes({0x00}), "utf \"\" bytes");
    check(written([](network::ByteBuffer& b) { b.write_utf("你"); }) == bytes({0x03, 0xE4, 0xBD, 0xA0}), "utf 你 bytes");
    check(written([](network::ByteBuffer& b) { b.write_utf("😀"); }) == bytes({0x04, 0xF0, 0x9F, 0x98, 0x80}), "utf emoji bytes");
    // utf16_length: java String.length() semantics (surrogate pair = 2 units)
    check(network::ByteBuffer::utf16_length("") == 0, "utf16_length(\"\")");
    check(network::ByteBuffer::utf16_length("ab") == 2, "utf16_length(ab)");
    check(network::ByteBuffer::utf16_length("你") == 1, "utf16_length(你)");
    check(network::ByteBuffer::utf16_length("😀") == 2, "utf16_length(😀) counts 2 code units");
}

MC_TEST(utf_maxlen_enforced) {
    // write side: char-count guard (emoji is 2 UTF-16 units)
    expect_throw<network::McProtocolError>([] { network::ByteBuffer b; b.write_utf("😀", 1); }, "write emoji with maxLen=1");
    expect_throw<network::McProtocolError>([] { network::ByteBuffer b; b.write_utf("abc", 2); }, "write abc with maxLen=2");
    // read side: decoded char count > maxLen
    {
        network::ByteBuffer buf;
        buf.write_utf("abc");
        buf.set_read_pos(0);
        expect_throw<network::McProtocolError>([&] { (void)buf.read_utf(2); }, "read abc with maxLen=2");
    }
    // read side: declared encoded length > 3*maxLen (Utf8String.read order)
    {
        network::ByteBuffer buf;
        buf.write_varint(10);
        buf.write_bytes(reinterpret_cast<const uint8_t*>("abcd"), 4);
        buf.set_read_pos(0);
        expect_throw<network::McProtocolError>([&] { (void)buf.read_utf(1); }, "declared 10 > 3*1");
    }
    // read side: declared length exceeds bytes in buffer
    {
        network::ByteBuffer buf;
        buf.write_varint(10);
        buf.write_bytes(reinterpret_cast<const uint8_t*>("abcd"), 4);
        buf.set_read_pos(0);
        expect_throw<network::McProtocolError>([&] { (void)buf.read_utf(); }, "declared 10 but only 4 available");
    }
}

// ------------------------------------------------- UUID / float / double ----

MC_TEST(uuid_roundtrip) {
    const util::UUID uuid(0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL);
    network::ByteBuffer buf;
    buf.write_uuid(uuid);
    // two big-endian int64, most significant first
    check(buf.data() == bytes({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10}),
          "uuid wire bytes");
    buf.set_read_pos(0);
    check(buf.read_uuid() == uuid, "uuid roundtrip");
}

MC_TEST(float_double_big_endian_bits) {
    // float: raw IEEE-754 bits, big-endian on the wire
    check(written([](network::ByteBuffer& b) { b.write_float(1.0F); }) == bytes({0x3F, 0x80, 0x00, 0x00}), "float 1.0f");
    check(written([](network::ByteBuffer& b) { b.write_float(-2.5F); }) == bytes({0xC0, 0x20, 0x00, 0x00}), "float -2.5f");
    const std::vector<float> floats = {3.1415927F, -0.0F, std::numeric_limits<float>::max(), std::numeric_limits<float>::min()};
    for (const float f : floats) {
        network::ByteBuffer buf;
        buf.write_float(f);
        buf.set_read_pos(0);
        check(buf.read_float() == f, "float roundtrip");
    }
    // NaN payload survives (netty floatToRawIntBits, not floatToIntBits)
    {
        const uint32_t payload = 0x7FC12345u;  // quiet NaN with nonstandard payload
        float nan_with_payload = 0.0F;
        std::memcpy(&nan_with_payload, &payload, sizeof(nan_with_payload));
        network::ByteBuffer buf;
        buf.write_float(nan_with_payload);
        buf.set_read_pos(0);
        float out = buf.read_float();
        uint32_t out_bits = 0;
        std::memcpy(&out_bits, &out, sizeof(out_bits));
        check(out_bits == payload, "NaN payload survives");
    }
    // double: raw bits, big-endian
    check(written([](network::ByteBuffer& b) { b.write_double(1.0); }) == bytes({0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
          "double 1.0");
    check(written([](network::ByteBuffer& b) { b.write_double(3.141592653589793); }) ==
              bytes({0x40, 0x09, 0x21, 0xFB, 0x54, 0x44, 0x2D, 0x18}),
          "double pi bytes");
    const std::vector<double> doubles = {2.718281828459045, -0.0, std::numeric_limits<double>::max(), 1e-300};
    for (const double d : doubles) {
        network::ByteBuffer buf;
        buf.write_double(d);
        buf.set_read_pos(0);
        check(buf.read_double() == d, "double roundtrip");
    }
}

// --------------------------------------------- fixed-width / byte array ----

MC_TEST(fixed_width_primitives_big_endian) {
    check(written([](network::ByteBuffer& b) { b.write_int(-1); }) == bytes({0xFF, 0xFF, 0xFF, 0xFF}), "int32 -1");
    check(written([](network::ByteBuffer& b) { b.write_short(-2); }) == bytes({0xFF, 0xFE}), "int16 -2");
    check(written([](network::ByteBuffer& b) { b.write_long(-1); }) == bytes({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}),
          "int64 -1");
    check(written([](network::ByteBuffer& b) { b.write_u16(0x1234); }) == bytes({0x12, 0x34}), "u16 0x1234");
    // roundtrips incl. negatives
    for (const int16_t s : {std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max(), static_cast<int16_t>(-1)}) {
        network::ByteBuffer buf;
        buf.write_short(s);
        buf.set_read_pos(0);
        check(buf.read_short() == s, "short roundtrip");
    }
    for (const int32_t i : {std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(), -7}) {
        network::ByteBuffer buf;
        buf.write_int(i);
        buf.set_read_pos(0);
        check(buf.read_int() == i, "int roundtrip");
    }
    // underflow: reading past the end throws
    network::ByteBuffer empty;
    expect_throw<network::McProtocolError>([&] { (void)empty.read_int(); }, "read_int on empty buffer");
    expect_throw<network::McProtocolError>([&] { (void)empty.read_byte(); }, "read_byte on empty buffer");
}

MC_TEST(byte_array_roundtrip_and_limits) {
    {
        const Bytes payload = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42};
        network::ByteBuffer buf;
        buf.write_byte_array(payload.data(), payload.size());
        check(buf.data() == bytes({0x06, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42}), "byte array wire: VarInt len + raw");
        buf.set_read_pos(0);
        check(buf.read_byte_array() == payload, "byte array roundtrip");
    }
    // empty array: just the zero VarInt
    check(written([](network::ByteBuffer& b) { b.write_byte_array(nullptr, 0); }) == bytes({0x00}), "empty byte array");
    // size > maxSize -> rejected
    {
        network::ByteBuffer buf;
        buf.write_byte_array("abc", 3);
        buf.set_read_pos(0);
        expect_throw<network::McProtocolError>([&] { (void)buf.read_byte_array(2); }, "byte array size 3 > max 2");
    }
    // negative size -> rejected (java: NegativeArraySizeException)
    {
        network::ByteBuffer buf;
        buf.write_varint(-1);
        buf.set_read_pos(0);
        expect_throw<network::McProtocolError>([&] { (void)buf.read_byte_array(); }, "negative byte array size");
    }
}

// -------------------------------------------------------- NBT: strings ----

MC_TEST(nbt_string_modified_utf8) {
    using nbt::NbtAccounter;
    using nbt::StringTag;
    // "A\0你" in java modified UTF-8: 41 | C0 80 | E4 BD A0 -> 6 bytes
    {
        network::ByteBuffer buf;
        nbt::write_java_utf8(buf, std::string("A\0", 2) + "你");
        check(buf.data() == bytes({0x00, 0x06, 0x41, 0xC0, 0x80, 0xE4, 0xBD, 0xA0}), "writeUTF \"A\\0你\" wire bytes");
        buf.set_read_pos(0);
        check(nbt::read_java_utf8(buf) == std::string("A\0", 2) + "你", "readUTF \"A\\0你\" roundtrip");
    }
    // emoji: java writeUTF emits CESU-8 surrogate pairs (2x 3-byte), while our
    // in-memory representation is standard UTF-8
    {
        network::ByteBuffer buf;
        nbt::write_java_utf8(buf, "😀");
        check(buf.data() == bytes({0x00, 0x06, 0xED, 0xA0, 0xBD, 0xED, 0xB8, 0x80}), "writeUTF 😀 wire bytes (surrogate pair)");
        buf.set_read_pos(0);
        check(nbt::read_java_utf8(buf) == std::string("😀"), "readUTF 😀 -> standard UTF-8");
    }
    // empty string
    check(written([](network::ByteBuffer& b) { nbt::write_java_utf8(b, ""); }) == bytes({0x00, 0x00}), "writeUTF \"\"");
    // > 65535 encoded bytes -> rejected (u16 length prefix)
    expect_throw<nbt::NbtError>([] { network::ByteBuffer b; nbt::write_java_utf8(b, std::string(70000, 'x')); }, "writeUTF 70000 bytes");
    // skipString: skipBytes(readUnsignedShort())
    {
        network::ByteBuffer buf;
        nbt::write_java_utf8(buf, "hello");
        buf.write_byte(0x7F);
        buf.set_read_pos(0);
        nbt::skip_java_utf8(buf);
        check(buf.read_byte() == 0x7F && buf.readable_bytes() == 0, "skip_java_utf8 skips exactly the string");
    }
    // StringTag roundtrip through the tag API (accounting must not throw)
    {
        NbtAccounter accounter;
        network::ByteBuffer buf;
        StringTag("你好").write(buf);
        buf.set_read_pos(0);
        const auto tag = StringTag::read(buf, accounter);
        check(tag.value() == "你好", "StringTag roundtrip");
    }
}

// --------------------------------------------------- NBT: disk root format ----

// Handwritten "named root" stream (NbtIo.write shape: type byte + writeUTF
// root name + payload), parsed and re-serialized byte-for-byte. Compound keys
// serialize in std::map (sorted) order: bytes < int < long < name < nested.
MC_TEST(nbt_disk_named_root_roundtrip) {
    const Bytes handcrafted = {
        0x0A, 0x00, 0x00,                                    // root: CompoundTag, name ""
        // "bytes": ByteArrayTag [01 02 FF]
        0x07, 0x00, 0x05, 'b', 'y', 't', 'e', 's', 0x00, 0x00, 0x00, 0x03, 0x01, 0x02, 0xFF,
        // "int": IntTag 42
        0x03, 0x00, 0x03, 'i', 'n', 't', 0x00, 0x00, 0x00, 0x2A,
        // "long": LongTag -1
        0x04, 0x00, 0x04, 'l', 'o', 'n', 'g', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        // "name": StringTag "hello"
        0x08, 0x00, 0x04, 'n', 'a', 'm', 'e', 0x00, 0x05, 'h', 'e', 'l', 'l', 'o',
        // "nested": CompoundTag { "list": ListTag<Int> [1, 2, 3] }
        0x0A, 0x00, 0x06, 'n', 'e', 's', 't', 'e', 'd',
        0x09, 0x00, 0x04, 'l', 'i', 's', 't', 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x03,
        0x00,  // end of nested
        0x00,  // end of root
    };

    // (a) build the same tree programmatically and serialize -> identical bytes
    auto root = std::make_unique<nbt::CompoundTag>();
    root->put_byte_array("bytes", {0x01, 0x02, 0xFF});
    root->put_int("int", 42);
    root->put_long("long", -1);
    root->put_string("name", "hello");
    auto nested = std::make_unique<nbt::CompoundTag>();
    auto list = std::make_unique<nbt::ListTag>();
    list->add(std::make_unique<nbt::IntTag>(1));
    list->add(std::make_unique<nbt::IntTag>(2));
    list->add(std::make_unique<nbt::IntTag>(3));
    nested->put("list", std::move(list));
    root->put("nested", std::move(nested));

    network::ByteBuffer out;
    nbt::write_named_root(*root, out);  // java writes "" as root name
    check(out.data() == handcrafted, "programmatic tree serializes to handcrafted disk bytes");

    // (b) parse -> values -> re-serialize -> byte-identical
    nbt::NbtAccounter accounter;
    network::ByteBuffer in(out.data());
    auto parsed = nbt::read_named_compound_root(in, accounter);
    check(parsed->size() == 5, "root has 5 entries");
    check(parsed->get_int_or("int", 0) == 42, "int value");
    check(parsed->get_long_or("long", 0) == -1, "long value");
    check(parsed->get_string_or("name", "") == "hello", "string value");
    const auto& arr = dynamic_cast<const nbt::ByteArrayTag&>(*parsed->get("bytes")).value();
    check(arr == Bytes({0x01, 0x02, 0xFF}), "byte array value");
    const auto& inner = dynamic_cast<const nbt::CompoundTag&>(*parsed->get("nested"));
    const auto& ints = dynamic_cast<const nbt::ListTag&>(*inner.get("list"));
    check(ints.size() == 3, "list size 3");
    check(dynamic_cast<const nbt::IntTag&>(*ints.get(0)).value() == 1 &&
              dynamic_cast<const nbt::IntTag&>(*ints.get(1)).value() == 2 &&
              dynamic_cast<const nbt::IntTag&>(*ints.get(2)).value() == 3,
          "list<int> values");

    network::ByteBuffer out2;
    nbt::write_named_root(*parsed, out2);
    check(out2.data() == handcrafted, "re-serialized disk bytes identical");
}

// -------------------------------------------------- NBT: network format ----

// 26.2 network NBT (FriendlyByteBuf.writeNbt/readNbt -> NbtIo.writeAnyTag/
// readAnyTag): single type byte, NO root name, TAG_END (0) encodes java null.
MC_TEST(nbt_network_nameless_root) {
    // null -> single 0x00 byte; reading it back yields nullptr
    {
        network::ByteBuffer buf;
        nbt::write_network_root(nullptr, buf);
        check(buf.data() == bytes({0x00}), "null network root is one 0x00 byte");
        buf.set_read_pos(0);
        nbt::NbtAccounter accounter;
        check(nbt::read_network_root(buf, accounter) == nullptr, "TAG_END root reads as null");
    }
    // same tree as the disk test: network form == disk form minus the 2-byte
    // empty root name (same type byte, byte-identical payload)
    auto root = std::make_unique<nbt::CompoundTag>();
    root->put_int("i", 1);
    auto list = std::make_unique<nbt::ListTag>();
    list->add(std::make_unique<nbt::LongTag>(-1));
    root->put("l", std::move(list));

    network::ByteBuffer net;
    nbt::write_network_root(root.get(), net);
    check(net.data()[0] == 0x0A, "network root starts with type byte 0x0A");

    network::ByteBuffer disk;
    nbt::write_named_root(*root, disk);
    check(disk.data().size() == net.data().size() + 2, "disk adds exactly the 2-byte \"\" root name");
    check(disk.data()[0] == net.data()[0], "same root type byte");
    check(std::equal(net.data().begin() + 1, net.data().end(), disk.data().begin() + 3), "payload bytes identical");

    // roundtrip through the network reader
    nbt::NbtAccounter accounter;
    network::ByteBuffer in(net.data());
    auto parsed = nbt::read_network_root(in, accounter);
    check(parsed != nullptr && parsed->type() == nbt::TagType::Compound, "network root parses to compound");
    network::ByteBuffer out;
    nbt::write_network_root(parsed.get(), out);
    check(out.data() == net.data(), "network re-serialize identical");
}

// ------------------------------------------- NBT: root edge cases ----

MC_TEST(nbt_root_edges) {
    nbt::NbtAccounter accounter;
    // named root: the reader always SKIPS the name without looking at it
    {
        network::ByteBuffer in(bytes({0x0A, 0x00, 0x02, 'A', 'B', 0x00}));  // compound "AB" {} (empty)
        auto root = nbt::read_named_root(in, accounter);
        check(root != nullptr && root->type() == nbt::TagType::Compound, "name skipped, compound parsed");
        check(dynamic_cast<const nbt::CompoundTag&>(*root).empty(), "empty root compound");
    }
    // EndTag root (0x00)
    {
        network::ByteBuffer in(bytes({0x00}));
        auto root = nbt::read_named_root(in, accounter);
        check(root != nullptr && root->type() == nbt::TagType::End, "EndTag root");
    }
    // NbtIo.read on a non-compound root -> "Root tag must be a named compound tag"
    {
        network::ByteBuffer in(bytes({0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2A}));  // named int root
        expect_throw<nbt::NbtFormatError>([&] { (void)nbt::read_named_compound_root(in, accounter); }, "non-compound root");
    }
    // unknown tag id -> NbtFormatError("Invalid tag id: X")
    {
        network::ByteBuffer in(bytes({0x0D}));
        expect_throw<nbt::NbtFormatError>([&] { (void)nbt::read_tag_payload(13, in, accounter); }, "tag id 13");
        expect_throw<nbt::NbtFormatError>([&] { (void)nbt::read_tag_payload(-1, in, accounter); }, "tag id -1");
    }
}

// ------------------------------------------- NBT: ListTag wrap/unwrap ----

// mixed list [IntTag(1), StringTag("hi")]: identifyRawElementType() -> 10,
// each non-compound element is wrapped into a one-entry {"" : x} compound;
// parsing unwraps those again (ListTag.addAndUnwrap). NOTE: Tag.write() emits
// the PAYLOAD only (the 0x09 list id byte comes from the enclosing frame).
const Bytes kHeteroListBytes = {0x0A, 0x00, 0x00, 0x00, 0x02,
                                0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,  // {"" : IntTag(1)}
                                0x08, 0x00, 0x00, 0x00, 0x02, 'h', 'i', 0x00};   // {"" : StringTag("hi")}

MC_TEST(nbt_list_heterogeneous_wrapping) {
    {
        auto list = std::make_unique<nbt::ListTag>();
        list->add(std::make_unique<nbt::IntTag>(1));
        list->add(std::make_unique<nbt::StringTag>("hi"));
        check(list->identify_raw_element_type() == 10, "mixed list claims compound element type");
        check(written([&list](network::ByteBuffer& b) { list->write(b); }) == kHeteroListBytes,
              "heterogeneous list wire bytes with wrappers");
        nbt::NbtAccounter accounter;
        network::ByteBuffer in(kHeteroListBytes);
        auto parsed = nbt::ListTag::read(in, accounter);
        check(parsed->size() == 2, "unwrapped list keeps 2 elements");
        check(parsed->get(0)->type() == nbt::TagType::Int && dynamic_cast<const nbt::IntTag&>(*parsed->get(0)).value() == 1,
              "element 0 unwrapped to IntTag(1)");
        check(parsed->get(1)->type() == nbt::TagType::String && dynamic_cast<const nbt::StringTag&>(*parsed->get(1)).value() == "hi",
              "element 1 unwrapped to StringTag(hi)");
        // re-serialize: identical bytes (wrap happens again)
        network::ByteBuffer out;
        parsed->write(out);
        check(out.data() == kHeteroListBytes, "heterogeneous list re-serialize identical");
    }
    // self-standing compounds in a list are NOT wrapped (isWrapper == false)
    {
        auto list = std::make_unique<nbt::ListTag>();
        auto compound = std::make_unique<nbt::CompoundTag>();
        compound->put_int("x", 5);
        list->add(std::move(compound));
        check(written([&list](network::ByteBuffer& b) { list->write(b); }) ==
                  bytes({0x0A, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x01, 'x', 0x00, 0x00, 0x00, 0x05, 0x00}),
              "list of compounds: no wrapper");
    }
    // homogeneous list<int>
    check(written([](network::ByteBuffer& b) {
              nbt::ListTag l;
              l.add(std::make_unique<nbt::IntTag>(1));
              l.add(std::make_unique<nbt::IntTag>(2));
              l.add(std::make_unique<nbt::IntTag>(3));
              l.write(b);
          }) == bytes({0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03}),
          "homogeneous list<int> wire bytes");
    // empty list: type 0, count 0
    check(written([](network::ByteBuffer& b) { nbt::ListTag().write(b); }) == bytes({0x00, 0x00, 0x00, 0x00, 0x00}),
          "empty list wire bytes");
    // malformed: negative count / missing type
    {
        nbt::NbtAccounter accounter;
        network::ByteBuffer negative(bytes({0x03, 0xFF, 0xFF, 0xFF, 0xFF}));
        expect_throw<nbt::NbtFormatError>([&] { (void)nbt::ListTag::read(negative, accounter); }, "negative list count");
        network::ByteBuffer missing(bytes({0x00, 0x00, 0x00, 0x00, 0x01}));
        expect_throw<nbt::NbtFormatError>([&] { (void)nbt::ListTag::read(missing, accounter); }, "missing list element type");
    }
}

// ------------------------------------- NBT: NbtAccounter depth + quota ----

namespace {

// build a disk-framed stream of `depth` nested compounds:
// [0A 00 00] + (depth-1) x [0A 00 01 'a'] + depth x [00]
Bytes nested_compound_bytes(int depth) {
    Bytes out = {0x0A, 0x00, 0x00};
    for (int i = 0; i < depth - 1; ++i) {
        const Bytes entry = {0x0A, 0x00, 0x01, static_cast<uint8_t>('a')};
        out.insert(out.end(), entry.begin(), entry.end());
    }
    for (int i = 0; i < depth; ++i) {
        out.push_back(0x00);
    }
    return out;
}

}  // namespace

MC_TEST(nbt_depth_limit_512) {
    // exactly 512 levels is allowed (pushDepth succeeds while depth < 512)
    {
        nbt::NbtAccounter accounter;
        network::ByteBuffer in(nested_compound_bytes(512));
        auto root = nbt::read_named_root(in, accounter);
        check(root->type() == nbt::TagType::Compound, "512-deep compound parses");
        check(accounter.depth() == 0, "depth unwound to 0");
    }
    // 513 levels -> NbtAccounterException("too high complexity, depth > 512")
    {
        nbt::NbtAccounter accounter;
        network::ByteBuffer in(nested_compound_bytes(513));
        expect_throw<nbt::NbtAccounterError>([&] { (void)nbt::read_named_root(in, accounter); }, "513-deep compound");
    }
}

MC_TEST(nbt_quota_limit) {
    // ByteArrayTag declaring 0x7FFFFFFF elements: accountBytes(24) then
    // accountBytes(1, 0x7FFFFFFF) exceeds the 2 MiB default quota BEFORE any
    // allocation happens
    {
        nbt::NbtAccounter accounter;
        network::ByteBuffer in(bytes({0x7F, 0xFF, 0xFF, 0xFF}));
        expect_throw<nbt::NbtAccounterError>([&] { (void)nbt::read_tag_payload(0x07, in, accounter); }, "huge byte array");
    }
    // negative array length -> accountBytes(negative) -> NbtAccounterError
    {
        nbt::NbtAccounter accounter;
        network::ByteBuffer in(bytes({0xFF, 0xFF, 0xFF, 0xFF}));
        expect_throw<nbt::NbtAccounterError>([&] { (void)nbt::read_tag_payload(0x07, in, accounter); }, "negative array length");
    }
    // generous quota passes for a normal payload; usage is tracked
    {
        nbt::NbtAccounter accounter(nbt::NbtAccounter::kUncompressedQuota);
        network::ByteBuffer in(bytes({0x00, 0x00, 0x00, 0x02, 0xCA, 0xFE}));
        auto tag = nbt::read_tag_payload(0x07, in, accounter);
        check(dynamic_cast<nbt::ByteArrayTag*>(tag.get()) != nullptr, "byte array within quota");
        check(accounter.usage() == 24 + 2, "accounter usage = SELF_SIZE + 1*length");
    }
}

// ------------------------------------------- NBT: CompoundTag accessors ----

MC_TEST(nbt_compound_accessors) {
    nbt::CompoundTag c;
    c.put_byte("b", 7);
    c.put_boolean("flag", true);
    c.put_double("d", 2.5);
    c.put_string("s", "x");
    // java getXxxOr: any NumericTag widens/truncates, non-numeric -> default
    check(c.get_int_or("b", 0) == 7, "ByteTag widens to int");
    check(c.get_byte_or("d", 0) == 2, "DoubleTag byteValue (floor+mask)");
    check(c.get_boolean_or("flag", false), "boolean true");
    check(c.get_boolean_or("b", false), "nonzero byte is true");
    check(c.get_string_or("d", "fallback") == "fallback", "string_or on numeric returns default");
    check(c.get_int_or("absent", -5) == -5, "int_or absent returns default");
    // put returns the previous value (java @Nullable put)
    const auto previous = c.put("s", std::make_unique<nbt::StringTag>("y"));
    check(dynamic_cast<const nbt::StringTag*>(previous.get()) != nullptr &&
              dynamic_cast<const nbt::StringTag*>(previous.get())->value() == "x",
          "put returns previous tag");
    check(c.get_string_or("s", "") == "y", "new value visible");
    // remove returns the removed tag, nullptr when absent
    check(c.remove("s") != nullptr, "remove returns tag");
    check(c.remove("s") == nullptr && !c.contains("s"), "second remove is null");
    // copy() is a deep copy
    c.put_int_array("ia", {1, 2, 3});
    const auto copied = c.copy();
    const auto* copied_compound = dynamic_cast<const nbt::CompoundTag*>(copied.get());
    check(copied_compound != nullptr && copied_compound->size() == c.size(), "copy size");
    dynamic_cast<nbt::CompoundTag&>(*copied).put_int("b", 99);
    check(c.get_byte_or("b", 0) == 7, "copy is deep (no aliasing)");
    check((dynamic_cast<const nbt::IntArrayTag&>(*c.get("ia")).value() == std::vector<int32_t>{1, 2, 3}), "int array value");
    {
        nbt::CompoundTag la_holder;
        la_holder.put_long_array("la", {1});
        check((dynamic_cast<const nbt::LongArrayTag&>(*la_holder.get("la")).value() == std::vector<int64_t>{1}), "long array value");
    }
    // FloatTag/DoubleTag java cast semantics (Mth.floor + mask — NOT truncation)
    check(nbt::FloatTag(300.5F).as_int() == 300, "float->int floors");
    check(nbt::DoubleTag(-2.5).as_int() == -3, "double->int floors towards -inf");
    check(nbt::DoubleTag(1e100).as_int() == std::numeric_limits<int32_t>::max(), "double overflow saturates");
    check(nbt::DoubleTag(std::numeric_limits<double>::quiet_NaN()).as_int() == 0, "NaN casts to 0");
    check(nbt::FloatTag(200.0F).as_byte() == static_cast<int8_t>(200), "float->byte low 8 bits (wraps to -56)");
    check(nbt::DoubleTag(-1.5).as_byte() == -2, "double->byte floor(-1.5)=-2 & 0xFF -> -2");
    check(nbt::DoubleTag(-300.5).as_byte() == -45, "double->byte floor(-300.5)&0xFF -> -45");
    check(nbt::FloatTag(70000.0F).as_short() == 4464, "float->short low 16 bits");
    check(nbt::DoubleTag(9.3e18).as_long() == std::numeric_limits<int64_t>::max(), "double->long saturates");
}

// ------------------------------------------------------------------ main ----

}  // namespace

int main() {
    int pass = 0;
    const int total = static_cast<int>(registry().size());
    for (const TestCase& test : registry()) {
        try {
            test.fn();
            std::printf("[PASS] %s\n", test.name);
            ++pass;
        } catch (const std::exception& e) {
            std::printf("[FAIL] %s: %s\n", test.name, e.what());
        } catch (...) {
            std::printf("[FAIL] %s: unknown exception\n", test.name);
        }
    }
    std::printf("PASS %d/%d\n", pass, total);
    return pass == total ? 0 : 1;
}
