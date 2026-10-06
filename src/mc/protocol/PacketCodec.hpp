#pragma once
// Packet ID table + wire codecs for the P1-b server flow, translated from
// net/minecraft/network/protocol/{handshake,status,ping,login,configuration}/
// (decompiled source /home/z/my-project/mcsrc, official Mojang names).
//
// NUMERIC PACKET IDS: 26.2 has no per-packet constants — the wire ids are the
// 0-based REGISTRATION ORDER in the *Protocols classes
// (ProtocolInfoBuilder adds each PacketType to an IdDispatchCodec whose
// Builder.build() assigns `int id = toId.size()` — see
// IdDispatchCodec.Builder.build, L83-96). The registration orders used here
// (java source, read on 2026 rewrite):
//
//  HandshakeProtocols.SERVERBOUND (serverbound):
//    0x00 intention                                   HandshakePacketTypes.CLIENT_INTENTION
//  StatusProtocols.SERVERBOUND (serverbound):
//    0x00 status_request, 0x01 ping_request           (ping types in protocol/ping/PingPacketTypes)
//  StatusProtocols.CLIENTBOUND (clientbound):
//    0x00 status_response, 0x01 pong_response
//  LoginProtocols.SERVERBOUND (serverbound):
//    0x00 hello, 0x01 key, 0x02 custom_query_answer,
//    0x03 login_acknowledged, 0x04 cookie_response
//  LoginProtocols.CLIENTBOUND (clientbound):
//    0x00 login_disconnect, 0x01 hello(=encryption request),
//    0x02 login_finished, 0x03 login_compression,
//    0x04 custom_query, 0x05 cookie_request
//  ConfigurationProtocols.SERVERBOUND (serverbound):
//    0x00 client_information, 0x01 cookie_response, 0x02 custom_payload,
//    0x03 finish_configuration, 0x04 keep_alive, 0x05 pong, 0x06 resource_pack,
//    0x07 select_known_packs, 0x08 custom_click_action, 0x09 accept_code_of_conduct
//  ConfigurationProtocols.CLIENTBOUND (clientbound):
//    0x00 cookie_request, 0x01 custom_payload, 0x02 disconnect,
//    0x03 finish_configuration, 0x04 keep_alive, 0x05 ping, 0x06 reset_chat,
//    0x07 registry_data, 0x08 resource_pack_pop, 0x09 resource_pack_push,
//    0x0a store_cookie, 0x0b transfer, 0x0c update_enabled_features,
//    0x0d update_tags, 0x0e select_known_packs, 0x0f custom_report_details,
//    0x10 server_links, 0x11 clear_dialog, 0x12 show_dialog, 0x13 code_of_conduct
//
// All field codecs are FriendlyByteBuf primitives (see mc/network/ByteBuffer):
// VarInt ids and counts, VarInt-length-prefixed UTF strings at packet level,
// big-endian fixed-width ints.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mc/network/ByteBuffer.hpp"
#include "mc/util/UUID.hpp"

namespace mc::protocol {

// ---------------------------------------------------------------------------
// Packet ID constants (see the registration-order table in this file's header)
// ---------------------------------------------------------------------------

// handshake serverbound
constexpr int32_t S_INTENTION = 0x00;

// status
constexpr int32_t S_STATUS_REQUEST = 0x00;
constexpr int32_t S_PING_REQUEST = 0x01;
constexpr int32_t C_STATUS_RESPONSE = 0x00;
constexpr int32_t C_PONG_RESPONSE = 0x01;

// login serverbound
constexpr int32_t S_LOGIN_HELLO = 0x00;
constexpr int32_t S_LOGIN_KEY = 0x01;
constexpr int32_t S_LOGIN_CUSTOM_QUERY_ANSWER = 0x02;
constexpr int32_t S_LOGIN_ACKNOWLEDGED = 0x03;
constexpr int32_t S_LOGIN_COOKIE_RESPONSE = 0x04;

// login clientbound
constexpr int32_t C_LOGIN_DISCONNECT = 0x00;
constexpr int32_t C_LOGIN_HELLO = 0x01;
constexpr int32_t C_LOGIN_FINISHED = 0x02;
constexpr int32_t C_LOGIN_COMPRESSION = 0x03;
constexpr int32_t C_LOGIN_CUSTOM_QUERY = 0x04;
constexpr int32_t C_LOGIN_COOKIE_REQUEST = 0x05;

// configuration serverbound
constexpr int32_t S_CONFIG_CLIENT_INFORMATION = 0x00;
constexpr int32_t S_CONFIG_COOKIE_RESPONSE = 0x01;
constexpr int32_t S_CONFIG_CUSTOM_PAYLOAD = 0x02;
constexpr int32_t S_CONFIG_FINISH_CONFIGURATION = 0x03;
constexpr int32_t S_CONFIG_KEEP_ALIVE = 0x04;
constexpr int32_t S_CONFIG_PONG = 0x05;
constexpr int32_t S_CONFIG_RESOURCE_PACK = 0x06;
constexpr int32_t S_CONFIG_SELECT_KNOWN_PACKS = 0x07;
constexpr int32_t S_CONFIG_CUSTOM_CLICK_ACTION = 0x08;
constexpr int32_t S_CONFIG_ACCEPT_CODE_OF_CONDUCT = 0x09;

// configuration clientbound
constexpr int32_t C_CONFIG_COOKIE_REQUEST = 0x00;
constexpr int32_t C_CONFIG_CUSTOM_PAYLOAD = 0x01;
constexpr int32_t C_CONFIG_DISCONNECT = 0x02;
constexpr int32_t C_CONFIG_FINISH_CONFIGURATION = 0x03;
constexpr int32_t C_CONFIG_KEEP_ALIVE = 0x04;
constexpr int32_t C_CONFIG_PING = 0x05;
constexpr int32_t C_CONFIG_RESET_CHAT = 0x06;
constexpr int32_t C_CONFIG_REGISTRY_DATA = 0x07;
constexpr int32_t C_CONFIG_RESOURCE_PACK_POP = 0x08;
constexpr int32_t C_CONFIG_RESOURCE_PACK_PUSH = 0x09;
constexpr int32_t C_CONFIG_STORE_COOKIE = 0x0A;
constexpr int32_t C_CONFIG_TRANSFER = 0x0B;
constexpr int32_t C_CONFIG_UPDATE_ENABLED_FEATURES = 0x0C;
constexpr int32_t C_CONFIG_UPDATE_TAGS = 0x0D;
constexpr int32_t C_CONFIG_SELECT_KNOWN_PACKS = 0x0E;
constexpr int32_t C_CONFIG_CUSTOM_REPORT_DETAILS = 0x0F;
constexpr int32_t C_CONFIG_SERVER_LINKS = 0x10;
constexpr int32_t C_CONFIG_CLEAR_DIALOG = 0x11;
constexpr int32_t C_CONFIG_SHOW_DIALOG = 0x12;
constexpr int32_t C_CONFIG_CODE_OF_CONDUCT = 0x13;

// ---------------------------------------------------------------------------
// handshake.ClientIntentionPacket (from ClientIntentionPacket.java)
//   write: varint protocolVersion, utf(<=255) hostName, u16 port, varint intent.id()
// intent ids from ClientIntent.java: 1=STATUS, 2=LOGIN, 3=TRANSFER
// ---------------------------------------------------------------------------
struct Handshake {
    int32_t protocol_version = 0;
    std::string host;
    uint16_t port = 0;
    int32_t intent = 0;
};

Handshake decode_handshake(network::ByteBuffer& buf);

// ---------------------------------------------------------------------------
// status packets
//   ServerboundStatusRequestPacket: unit (empty)
//   ServerboundPingRequestPacket:   i64 time   (from ServerboundPingRequestPacket.java)
//   ClientboundStatusResponsePacket: utf(<=32767) JSON (lenientJson(32767))
//   ClientboundPongResponsePacket:   i64 time
// ---------------------------------------------------------------------------
struct PingRequest {
    int64_t time = 0;
};

PingRequest decode_ping_request(network::ByteBuffer& buf);

// full packet payload: varint(id) + utf json (ClientboundStatusResponsePacket)
network::ByteBuffer encode_status_response(const std::string& status_json);
network::ByteBuffer encode_pong_response(int64_t time);

// ---------------------------------------------------------------------------
// login packets
//   ServerboundHelloPacket: utf(<=16) name, uuid profileId   (from ServerboundHelloPacket.java —
//     26.2 carries an intended profile id uuid; no nullable wrapper)
//   ClientboundLoginCompressionPacket: varint threshold      (from ClientboundLoginCompressionPacket.java)
//   ClientboundLoginFinishedPacket: game_profile + uuid sessionId
//     game_profile (ByteBufCodecs.GAME_PROFILE): uuid id, utf(<=16) name,
//       varint(<=16) property count, per property: utf(<=64) name,
//       utf(<=32767) value, optional utf(<=1024) signature
//     (ByteBufCodecs.GAME_PROFILE_PROPERTIES + UUIDUtil.STREAM_CODEC)
//   ClientboundLoginDisconnectPacket: utf(<=262144) JSON component
//     (ByteBufCodecs.lenientJson(262144))
// ---------------------------------------------------------------------------
struct LoginHello {
    std::string name;
    util::UUID profile_id;
};

LoginHello decode_login_hello(network::ByteBuffer& buf);

struct ProfileProperty {
    std::string name;
    std::string value;
    std::optional<std::string> signature;
};

network::ByteBuffer encode_login_finished(const util::UUID& id, const std::string& name);
network::ByteBuffer encode_login_compression(int32_t threshold);
network::ByteBuffer encode_login_disconnect(const std::string& reason_json);

// ---------------------------------------------------------------------------
// configuration packets
//   ServerboundClientInformationPacket wraps
//   net/minecraft/server/level/ClientInformation — decode lives with the
//   record (mc/server/level/ClientInformation.hpp).
//   ServerboundSelectKnownPacks / ClientboundSelectKnownPacks:
//     varint count (sb max 64, cb unbounded list), per entry: utf namespace,
//     utf id, utf version (KnownPack.STREAM_CODEC)
//   ClientboundCustomPayloadPacket (CONFIG_STREAM_CODEC -> CustomPacketPayload.codec):
//     utf(<=32767) payload type identifier, then the type's codec
//     (BrandPayload: utf brand)
//   ClientboundUpdateEnabledFeaturesPacket:
//     varint count + utf(<=32767) identifier per feature
//   ClientboundRegistryDataPacket:
//     utf(<=32767) registry key identifier, varint count, per entry:
//     utf(<=32767) entry identifier + optional network tag
//     (PackedRegistryEntry.STREAM_CODEC: ByteBufCodecs.TAG.apply(optional) =
//     bool present + NbtIo.writeAnyTag bytes; P1-b sends empty entries)
//   ClientboundUpdateTagsPacket:
//     varint map count, per registry: utf identifier, varint tag count,
//     per tag: utf identifier, varint entry count + varint ids
//     (TagNetworkSerialization.NetworkPayload.write = FriendlyByteBuf.writeMap)
//   ClientboundKeepAlivePacket: i64 id; ServerboundKeepAlivePacket: i64 id
//   ClientboundDisconnectPacket: utf(<=262144) JSON component
// ---------------------------------------------------------------------------
struct KnownPack {
    std::string namespace_id;
    std::string id;
    std::string version;
};

std::vector<KnownPack> decode_select_known_packs(network::ByteBuffer& buf);

// ClientboundSelectKnownPacks (server -> client request): varint count +
// (utf namespace, utf id, utf version) per pack — same KnownPack codec.
network::ByteBuffer encode_select_known_packs(const std::vector<KnownPack>& packs);

network::ByteBuffer encode_custom_payload_brand(const std::string& brand);
network::ByteBuffer encode_update_enabled_features(const std::vector<std::string>& features);
network::ByteBuffer encode_registry_data(const std::string& registry_key);
network::ByteBuffer encode_update_tags_empty();
network::ByteBuffer encode_keep_alive(int64_t id);
network::ByteBuffer encode_finish_configuration();
network::ByteBuffer encode_config_disconnect(const std::string& reason_json);

}  // namespace mc::protocol
