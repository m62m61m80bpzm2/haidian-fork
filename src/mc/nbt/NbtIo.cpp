// from net/minecraft/nbt/NbtIo.java (writeUnnamedTag / readUnnamedTag /
// writeAnyTag / readAnyTag) and net/minecraft/network/FriendlyByteBuf.java
// (writeNbt / readNbt).

#include "mc/nbt/NbtIo.hpp"

namespace mc::nbt {

void write_named_root(const Tag& root, network::ByteBuffer& out, const std::string& name) {
    // from NbtIo.writeUnnamedTag:
    //   output.writeByte(tag.getId());
    //   if (tag.getId() != 0) { output.writeUTF(""); tag.write(output); }
    // (java's NbtIo.write always passes ""; EndTag root writes just the byte)
    const uint8_t id = static_cast<uint8_t>(root.type());
    out.write_byte(static_cast<int8_t>(id));
    if (id != 0) {
        write_java_utf8(out, name);
        root.write(out);
    }
}

std::unique_ptr<Tag> read_named_root(network::ByteBuffer& in, NbtAccounter& accounter) {
    // from NbtIo.readUnnamedTag:
    //   byte type = input.readByte();
    //   if (type == 0) return EndTag.INSTANCE;
    //   StringTag.skipString(input);   // root name is skipped, not kept
    //   return readTagSafe(input, accounter, type);
    const int8_t type_id = in.read_byte();
    if (type_id == 0) {
        return std::make_unique<EndTag>();
    }
    skip_java_utf8(in);
    return read_tag_payload(type_id, in, accounter);
}

std::unique_ptr<CompoundTag> read_named_compound_root(network::ByteBuffer& in, NbtAccounter& accounter) {
    // from NbtIo.read:
    //   if (readUnnamedTag(input, accounter) instanceof CompoundTag c) return c;
    //   else throw new IOException("Root tag must be a named compound tag");
    std::unique_ptr<Tag> root = read_named_root(in, accounter);
    if (CompoundTag* compound = dynamic_cast<CompoundTag*>(root.get())) {
        root.release();
        return std::unique_ptr<CompoundTag>(compound);
    }
    throw NbtFormatError("Root tag must be a named compound tag");
}

void write_network_root(const Tag* root, network::ByteBuffer& out) {
    // from FriendlyByteBuf.writeNbt:
    //   if (tag == null) tag = EndTag.INSTANCE;
    //   NbtIo.writeAnyTag(tag, ...);   // writeByte(id); if (id != 0) tag.write(...)
    const Tag& tag = root != nullptr ? *root : EndTag::instance();
    const uint8_t id = static_cast<uint8_t>(tag.type());
    out.write_byte(static_cast<int8_t>(id));
    if (id != 0) {
        tag.write(out);
    }
}

std::unique_ptr<Tag> read_network_root(network::ByteBuffer& in, NbtAccounter& accounter) {
    // from FriendlyByteBuf.readNbt:
    //   Tag tag = NbtIo.readAnyTag(...);   // readByte; 0 -> EndTag.INSTANCE
    //   return tag.getId() == 0 ? null : tag;
    const int8_t type_id = in.read_byte();
    if (type_id == 0) {
        return nullptr;
    }
    return read_tag_payload(type_id, in, accounter);
}

std::unique_ptr<Tag> read_tag_payload(int8_t type_id, network::ByteBuffer& in, NbtAccounter& accounter) {
    // from TagTypes.getType(typeId).load(input, accounter); unknown ids reach
    // TagType.createInvalid(id).load which throws IOException("Invalid tag id: X")
    // (java byte is signed, so the message shows the signed value).
    switch (type_id) {
        case 0: {
            // EndTag.TYPE.load: accountBytes(8); return INSTANCE
            accounter.account_bytes(8);
            return std::make_unique<EndTag>();
        }
        case 1:
            return std::make_unique<ByteTag>(ByteTag::read(in, accounter));
        case 2:
            return std::make_unique<ShortTag>(ShortTag::read(in, accounter));
        case 3:
            return std::make_unique<IntTag>(IntTag::read(in, accounter));
        case 4:
            return std::make_unique<LongTag>(LongTag::read(in, accounter));
        case 5:
            return std::make_unique<FloatTag>(FloatTag::read(in, accounter));
        case 6:
            return std::make_unique<DoubleTag>(DoubleTag::read(in, accounter));
        case 7:
            return std::make_unique<ByteArrayTag>(ByteArrayTag::read(in, accounter));
        case 8:
            return std::make_unique<StringTag>(StringTag::read(in, accounter));
        case 9:
            return ListTag::read(in, accounter);
        case 10:
            return CompoundTag::read(in, accounter);
        case 11:
            return std::make_unique<IntArrayTag>(IntArrayTag::read(in, accounter));
        case 12:
            return std::make_unique<LongArrayTag>(LongArrayTag::read(in, accounter));
        default:
            throw NbtFormatError("Invalid tag id: " + std::to_string(static_cast<int>(type_id)));
    }
}

}  // namespace mc::nbt
