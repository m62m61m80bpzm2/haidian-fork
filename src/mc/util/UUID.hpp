#pragma once
// Java source: java.util.UUID (JDK 21)
//
// Minimal C++ mirror of the UUID bit-pair container used by the protocol
// layer (FriendlyByteBuf.readUUID/writeUUID: two big-endian longs, most
// significant bits first). The full UUIDUtil (offline-player v3 UUIDs,
// net/minecraft/core/UUIDUtil.java) lands in P1-b; only the value type is
// needed for P1-a.
//
// Java package java.util -> namespace mc::util (closest mirror; plan §2 rule 3
// maps net.minecraft.* packages, java.util.UUID has no dedicated home).

#include <compare>
#include <cstdint>

namespace mc::util {

class UUID {
public:
    UUID() = default;
    UUID(uint64_t most, uint64_t least) : most_(most), least_(least) {}

    // java UUID.getMostSignificantBits() / getLeastSignificantBits()
    uint64_t most() const { return most_; }
    uint64_t least() const { return least_; }

    friend bool operator==(const UUID&, const UUID&) = default;
    friend std::strong_ordering operator<=>(const UUID&, const UUID&) = default;

private:
    uint64_t most_ = 0;
    uint64_t least_ = 0;
};

}  // namespace mc::util
