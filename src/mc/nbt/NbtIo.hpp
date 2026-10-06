#pragma once
// from net/minecraft/nbt/NbtIo.java
//   + net/minecraft/network/FriendlyByteBuf.java (writeNbt / readNbt, L526-562)
//   + net/minecraft/network/codec/ByteBufCodecs.java (TAG / TRUSTED_TAG codecs)
//
// Two ROOT framings over the shared tag payload format (see Tag.hpp):
//
//  * DISK — "named root" (NbtIo.write / NbtIo.readUnnamedTag):
//      [type byte][writeUTF(root name)][payload]
//    The java writer always emits "" as the root name (NbtIo.writeUnnamedTag);
//    the reader always SKIPS the name without looking at it. We keep the name
//    parameter (default "") so region-file style writers can record one.
//    gzip'd .dat (NbtIo.readCompressed/writeCompressed) is NOT handled in
//    P1-a — bare streams only; gzip comes with the P3 anvil work (zlib is
//    already linked).
//
//  * NETWORK — (FriendlyByteBuf.writeNbt/readNbt -> NbtIo.writeAnyTag/readAnyTag,
//    the 1.20.5+ format used by protocol 776):
//      [type byte][payload]      — no name; TAG_END (0) == java null
//    read_network_root returns nullptr for TAG_END, mirroring
//    FriendlyByteBuf.readNbt (`tag.getId() == 0 ? null : tag`), which is what
//    Optional<CompoundTag> fields decode from.

#include <memory>
#include <string>

#include "mc/nbt/Tag.hpp"

namespace mc::nbt {

// NbtIo.writeUnnamedTag: writeByte(root id); if (id != 0) { writeUTF(name); payload }
void write_named_root(const Tag& root, network::ByteBuffer& out, const std::string& name = "");

// NbtIo.readUnnamedTag: type byte; 0 -> EndTag; else skipString(root name) + payload
std::unique_ptr<Tag> read_named_root(network::ByteBuffer& in, NbtAccounter& accounter);

// NbtIo.read: named root must be a compound, else java throws
// IOException("Root tag must be a named compound tag")
std::unique_ptr<CompoundTag> read_named_compound_root(network::ByteBuffer& in, NbtAccounter& accounter);

// FriendlyByteBuf.writeNbt: null -> EndTag.INSTANCE (single 0 byte);
// else NbtIo.writeAnyTag: writeByte(id); if (id != 0) payload (no name)
void write_network_root(const Tag* root /*nullable*/, network::ByteBuffer& out);

// FriendlyByteBuf.readNbt: TAG_END root -> nullptr; else payload
std::unique_ptr<Tag> read_network_root(network::ByteBuffer& in, NbtAccounter& accounter);

// TagTypes.getType(id).load(...) — dispatch on the type byte; unknown ids
// throw NbtFormatError("Invalid tag id: X") (java TagType.createInvalid).
std::unique_ptr<Tag> read_tag_payload(int8_t type_id, network::ByteBuffer& in, NbtAccounter& accounter);

}  // namespace mc::nbt
