// from net/minecraft/core/UUIDUtil.java (see header).
#include "mc/core/UUIDUtil.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>

#include "mc/util/Md5.hpp"

namespace mc::core::uuid_util {

util::UUID create_offline_player_uuid(const std::string& player_name) {
    // UUIDUtil.createOfflinePlayerUUID:
    //   UUID.nameUUIDFromBytes(("OfflinePlayer:" + playerName).getBytes(UTF_8))
    const std::string seed = "OfflinePlayer:" + player_name;
    const std::array<uint8_t, 16> md5_bytes =
        util::md5(reinterpret_cast<const uint8_t*>(seed.data()), seed.size());

    // JDK21 UUID.nameUUIDFromBytes: md5[6] = (byte)((md5[6] & 0x0f) | 0x30);
    //                               md5[8] = (byte)((md5[8] & 0x3f) | 0x80);
    std::array<uint8_t, 16> bytes = md5_bytes;
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x30);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80);

    uint64_t most = 0;
    uint64_t least = 0;
    for (int i = 0; i < 8; ++i) {
        most = (most << 8) | bytes[i];
    }
    for (int i = 8; i < 16; ++i) {
        least = (least << 8) | bytes[i];
    }
    return util::UUID(most, least);
}

std::string to_string(const util::UUID& uuid) {
    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                  static_cast<unsigned>((uuid.most() >> 32) & 0xffffffffu),
                  static_cast<unsigned>((uuid.most() >> 16) & 0xffffu),
                  static_cast<unsigned>(uuid.most() & 0xffffu),
                  static_cast<unsigned>((uuid.least() >> 48) & 0xffffu),
                  static_cast<unsigned long long>(uuid.least() & 0xffffffffffffull));
    return std::string(buf);
}

}  // namespace mc::core::uuid_util
