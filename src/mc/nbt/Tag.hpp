#pragma once
// from net/minecraft/nbt/Tag.java
//   + TagTypes.java, NumericTag.java, EndTag.java,
//     ByteTag/ShortTag/IntTag/LongTag/FloatTag/DoubleTag.java,
//     ByteArrayTag.java, StringTag.java, ListTag.java, CompoundTag.java,
//     IntArrayTag.java, LongArrayTag.java,
//     NbtAccounter.java, NbtException/NbtFormatException/NbtAccounterException.java
//   (decompiled source: /home/z/my-project/mcsrc/, Vineflower 1.12.0,
//    official Mojang names)
//
// Java package net.minecraft.nbt -> namespace mc::nbt.
//
// ---------------------------------------------------------------------------
// WIRE FORMATS — verified against the 26.2 sources and the bundled netty
// (4.2.15.Final), do not guess:
//
//  * Tag PAYLOADS are byte-identical on disk and on the network. Both paths
//    go through java DataOutput/DataInput (network: FriendlyByteBuf wraps the
//    ByteBuf in io.netty.buffer.ByteBufOutputStream/ByteBufInputStream).
//    Strings are DataOutput.writeUTF: a 2-byte UNSIGNED length counting BYTES
//    followed by *modified UTF-8* (NUL -> C0 80; supplementary chars -> two
//    3-byte surrogate sequences, CESU-8 style). Verified byte-for-byte against
//    netty-buffer 4.2.15.Final ByteBufOutputStream.writeUTF:
//        "A\u0000你\0" -> 00 08 41 C0 80 E4 BD A0 C0 80
//
//  * The 1.20.5+ NETWORK change is ROOT FRAMING only (NbtIo.writeAnyTag/
//    readAnyTag, reached via FriendlyByteBuf.writeNbt/readNbt): a single type
//    byte, NO root name, and TAG_END (0) as root == java null (absent NBT,
//    Optional<CompoundTag>). The claim that network NBT strings switched to
//    VarInt length prefixes is FALSE for 26.2 — packet-level strings
//    (mc::network::ByteBuffer::read_utf) use VarInt prefixes, NBT strings do
//    not. Root names were last present in 1.20.4.
//
//  * DISK root (NbtIo.write/read): type byte + writeUTF(name) + payload. The
//    reader always SKIPS the root name (NbtIo.readUnnamedTag ->
//    StringTag.skipString); NbtIo.write always writes "". gzip'd .dat files
//    are NbtIo.readCompressed/writeCompressed — P1-a supports bare streams
//    only; gzip lands with the P3 anvil work (zlib already linked).
//
// Memory redesign (plan §2 rule 2): java shares immutable tag instances
// freely; we use unique_ptr ownership. CompoundTag children live in a
// std::map (java uses an unordered HashMap — iteration/write ORDER differs;
// compounds are name-keyed so this is semantically neutral, and std::map
// gives deterministic serialization).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "mc/network/ByteBuffer.hpp"

namespace mc::nbt {

// ---- errors (from NbtException.java / NbtFormatException.java / NbtAccounterException.java) ----
class NbtError : public std::runtime_error {
public:
    explicit NbtError(const std::string& what_arg) : std::runtime_error(what_arg) {}
};
class NbtFormatError : public NbtError {  // java NbtFormatException
public:
    explicit NbtFormatError(const std::string& what_arg) : NbtError(what_arg) {}
};
class NbtAccounterError : public NbtError {  // java NbtAccounterException
public:
    explicit NbtAccounterError(const std::string& what_arg) : NbtError(what_arg) {}
};

// ---- NbtAccounter (from NbtAccounter.java) ----
// Simplified for P1-a: quota + stack-depth tracking, no thread-local stacks.
// Java defaults: DEFAULT_NBT_QUOTA = 2097152, MAX_STACK_DEPTH = 512.
class NbtAccounter {
public:
    static constexpr long kDefaultQuota = 2097152;       // DEFAULT_NBT_QUOTA
    static constexpr long kUncompressedQuota = 104857600;  // UNCOMPRESSED_NBT_QUOTA
    static constexpr int kMaxDepth = 512;                // MAX_STACK_DEPTH

    explicit NbtAccounter(long quota = kDefaultQuota, int max_depth = kMaxDepth)
        : quota_(quota), max_depth_(max_depth) {}

    static NbtAccounter unlimited_heap() { return NbtAccounter(~0L, kMaxDepth); }  // java unlimitedHeap()

    // accountBytes: negative size -> java IllegalArgumentException
    void account_bytes(long bytes) {
        if (bytes < 0) {
            throw NbtAccounterError("Tried to account NBT tag with negative size: " + std::to_string(bytes));
        }
        if (usage_ + bytes > quota_) {
            throw NbtAccounterError("Tried to read NBT tag that was too big; tried to allocate: " + std::to_string(usage_) + " + " +
                                    std::to_string(bytes) + " bytes where max allowed: " + std::to_string(quota_));
        }
        usage_ += bytes;
    }
    void account_bytes(long bytes_per_entry, long count) { account_bytes(bytes_per_entry * count); }

    // pushDepth / popDepth: depth > max_depth -> NbtAccounterException
    void push_depth() {
        if (depth_ >= max_depth_) {
            throw NbtAccounterError("Tried to read NBT tag with too high complexity, depth > " + std::to_string(max_depth_));
        }
        ++depth_;
    }
    void pop_depth() {
        if (depth_ <= 0) {
            throw NbtAccounterError("NBT-Accounter tried to pop stack-depth at top-level");
        }
        --depth_;
    }

    long usage() const { return usage_; }
    int depth() const { return depth_; }

private:
    long quota_;
    long usage_ = 0;
    int max_depth_;
    int depth_ = 0;
};

// RAII helper mirroring java's pushDepth/try/finally/popDepth pairs.
namespace detail {
struct DepthGuard {
    explicit DepthGuard(NbtAccounter& accounter) : accounter_(accounter) { accounter_.push_depth(); }
    ~DepthGuard() { accounter_.pop_depth(); }
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;
    NbtAccounter& accounter_;
};
}  // namespace detail

// ---- TagType (from Tag.java TAG_* byte constants, values 0..12) ----
enum class TagType : uint8_t {
    End = 0,
    Byte = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Float = 5,
    Double = 6,
    ByteArray = 7,
    String = 8,
    List = 9,
    Compound = 10,
    IntArray = 11,
    LongArray = 12,
};

// java DataOutput.writeUTF / DataInputStream.readUTF ("modified UTF-8") over a
// ByteBuffer — the NBT string format on BOTH disk and network (header note).
// Throws NbtFormatError on malformed input / > 65535 encoded bytes.
void write_java_utf8(network::ByteBuffer& out, const std::string& value);
std::string read_java_utf8(network::ByteBuffer& in);
void skip_java_utf8(network::ByteBuffer& in);  // StringTag.skipString: skipBytes(readUnsignedShort())

class CompoundTag;
class ListTag;

// ---- Tag base (from Tag.java) ----
class Tag {
public:
    virtual ~Tag() = default;
    virtual TagType type() const = 0;  // java getId()
    // java Tag.write(DataOutput): payload only, no type byte / name framing.
    virtual void write(network::ByteBuffer& out) const = 0;
    virtual std::unique_ptr<Tag> copy() const = 0;  // java Tag.copy()
    // java `tag instanceof NumericTag` test
    virtual const class NumericTag* as_numeric() const { return nullptr; }
};

// ---- NumericTag (from NumericTag.java) ----
// as_* mirror java byteValue()/shortValue()/intValue()/longValue()/
// floatValue()/doubleValue() including java's cast semantics (documented per
// implementation in Tag.cpp).
class NumericTag : public Tag {
public:
    virtual int8_t as_byte() const = 0;    // byteValue()
    virtual int16_t as_short() const = 0;  // shortValue()
    virtual int32_t as_int() const = 0;    // intValue()
    virtual int64_t as_long() const = 0;   // longValue()
    virtual float as_float() const = 0;    // floatValue()
    virtual double as_double() const = 0;  // doubleValue()
    const NumericTag* as_numeric() const final { return this; }
};

// ---- EndTag (from EndTag.java; stateless like java's singleton INSTANCE —
// here a public default ctor because unique_ptr ownership needs copy()) ----
class EndTag final : public Tag {
public:
    EndTag() = default;
    TagType type() const override { return TagType::End; }
    void write(network::ByteBuffer&) const override {}
    std::unique_ptr<Tag> copy() const override { return std::make_unique<EndTag>(); }
    static EndTag& instance() {
        static EndTag tag;
        return tag;
    }
};

// ---- ByteTag (from ByteTag.java) ----
class ByteTag final : public NumericTag {
public:
    explicit ByteTag(int8_t value) : value_(value) {}
    static ByteTag value_of(int8_t value) { return ByteTag(value); }
    TagType type() const override { return TagType::Byte; }
    void write(network::ByteBuffer& out) const override { out.write_byte(value_); }  // output.writeByte(value)
    // ByteTag.TYPE.load: accountBytes(9); readByte
    static ByteTag read(network::ByteBuffer& in, NbtAccounter& accounter) {
        accounter.account_bytes(9);
        return ByteTag(in.read_byte());
    }
    std::unique_ptr<Tag> copy() const override { return std::make_unique<ByteTag>(value_); }
    int8_t value() const { return value_; }
    int8_t as_byte() const override { return value_; }
    int16_t as_short() const override { return static_cast<int16_t>(value_); }
    int32_t as_int() const override { return value_; }
    int64_t as_long() const override { return value_; }
    float as_float() const override { return static_cast<float>(value_); }
    double as_double() const override { return static_cast<double>(value_); }

private:
    int8_t value_;
};

// ---- ShortTag (from ShortTag.java) ----
class ShortTag final : public NumericTag {
public:
    explicit ShortTag(int16_t value) : value_(value) {}
    static ShortTag value_of(int16_t value) { return ShortTag(value); }
    TagType type() const override { return TagType::Short; }
    void write(network::ByteBuffer& out) const override { out.write_short(value_); }  // output.writeShort(value)
    // ShortTag.TYPE.load: accountBytes(10); readShort
    static ShortTag read(network::ByteBuffer& in, NbtAccounter& accounter) {
        accounter.account_bytes(10);
        return ShortTag(in.read_short());
    }
    std::unique_ptr<Tag> copy() const override { return std::make_unique<ShortTag>(value_); }
    int16_t value() const { return value_; }
    int8_t as_byte() const override { return static_cast<int8_t>(value_); }
    int16_t as_short() const override { return value_; }
    int32_t as_int() const override { return value_; }
    int64_t as_long() const override { return value_; }
    float as_float() const override { return static_cast<float>(value_); }
    double as_double() const override { return static_cast<double>(value_); }

private:
    int16_t value_;
};

// ---- IntTag (from IntTag.java) ----
class IntTag final : public NumericTag {
public:
    explicit IntTag(int32_t value) : value_(value) {}
    static IntTag value_of(int32_t value) { return IntTag(value); }
    TagType type() const override { return TagType::Int; }
    void write(network::ByteBuffer& out) const override { out.write_int(value_); }  // output.writeInt(value)
    // IntTag.TYPE.load: accountBytes(12); readInt
    static IntTag read(network::ByteBuffer& in, NbtAccounter& accounter) {
        accounter.account_bytes(12);
        return IntTag(in.read_int());
    }
    std::unique_ptr<Tag> copy() const override { return std::make_unique<IntTag>(value_); }
    int32_t value() const { return value_; }
    int8_t as_byte() const override { return static_cast<int8_t>(value_); }
    int16_t as_short() const override { return static_cast<int16_t>(value_); }
    int32_t as_int() const override { return value_; }
    int64_t as_long() const override { return value_; }
    float as_float() const override { return static_cast<float>(value_); }
    double as_double() const override { return static_cast<double>(value_); }

private:
    int32_t value_;
};

// ---- LongTag (from LongTag.java) ----
class LongTag final : public NumericTag {
public:
    explicit LongTag(int64_t value) : value_(value) {}
    static LongTag value_of(int64_t value) { return LongTag(value); }
    TagType type() const override { return TagType::Long; }
    void write(network::ByteBuffer& out) const override { out.write_long(value_); }  // output.writeLong(value)
    // LongTag.TYPE.load: accountBytes(16); readLong
    static LongTag read(network::ByteBuffer& in, NbtAccounter& accounter) {
        accounter.account_bytes(16);
        return LongTag(in.read_long());
    }
    std::unique_ptr<Tag> copy() const override { return std::make_unique<LongTag>(value_); }
    int64_t value() const { return value_; }
    int8_t as_byte() const override { return static_cast<int8_t>(value_); }
    int16_t as_short() const override { return static_cast<int16_t>(value_); }
    int32_t as_int() const override { return static_cast<int32_t>(value_); }
    int64_t as_long() const override { return value_; }
    float as_float() const override { return static_cast<float>(value_); }
    double as_double() const override { return static_cast<double>(value_); }

private:
    int64_t value_;
};

// ---- FloatTag (from FloatTag.java) ----
class FloatTag final : public NumericTag {
public:
    explicit FloatTag(float value) : value_(value) {}
    static FloatTag value_of(float value) { return FloatTag(value); }
    TagType type() const override { return TagType::Float; }
    void write(network::ByteBuffer& out) const override { out.write_float(value_); }  // output.writeFloat(value)
    // FloatTag.TYPE.load: accountBytes(12); readFloat
    static FloatTag read(network::ByteBuffer& in, NbtAccounter& accounter) {
        accounter.account_bytes(12);
        return FloatTag(in.read_float());
    }
    std::unique_ptr<Tag> copy() const override { return std::make_unique<FloatTag>(value_); }
    float value() const { return value_; }
    int8_t as_byte() const override;    // java byteValue(): (byte)(Mth.floor(v) & 0xFF)
    int16_t as_short() const override;  // java shortValue(): (short)(Mth.floor(v) & 65535)
    int32_t as_int() const override;    // java intValue(): Mth.floor(v)
    int64_t as_long() const override;   // java longValue(): (long) v
    float as_float() const override { return value_; }
    double as_double() const override { return static_cast<double>(value_); }

private:
    float value_;
};

// ---- DoubleTag (from DoubleTag.java) ----
class DoubleTag final : public NumericTag {
public:
    explicit DoubleTag(double value) : value_(value) {}
    static DoubleTag value_of(double value) { return DoubleTag(value); }
    TagType type() const override { return TagType::Double; }
    void write(network::ByteBuffer& out) const override { out.write_double(value_); }  // output.writeDouble(value)
    // DoubleTag.TYPE.load: accountBytes(16); readDouble
    static DoubleTag read(network::ByteBuffer& in, NbtAccounter& accounter) {
        accounter.account_bytes(16);
        return DoubleTag(in.read_double());
    }
    std::unique_ptr<Tag> copy() const override { return std::make_unique<DoubleTag>(value_); }
    double value() const { return value_; }
    int8_t as_byte() const override;    // java byteValue(): (byte)(Mth.floor(v) & 0xFF)
    int16_t as_short() const override;  // java shortValue(): (short)(Mth.floor(v) & 65535)
    int32_t as_int() const override;    // java intValue(): Mth.floor(v)
    int64_t as_long() const override;   // java longValue(): (long) v
    float as_float() const override { return static_cast<float>(value_); }
    double as_double() const override { return value_; }

private:
    double value_;
};

// ---- ByteArrayTag (from ByteArrayTag.java) ----
class ByteArrayTag final : public Tag {
public:
    explicit ByteArrayTag(std::vector<uint8_t> data) : data_(std::move(data)) {}
    TagType type() const override { return TagType::ByteArray; }
    void write(network::ByteBuffer& out) const override {
        // java: output.writeInt(this.data.length); output.write(this.data);
        out.write_int(static_cast<int32_t>(data_.size()));
        if (!data_.empty()) {
            out.write_bytes(data_.data(), data_.size());
        }
    }
    // ByteArrayTag.TYPE.load: accountBytes(24); length = readInt;
    // accountBytes(1, length) (negative -> NbtAccounterError); readFully
    static ByteArrayTag read(network::ByteBuffer& in, NbtAccounter& accounter) {
        accounter.account_bytes(24);
        const int32_t length = in.read_int();
        accounter.account_bytes(1, length);
        std::vector<uint8_t> data(static_cast<size_t>(length));
        if (length > 0) {
            in.read_bytes(data.data(), data.size());
        }
        return ByteArrayTag(std::move(data));
    }
    std::unique_ptr<Tag> copy() const override { return std::make_unique<ByteArrayTag>(data_); }
    const std::vector<uint8_t>& value() const { return data_; }  // java getAsByteArray()

private:
    std::vector<uint8_t> data_;
};

// ---- IntArrayTag (from IntArrayTag.java) ----
class IntArrayTag final : public Tag {
public:
    explicit IntArrayTag(std::vector<int32_t> data) : data_(std::move(data)) {}
    TagType type() const override { return TagType::IntArray; }
    void write(network::ByteBuffer& out) const override {
        // java: output.writeInt(length); for (int i : data) output.writeInt(i);
        out.write_int(static_cast<int32_t>(data_.size()));
        for (const int32_t v : data_) {
            out.write_int(v);
        }
    }
    // IntArrayTag.TYPE.load: accountBytes(24); length = readInt;
    // accountBytes(4, length); per-element readInt
    static IntArrayTag read(network::ByteBuffer& in, NbtAccounter& accounter) {
        accounter.account_bytes(24);
        const int32_t length = in.read_int();
        accounter.account_bytes(4, length);
        std::vector<int32_t> data(static_cast<size_t>(length));
        for (int32_t& v : data) {
            v = in.read_int();
        }
        return IntArrayTag(std::move(data));
    }
    std::unique_ptr<Tag> copy() const override { return std::make_unique<IntArrayTag>(data_); }
    const std::vector<int32_t>& value() const { return data_; }  // java getAsIntArray()

private:
    std::vector<int32_t> data_;
};

// ---- LongArrayTag (from LongArrayTag.java) ----
class LongArrayTag final : public Tag {
public:
    explicit LongArrayTag(std::vector<int64_t> data) : data_(std::move(data)) {}
    TagType type() const override { return TagType::LongArray; }
    void write(network::ByteBuffer& out) const override {
        // java: output.writeInt(length); for (long l : data) output.writeLong(l);
        out.write_int(static_cast<int32_t>(data_.size()));
        for (const int64_t v : data_) {
            out.write_long(v);
        }
    }
    // LongArrayTag.TYPE.load: accountBytes(24); length = readInt;
    // accountBytes(8, length); per-element readLong
    static LongArrayTag read(network::ByteBuffer& in, NbtAccounter& accounter) {
        accounter.account_bytes(24);
        const int32_t length = in.read_int();
        accounter.account_bytes(8, length);
        std::vector<int64_t> data(static_cast<size_t>(length));
        for (int64_t& v : data) {
            v = in.read_long();
        }
        return LongArrayTag(std::move(data));
    }
    std::unique_ptr<Tag> copy() const override { return std::make_unique<LongArrayTag>(data_); }
    const std::vector<int64_t>& value() const { return data_; }  // java getAsLongArray()

private:
    std::vector<int64_t> data_;
};

// ---- StringTag (from StringTag.java) ----
class StringTag final : public Tag {
public:
    explicit StringTag(std::string value) : value_(std::move(value)) {}
    // java StringTag.valueOf interns ""; value semantics here make that an
    // optimisation we skip.
    static StringTag value_of(std::string data) { return StringTag(std::move(data)); }
    TagType type() const override { return TagType::String; }
    void write(network::ByteBuffer& out) const override { write_java_utf8(out, value_); }  // output.writeUTF(value)
    // StringTag.TYPE.load: accountBytes(36); readUTF; accountBytes(2, char count)
    static StringTag read(network::ByteBuffer& in, NbtAccounter& accounter) {
        accounter.account_bytes(36);
        std::string data = read_java_utf8(in);
        accounter.account_bytes(2, static_cast<long>(network::ByteBuffer::utf16_length(data)));
        return StringTag(std::move(data));
    }
    std::unique_ptr<Tag> copy() const override { return std::make_unique<StringTag>(value_); }
    const std::string& value() const { return value_; }

private:
    std::string value_;
};

// ---- ListTag (from ListTag.java) ----
// java stores a plain ArrayList with NO type validation on add(); the only
// "heterogeneous" handling is at WRITE time (identifyRawElementType returns 10
// for mixed lists and every element that is not a self-standing compound gets
// wrapped into a one-entry {"" : element} compound) and at READ time
// (addAndUnwrap unwraps those one-entry compounds again). We mirror exactly
// that instead of inventing stricter checks.
class ListTag final : public Tag {
public:
    ListTag() = default;
    TagType type() const override { return TagType::List; }
    void write(network::ByteBuffer& out) const override;
    // ListTag.TYPE.load -> loadList: pushDepth; accountBytes(36); type byte;
    // count = readInt (negative -> NbtFormatError); type==0 && count>0 ->
    // NbtFormatError("Missing type on ListTag"); accountBytes(4, count);
    // per-element load + addAndUnwrap
    static std::unique_ptr<ListTag> read(network::ByteBuffer& in, NbtAccounter& accounter);

    void add(std::unique_ptr<Tag> tag) { list_.push_back(std::move(tag)); }  // java add (no validation)
    void add_and_unwrap(std::unique_ptr<Tag> tag);                           // java addAndUnwrap
    uint8_t identify_raw_element_type() const;  // java @VisibleForTesting identifyRawElementType()
    size_t size() const { return list_.size(); }
    bool empty() const { return list_.empty(); }
    const Tag* get(size_t index) const { return index < list_.size() ? list_[index].get() : nullptr; }
    void clear() { list_.clear(); }
    std::unique_ptr<Tag> copy() const override;

private:
    static std::unique_ptr<Tag> wrap_if_needed(uint8_t element_type, const Tag& tag);  // java wrapIfNeeded
    static std::unique_ptr<Tag> wrap_element(const Tag& tag);                          // java wrapElement
    static bool is_wrapper(const CompoundTag& tag);                                    // java isWrapper
    std::vector<std::unique_ptr<Tag>> list_;
};

// ---- CompoundTag (from CompoundTag.java) ----
class CompoundTag final : public Tag {
public:
    // java uses HashMap; std::map keeps deterministic (sorted-key) write order.
    using Container = std::map<std::string, std::unique_ptr<Tag>>;

    CompoundTag() = default;
    explicit CompoundTag(Container tags) : tags_(std::move(tags)) {}

    TagType type() const override { return TagType::Compound; }
    void write(network::ByteBuffer& out) const override;
    // CompoundTag.TYPE.load -> loadCompound: pushDepth; accountBytes(48);
    // loop { type byte (0 = end); key = readUTF (accountBytes 28 + 2*chars);
    // value = type.load } — the unnamed compound payload.
    static std::unique_ptr<CompoundTag> read(network::ByteBuffer& in, NbtAccounter& accounter);
    std::unique_ptr<Tag> copy() const override;

    // ---- mutation (java put / putXxx) ----
    std::unique_ptr<Tag> put(const std::string& name, std::unique_ptr<Tag> tag);  // returns previous (java @Nullable)
    void put_byte(const std::string& name, int8_t value);
    void put_short(const std::string& name, int16_t value);
    void put_int(const std::string& name, int32_t value);
    void put_long(const std::string& name, int64_t value);
    void put_float(const std::string& name, float value);
    void put_double(const std::string& name, double value);
    void put_string(const std::string& name, std::string value);
    void put_boolean(const std::string& name, bool value);  // java: ByteTag.valueOf(value)
    void put_byte_array(const std::string& name, std::vector<uint8_t> value);
    void put_int_array(const std::string& name, std::vector<int32_t> value);
    void put_long_array(const std::string& name, std::vector<int64_t> value);
    std::unique_ptr<Tag> remove(const std::string& name);

    // ---- read access ----
    const Tag* get(const std::string& name) const;  // null when absent (java @Nullable get)
    bool contains(const std::string& name) const { return tags_.count(name) != 0; }
    size_t size() const { return tags_.size(); }
    bool empty() const { return tags_.empty(); }

    // java getXxxOr(name, default): `tags.get(name) instanceof NumericTag tag ?
    // tag.byteValue() : default` — any numeric tag widens/truncates.
    int8_t get_byte_or(const std::string& name, int8_t default_value) const;
    int16_t get_short_or(const std::string& name, int16_t default_value) const;
    int32_t get_int_or(const std::string& name, int32_t default_value) const;
    int64_t get_long_or(const std::string& name, int64_t default_value) const;
    float get_float_or(const std::string& name, float default_value) const;
    double get_double_or(const std::string& name, double default_value) const;
    std::string get_string_or(const std::string& name, std::string default_value) const;  // instanceof StringTag
    bool get_boolean_or(const std::string& name, bool default_value) const;  // getByteOr(name, def?1:0) != 0

    Container::const_iterator begin() const { return tags_.begin(); }
    Container::const_iterator end() const { return tags_.end(); }

private:
    Container tags_;
};

}  // namespace mc::nbt
