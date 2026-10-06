#pragma once
// java.security.MessageDigest.getInstance("MD5") — JDK 21 provider (RFC 1321).
//
// No third-party crypto library is allowed (plan §2 rule 6), so this is a
// self-contained RFC 1321 MD5 used by mc::core::UUIDUtil for the offline-mode
// v3 player UUID (UUID.nameUUIDFromBytes). Only the one-shot digest form is
// needed (java MessageDigest.digest(byte[])).

#include <array>
#include <cstddef>
#include <cstdint>

namespace mc::util {

// java: MessageDigest.getInstance("MD5").digest(input)
std::array<uint8_t, 16> md5(const uint8_t* data, size_t len);

}  // namespace mc::util
