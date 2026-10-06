// RFC 1321 MD5 (java.security provider equivalent; see header).
// Self-contained implementation; verified against the RFC 1321 test vectors
// in tests/protocol_tests.cpp.
#include "mc/util/Md5.hpp"

namespace mc::util {

namespace {

struct Md5Ctx {
    uint32_t a = 0x67452301;
    uint32_t b = 0xefcdab89;
    uint32_t c = 0x98badcfe;
    uint32_t d = 0x10325476;
};

// RFC 1321 §3.4: per-round shift amounts.
constexpr int kShifts[4][4] = {
    {7, 12, 17, 22},  // round 1
    {5, 9, 14, 20},   // round 2
    {4, 11, 16, 23},  // round 3
    {6, 10, 15, 21},  // round 4
};

// RFC 1321: K[i] = floor(abs(sin(i + 1)) * 2^32), precomputed.
constexpr uint32_t kSine[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

inline uint32_t rotl(uint32_t v, int s) {
    return (v << s) | (v >> (32 - s));
}

void process_block(Md5Ctx& ctx, const uint8_t* block) {
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
        // little-endian 32-bit words (RFC 1321 §2.1 note)
        m[i] = static_cast<uint32_t>(block[i * 4]) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 3]) << 24);
    }

    uint32_t a = ctx.a;
    uint32_t b = ctx.b;
    uint32_t c = ctx.c;
    uint32_t d = ctx.d;

    for (int i = 0; i < 64; ++i) {
        const int round = i / 16;
        const int step = i % 16;
        uint32_t f = 0;
        int g = 0;
        switch (round) {
            case 0: f = (b & c) | (~b & d); g = i; break;                       // F(b,c,d), i
            case 1: f = (d & b) | (~d & c); g = (5 * i + 1) % 16; break;        // G(b,c,d), (5i+1) mod 16
            case 2: f = b ^ c ^ d; g = (3 * i + 5) % 16; break;                 // H(b,c,d), (3i+5) mod 16
            default: f = c ^ (b | ~d); g = (7 * i) % 16; break;                 // I(b,c,d), (7i) mod 16
        }
        const uint32_t tmp = d;
        d = c;
        c = b;
        // shift pattern repeats every 4 steps within a round (RFC 1321 §3.4
        // lists s = 7,12,17,22, 7,12,17,22, ... per round) — step % 4, NOT
        // step (step = i % 16 would read out of bounds / wrong shifts).
        b = b + rotl(a + f + kSine[i] + m[g], kShifts[round][step % 4]);
        a = tmp;
    }

    ctx.a += a;
    ctx.b += b;
    ctx.c += c;
    ctx.d += d;
}

}  // namespace

std::array<uint8_t, 16> md5(const uint8_t* data, size_t len) {
    Md5Ctx ctx;

    // full 64-byte blocks
    size_t i = 0;
    for (; i + 64 <= len; i += 64) {
        process_block(ctx, data + i);
    }

    // tail: padding (0x80 then zeros), 64-bit little-endian bit length.
    const size_t rest = len - i;
    uint8_t tail[128] = {};
    for (size_t j = 0; j < rest; ++j) {
        tail[j] = data[i + j];
    }
    tail[rest] = 0x80;
    const size_t padded_len = (rest + 1 <= 56) ? 64 : 128;
    const uint64_t total_bits = static_cast<uint64_t>(len) * 8;
    for (int k = 0; k < 8; ++k) {
        tail[padded_len - 8 + k] = static_cast<uint8_t>(total_bits >> (8 * k));
    }
    process_block(ctx, tail);
    if (padded_len == 128) {
        process_block(ctx, tail + 64);
    }

    // output: registers A,B,C,D little-endian byte order (RFC 1321 §3.3)
    std::array<uint8_t, 16> out{};
    const uint32_t regs[4] = {ctx.a, ctx.b, ctx.c, ctx.d};
    for (int r = 0; r < 4; ++r) {
        for (int k = 0; k < 4; ++k) {
            out[r * 4 + k] = static_cast<uint8_t>(regs[r] >> (8 * k));
        }
    }
    return out;
}

}  // namespace mc::util
