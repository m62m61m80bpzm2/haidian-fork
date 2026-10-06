// P1-b protocol-layer unit tests — same assert style as mc_tests.cpp (P1-a).
// Verified facts (java sources in /home/z/my-project/mcsrc):
//  - MD5 via RFC 1321 test vectors (java.security provider equivalent)
//  - UUIDUtil.createOfflinePlayerUUID: md5("OfflinePlayer:"+name), v3 + IETF
//    variant bits set
//  - CompressionCodec: zlib (RFC 1950) round trip + java error texts
//  - PacketCodec: wire encodings byte-checked against the java STREAM_CODEC
//  - Connection state machine: transition table
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "mc/core/UUIDUtil.hpp"
#include "mc/nbt/Nbt.hpp"
#include "mc/network/ByteBuffer.hpp"
#include "mc/protocol/ConnectionState.hpp"
#include "mc/protocol/PacketCodec.hpp"
#include "mc/protocol/ServerStatus.hpp"
#include "mc/server/network/CompressionCodec.hpp"
#include "mc/server/level/ClientInformation.hpp"
#include "mc/util/Md5.hpp"

namespace {

int g_pass = 0;
int g_total = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    ++g_total;
    if (ok) {
        ++g_pass;
        std::cout << "[PASS] " << name << "\n";
    } else {
        std::cout << "[FAIL] " << name << (detail.empty() ? "" : " — " + detail) << "\n";
    }
}

std::string hex(const std::vector<uint8_t>& bytes) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const uint8_t b : bytes) {
        out += kDigits[b >> 4];
        out += kDigits[b & 0x0f];
    }
    return out;
}

// ---------------------------------------------------------------------------
void test_md5() {
    // RFC 1321 appendix A.5 test suite
    {
        const auto d = mc::util::md5(nullptr, 0);
        check(hex(std::vector<uint8_t>(d.begin(), d.end())) == "d41d8cd98f00b204e9800998ecf8427e",
              "md5 empty = d41d8cd9...");
    }
    {
        const char* s = "a";
        const auto d = mc::util::md5(reinterpret_cast<const uint8_t*>(s), 1);
        check(hex(std::vector<uint8_t>(d.begin(), d.end())) == "0cc175b9c0f1b6a831c399e269772661",
              "md5 \"a\" = 0cc175b9...");
    }
    {
        const char* s = "abc";
        const auto d = mc::util::md5(reinterpret_cast<const uint8_t*>(s), 3);
        check(hex(std::vector<uint8_t>(d.begin(), d.end())) == "900150983cd24fb0d6963f7d28e17f72",
              "md5 \"abc\" = 90015098...");
    }
    {
        const char* s = "message digest";
        const auto d = mc::util::md5(reinterpret_cast<const uint8_t*>(s), 14);
        check(hex(std::vector<uint8_t>(d.begin(), d.end())) == "f96b697d7cb7938d525a2f31aaf161d0",
              "md5 \"message digest\"");
    }
    {
        const char* s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        const auto d = mc::util::md5(reinterpret_cast<const uint8_t*>(s), 62);
        check(hex(std::vector<uint8_t>(d.begin(), d.end())) == "d174ab98d277d9f5a5611c2c9f419d9f",
              "md5 alphabet+digits");
    }
    {
        // 56-byte input forces the two-block padding path (rest+1 == 57 > 56)
        const char* s = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
        const auto d = mc::util::md5(reinterpret_cast<const uint8_t*>(s), 80);
        check(hex(std::vector<uint8_t>(d.begin(), d.end())) == "57edf4a22be3c955ac49da2e2107b67a",
              "md5 80-byte two-block padding");
    }
}

void test_offline_uuid() {
    // UUIDUtil.createOfflinePlayerUUID("OfflinePlayer:"+name) -> v3, variant 10
    {
        const mc::util::UUID u = mc::core::uuid_util::create_offline_player_uuid("Probe");
        const std::string s = mc::core::uuid_util::to_string(u);
        // version nibble must be 3, variant nibble must be 8/9/a/b
        check(s.size() == 36 && s[14] == '3' && (s[19] == '8' || s[19] == '9' || s[19] == 'a' || s[19] == 'b'),
              "offline uuid v3/variant bits", s);
        // determinism + name sensitivity
        const mc::util::UUID u2 = mc::core::uuid_util::create_offline_player_uuid("Probe");
        const mc::util::UUID u3 = mc::core::uuid_util::create_offline_player_uuid("probe");
        check(u == u2, "offline uuid deterministic");
        check(!(u == u3), "offline uuid name-sensitive");
    }
    // cross-check the md5 payload itself for one name (independent computation)
    {
        const std::string seed = "OfflinePlayer:Probe";
        auto d = mc::util::md5(reinterpret_cast<const uint8_t*>(seed.data()), seed.size());
        const mc::util::UUID u = mc::core::uuid_util::create_offline_player_uuid("Probe");
        uint64_t most = 0, least = 0;
        for (int i = 0; i < 8; ++i) {
            most = (most << 8) | d[i];
        }
        for (int i = 8; i < 16; ++i) {
            least = (least << 8) | d[i];
        }
        // apply the JDK21 UUID.nameUUIDFromBytes mask to the raw digest:
        //   md5[6] = (md5[6] & 0x0f) | 0x30   version 3
        //   md5[8] = (md5[8] & 0x3f) | 0x80   IETF variant
        d[6] = static_cast<uint8_t>((d[6] & 0x0f) | 0x30);
        d[8] = static_cast<uint8_t>((d[8] & 0x3f) | 0x80);
        uint64_t most_masked = 0;
        uint64_t least_masked = 0;
        for (int i = 0; i < 8; ++i) {
            most_masked = (most_masked << 8) | d[i];
        }
        for (int i = 8; i < 16; ++i) {
            least_masked = (least_masked << 8) | d[i];
        }
        (void)most;
        (void)least;
        check(u.most() == most_masked && u.least() == least_masked,
              "offline uuid == masked md5 digest");
    }
}

void test_compression() {
    using mc::server::network::CompressionCodec;
    // round trips across the 256-byte threshold boundary
    for (const size_t size : {size_t(0), size_t(1), size_t(128), size_t(255), size_t(256),
                              size_t(257), size_t(4096), size_t(1 << 20)}) {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>((i * 31 + 7) & 0xff);
        }
        const std::vector<uint8_t> compressed = CompressionCodec::compress(data.data(), data.size());
        const std::vector<uint8_t> round = CompressionCodec::decompress(
            compressed.data(), compressed.size(), data.size());
        check(round == data, "compress/decompress round trip size=" + std::to_string(size));
        // zlib header present (RFC 1950: 0x78 mask, FC checksum bits)
        if (!compressed.empty()) {
            check((compressed[0] & 0x0f) == 8, "zlib CM=8 size=" + std::to_string(size));
            check(((compressed[0] << 8 | compressed[1]) % 31) == 0, "zlib FCHECK size=" + std::to_string(size));
        }
    }
    // declared != actual -> java DecoderException text
    bool threw = false;
    try {
        const std::vector<uint8_t> compressed = CompressionCodec::compress(
            reinterpret_cast<const uint8_t*>("hello world, hello world, hello world"), 34);
        CompressionCodec::decompress(compressed.data(), compressed.size(), 100);
    } catch (const std::exception& e) {
        threw = std::string(e.what()).find("is does not match declared size") != std::string::npos;
    }
    check(threw, "short inflate -> java DecoderException text");

    // 8 MiB cap (CompressionEncoder IllegalArgumentException)
    bool too_big = false;
    try {
        std::vector<uint8_t> big(CompressionCodec::kMaxUncompressedLength + 1, 0x41);
        CompressionCodec::compress(big.data(), big.size());
    } catch (const std::exception& e) {
        too_big = std::string(e.what()).find("Packet too big") != std::string::npos;
    }
    check(too_big, ">8MiB -> Packet too big");
}

void test_packet_codec() {
    using namespace mc::protocol;
    using mc::network::ByteBuffer;

    // handshake decode: ClientIntentionPacket write = varint + utf(255) + u16 + varint
    {
        ByteBuffer in;
        in.write_varint(776);
        in.write_utf("127.0.0.1", 255);
        in.write_u16(25565);
        in.write_varint(2);
        ByteBuffer payload = ByteBuffer(std::move(in));
        payload.set_read_pos(0);
        const Handshake hs = decode_handshake(payload);
        check(hs.protocol_version == 776 && hs.host == "127.0.0.1" && hs.port == 25565 &&
                  hs.intent == 2,
              "handshake decode");
    }
    // bad intent rejected (ClientIntent.byId IllegalArgumentException)
    {
        ByteBuffer in;
        in.write_varint(776);
        in.write_utf("h", 255);
        in.write_u16(1);
        in.write_varint(7);
        ByteBuffer payload = ByteBuffer(std::move(in));
        payload.set_read_pos(0);
        bool threw = false;
        try {
            decode_handshake(payload);
        } catch (const std::exception&) {
            threw = true;
        }
        check(threw, "handshake bad intent throws");
    }

    // login hello: utf(16) + uuid
    {
        const mc::util::UUID u(0x0011223344556677ull, 0x8899aabbccddeee0ull);
        ByteBuffer in;
        in.write_utf("Probe", 16);
        in.write_uuid(u);
        ByteBuffer payload = ByteBuffer(std::move(in));
        payload.set_read_pos(0);
        const LoginHello hello = decode_login_hello(payload);
        check(hello.name == "Probe" && hello.profile_id == u, "login hello decode");
    }

    // login finished wire bytes: varint(id=2) uuid utf16 varint(0) uuid
    {
        const mc::util::UUID u = mc::core::uuid_util::create_offline_player_uuid("Probe");
        const ByteBuffer out = encode_login_finished(u, "Probe");
        ByteBuffer in = ByteBuffer(out.data());
        check(in.read_varint() == C_LOGIN_FINISHED, "login finished id");
        check(in.read_uuid() == u, "login finished uuid");
        check(in.read_utf(16) == "Probe", "login finished name");
        check(in.read_varint() == 0, "login finished zero properties");
        check(in.read_uuid() == u, "login finished session id");
        check(in.readable_bytes() == 0, "login finished no trailing bytes");
    }

    // login compression: varint(id=3) varint(threshold)
    {
        const ByteBuffer out = encode_login_compression(256);
        ByteBuffer in = ByteBuffer(out.data());
        check(in.read_varint() == C_LOGIN_COMPRESSION && in.read_varint() == 256 &&
                  in.readable_bytes() == 0,
              "login compression encode");
    }

    // status response: varint(id=0) + utf json
    {
        const ByteBuffer out = encode_status_response("{\"x\":1}");
        ByteBuffer in = ByteBuffer(out.data());
        check(in.read_varint() == C_STATUS_RESPONSE && in.read_utf(32767) == "{\"x\":1}",
              "status response encode");
    }

    // pong echo
    {
        const ByteBuffer out = encode_pong_response(12345);
        ByteBuffer in = ByteBuffer(out.data());
        check(in.read_varint() == C_PONG_RESPONSE && in.read_long() == 12345, "pong encode");
    }

    // client information round trip (ClientInformation.read/write)
    {
        mc::server::level::ClientInformation info;
        info.language = "zh_cn";
        info.view_distance = 12;
        info.chat_visibility = 1;
        info.chat_colors = false;
        info.model_customisation = 7;
        info.main_hand = 0;
        info.text_filtering_enabled = true;
        info.allows_listing = true;
        info.particle_status = 2;
        ByteBuffer buf;
        info.write(buf);
        ByteBuffer in = ByteBuffer(buf.data());
        const mc::server::level::ClientInformation back = mc::server::level::ClientInformation::read(in);
        check(back.language == "zh_cn" && back.view_distance == 12 && back.chat_visibility == 1 &&
                  !back.chat_colors && back.model_customisation == 7 && back.main_hand == 0 &&
                  back.text_filtering_enabled && back.allows_listing && back.particle_status == 2,
              "client information round trip");
    }

    // known packs round trip (serverbound decode cap 64)
    {
        ByteBuffer in;
        in.write_varint(2);
        in.write_utf("minecraft", 32767);
        in.write_utf("vanilla", 32767);
        in.write_utf("26.2", 32767);
        in.write_utf("minecraft", 32767);
        in.write_utf("core", 32767);
        in.write_utf("26.2", 32767);
        ByteBuffer payload = ByteBuffer(std::move(in));
        payload.set_read_pos(0);
        const std::vector<KnownPack> packs = decode_select_known_packs(payload);
        check(packs.size() == 2 && packs[0].id == "vanilla" && packs[1].id == "core" &&
                  packs[0].version == "26.2",
              "select known packs decode");
    }

    // select known packs encode → id + count + triples
    {
        const ByteBuffer out = encode_select_known_packs({{"minecraft", "vanilla", "26.2"}});
        ByteBuffer in = ByteBuffer(out.data());
        check(in.read_varint() == C_CONFIG_SELECT_KNOWN_PACKS && in.read_varint() == 1 &&
                  in.read_utf(32767) == "minecraft" && in.read_utf(32767) == "vanilla" &&
                  in.read_utf(32767) == "26.2",
              "select known packs encode");
    }

    // brand payload: id + utf "minecraft:brand" + utf brand
    {
        const ByteBuffer out = encode_custom_payload_brand("mc-cpp");
        ByteBuffer in = ByteBuffer(out.data());
        check(in.read_varint() == C_CONFIG_CUSTOM_PAYLOAD && in.read_utf(32767) == "minecraft:brand" &&
                  in.read_utf(32767) == "mc-cpp",
              "brand payload encode");
    }

    // enabled features + registry data + empty tags
    {
        const ByteBuffer feats = encode_update_enabled_features({"minecraft:a", "minecraft:b"});
        ByteBuffer in = ByteBuffer(feats.data());
        check(in.read_varint() == C_CONFIG_UPDATE_ENABLED_FEATURES && in.read_varint() == 2 &&
                  in.read_utf(32767) == "minecraft:a" && in.read_utf(32767) == "minecraft:b",
              "enabled features encode");

        const ByteBuffer reg = encode_registry_data("minecraft:worldgen/biome");
        ByteBuffer rin = ByteBuffer(reg.data());
        check(rin.read_varint() == C_CONFIG_REGISTRY_DATA &&
                  rin.read_utf(32767) == "minecraft:worldgen/biome" && rin.read_varint() == 0,
              "registry data encode (empty entries)");

        const ByteBuffer tags = encode_update_tags_empty();
        ByteBuffer tin = ByteBuffer(tags.data());
        check(tin.read_varint() == C_CONFIG_UPDATE_TAGS && tin.read_varint() == 0,
              "update tags empty encode");

        check(encode_finish_configuration().data().size() == 1, "finish config is id-only");
    }
}

void test_status_json() {
    using namespace mc::protocol;
    ServerStatusData data;
    data.description_text = "mc-cpp P1";
    data.max_players = 20;
    data.online_players = 0;
    data.version_name = "26.2";
    data.protocol_version = 776;
    data.enforces_secure_chat = true;
    const std::string json = build_status_json(data);
    // field names must match ServerStatus.CODEC; order follows the codec group
    const std::string expected =
        "{\"description\":{\"text\":\"mc-cpp P1\"},\"players\":{\"max\":20,\"online\":0,"
        "\"sample\":[]},\"version\":{\"name\":\"26.2\",\"protocol\":776},"
        "\"enforcesSecureChat\":true}";
    check(json == expected, "status json exact", json);

    // escaping
    check(json_escape("a\"b\\c\nd") == "a\\\"b\\\\c\\nd", "json escaping");
}

void test_state_machine() {
    using mc::protocol::ConnectionState;
    using mc::protocol::can_transition;
    check(can_transition(ConnectionState::HANDSHAKE, ConnectionState::STATUS), "handshake->status");
    check(can_transition(ConnectionState::HANDSHAKE, ConnectionState::LOGIN), "handshake->login");
    check(can_transition(ConnectionState::LOGIN, ConnectionState::CONFIGURATION), "login->configuration");
    check(can_transition(ConnectionState::CONFIGURATION, ConnectionState::PLAY), "configuration->play");
    check(!can_transition(ConnectionState::STATUS, ConnectionState::LOGIN), "status-!->login");
    check(!can_transition(ConnectionState::HANDSHAKE, ConnectionState::PLAY), "handshake-!->play");
    check(!can_transition(ConnectionState::LOGIN, ConnectionState::STATUS), "login-!->status");
}

}  // namespace

int main() {
    test_md5();
    test_offline_uuid();
    test_compression();
    test_packet_codec();
    test_status_json();
    test_state_machine();

    std::cout << "PASS " << g_pass << "/" << g_total << "\n";
    return g_pass == g_total ? 0 : 1;
}
