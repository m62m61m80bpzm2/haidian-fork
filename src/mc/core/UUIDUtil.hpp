#pragma once
// from net/minecraft/core/UUIDUtil.java (namespace mapped per plan §2 rule 3:
// net.minecraft.core -> mc::core, src/mc/core/).
//
// P1-b uses only the offline-mode identity path:
//   createOfflinePlayerUUID(name) = UUID.nameUUIDFromBytes(
//       ("OfflinePlayer:" + name).getBytes(UTF_8))
// java's nameUUIDFromBytes (JDK21):
//   md5 = MD5(bytes); md5[6] &= 0x0f; md5[6] |= 0x30;  // version 3
//                     md5[8] &= 0x3f; md5[8] |= 0x80;  // IETF variant
//   then big-endian longs.

#include <string>

#include "mc/util/UUID.hpp"

namespace mc::core::uuid_util {

// UUIDUtil.createOfflinePlayerUUID
util::UUID create_offline_player_uuid(const std::string& player_name);

// java UUID.toString() form (8-4-4-4-12 lowercase hex with dashes).
std::string to_string(const util::UUID& uuid);

}  // namespace mc::core::uuid_util
