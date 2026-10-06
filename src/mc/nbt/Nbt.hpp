#pragma once
// Java source dir: net/minecraft/nbt/
//   (/home/z/my-project/mcsrc/net/minecraft/nbt/)
//
// Implemented in P1-a — see Tag.hpp (13-tag set + NbtAccounter) and
// NbtIo.hpp (disk named-root + network nameless-root framings).
// Wire-format note, verified against the 26.2 sources (NbtIo.writeAnyTag/
// readAnyTag via FriendlyByteBuf.writeNbt/readNbt + ByteBufOutputStream):
// NBT strings are java DataOutput.writeUTF (u16 length + modified UTF-8) on
// BOTH disk and network; the network change is root framing only (single
// type byte, no root name, TAG_END == null). Packet-level strings outside
// NBT use VarInt prefixes — mc::network::ByteBuffer::read_utf.

namespace mc::nbt {
// P0-a placeholder header — kept (rather than deleted) so existing includes
// keep compiling; the real API lives in Tag.hpp / NbtIo.hpp.
}  // namespace mc::nbt
