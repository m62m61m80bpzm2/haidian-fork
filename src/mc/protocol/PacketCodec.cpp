// Wire codecs declared in PacketCodec.hpp — 1:1 translations of the java
// STREAM_CODEC member functions (each java source file named at the call site).
#include "mc/protocol/PacketCodec.hpp"

namespace mc::protocol {

using network::ByteBuffer;

// ---------------------------------------------------------------------------
// handshake
// ---------------------------------------------------------------------------

// ClientIntentionPacket(FriendlyByteBuf input):
//   readVarInt, readUtf(255), readUnsignedShort, ClientIntent.byId(readVarInt)
Handshake decode_handshake(ByteBuffer& buf) {
    Handshake packet;
    packet.protocol_version = buf.read_varint();
    packet.host = buf.read_utf(255);
    packet.port = buf.read_u16();
    packet.intent = buf.read_varint();
    if (packet.intent < 1 || packet.intent > 3) {
        // java: ClientIntent.byId throws
        // IllegalArgumentException("Unknown connection intent: " + id)
        throw network::McProtocolError("Unknown connection intent: " + std::to_string(packet.intent));
    }
    return packet;
}

// ---------------------------------------------------------------------------
// status
// ---------------------------------------------------------------------------

// ServerboundPingRequestPacket(ByteBuf input): this.time = input.readLong()
PingRequest decode_ping_request(ByteBuffer& buf) {
    PingRequest packet;
    packet.time = buf.read_long();
    return packet;
}

// ClientboundStatusResponsePacket wire bytes: varint(id) + utf(json)
network::ByteBuffer encode_status_response(const std::string& status_json) {
    ByteBuffer out;
    out.write_varint(C_STATUS_RESPONSE);
    out.write_utf(status_json, 32767);
    return out;
}

// ClientboundPongResponsePacket.write: writeLong(time)
network::ByteBuffer encode_pong_response(int64_t time) {
    ByteBuffer out;
    out.write_varint(C_PONG_RESPONSE);
    out.write_long(time);
    return out;
}

// ---------------------------------------------------------------------------
// login
// ---------------------------------------------------------------------------

// ServerboundHelloPacket(FriendlyByteBuf input):
//   readUtf(16), readUUID()
LoginHello decode_login_hello(ByteBuffer& buf) {
    LoginHello packet;
    packet.name = buf.read_utf(16);
    packet.profile_id = buf.read_uuid();
    return packet;
}

// ClientboundLoginFinishedPacket STREAM_CODEC (StreamCodec.composite):
//   GAME_PROFILE (uuid, utf16 name, properties) then UUID sessionId.
// The offline server profile carries no properties -> empty list.
network::ByteBuffer encode_login_finished(const util::UUID& id, const std::string& name) {
    ByteBuffer out;
    out.write_varint(C_LOGIN_FINISHED);
    out.write_uuid(id);                       // GameProfile.id
    out.write_utf(name, 16);                  // GameProfile.name
    out.write_varint(0);                      // property count (max 16)
    out.write_uuid(id);                       // sessionId (server.getConnection().getSessionId())
    return out;
}

// ClientboundLoginCompressionPacket.write: writeVarInt(threshold)
network::ByteBuffer encode_login_compression(int32_t threshold) {
    ByteBuffer out;
    out.write_varint(C_LOGIN_COMPRESSION);
    out.write_varint(threshold);
    return out;
}

// ClientboundLoginDisconnectPacket: utf JSON component (lenientJson(262144))
network::ByteBuffer encode_login_disconnect(const std::string& reason_json) {
    ByteBuffer out;
    out.write_varint(C_LOGIN_DISCONNECT);
    out.write_utf(reason_json, 262144);
    return out;
}

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------

// ServerboundSelectKnownPacks: ByteBufCodecs.list(64) of KnownPack.STREAM_CODEC
//   (utf namespace, utf id, utf version). Count is a VarInt.
std::vector<KnownPack> decode_select_known_packs(ByteBuffer& buf) {
    const int32_t count = buf.read_varint();
    if (count < 0 || count > 64) {
        // java: ByteBufCodecs.readCount(buf, 64) enforces the cap
        throw network::McProtocolError("Known pack list too big: " + std::to_string(count));
    }
    std::vector<KnownPack> packs;
    packs.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
        KnownPack pack;
        pack.namespace_id = buf.read_utf(32767);
        pack.id = buf.read_utf(32767);
        pack.version = buf.read_utf(32767);
        packs.push_back(std::move(pack));
    }
    return packs;
}

// ClientboundSelectKnownPacks (server -> client): ByteBufCodecs.list() of
// KnownPack.STREAM_CODEC (no 64 cap on the clientbound list).
network::ByteBuffer encode_select_known_packs(const std::vector<KnownPack>& packs) {
    ByteBuffer out;
    out.write_varint(C_CONFIG_SELECT_KNOWN_PACKS);
    out.write_varint(static_cast<int32_t>(packs.size()));
    for (const KnownPack& pack : packs) {
        out.write_utf(pack.namespace_id, 32767);
        out.write_utf(pack.id, 32767);
        out.write_utf(pack.version, 32767);
    }
    return out;
}

// ClientboundCustomPayloadPacket (CONFIG_STREAM_CODEC) for BrandPayload:
//   writeIdentifier(type.id()) = utf "minecraft:brand", then BrandPayload.write
//   = utf brand.
network::ByteBuffer encode_custom_payload_brand(const std::string& brand) {
    ByteBuffer out;
    out.write_varint(C_CONFIG_CUSTOM_PAYLOAD);
    out.write_utf("minecraft:brand", 32767);  // CustomPacketPayload.writeCap
    out.write_utf(brand, 32767);              // BrandPayload.write
    return out;
}

// ClientboundUpdateEnabledFeaturesPacket.write:
//   writeCollection(features, writeIdentifier) = varint count + utf ids
network::ByteBuffer encode_update_enabled_features(const std::vector<std::string>& features) {
    ByteBuffer out;
    out.write_varint(C_CONFIG_UPDATE_ENABLED_FEATURES);
    out.write_varint(static_cast<int32_t>(features.size()));
    for (const std::string& feature : features) {
        out.write_utf(feature, 32767);
    }
    return out;
}

// ClientboundRegistryDataPacket: utf registry key + list of PackedRegistryEntry.
// P1-b sends the vanilla registry KEY list with EMPTY entries (see
// ServerConfigurationPacketListenerImpl for the minimal-set decision), so each
// entry codec (utf id + optional tag) never fires here.
network::ByteBuffer encode_registry_data(const std::string& registry_key) {
    ByteBuffer out;
    out.write_varint(C_CONFIG_REGISTRY_DATA);
    out.write_utf(registry_key, 32767);  // REGISTRY_KEY_STREAM_CODEC = Identifier.STREAM_CODEC
    out.write_varint(0);                 // entries (ByteBufCodecs.list())
    return out;
}

// ClientboundUpdateTagsPacket with an empty map
// (TagNetworkSerialization.NetworkPayload.write = writeMap; P1-b ships no tags).
network::ByteBuffer encode_update_tags_empty() {
    ByteBuffer out;
    out.write_varint(C_CONFIG_UPDATE_TAGS);
    out.write_varint(0);  // map count
    return out;
}

// ClientboundKeepAlivePacket.write: writeLong(id)
network::ByteBuffer encode_keep_alive(int64_t id) {
    ByteBuffer out;
    out.write_varint(C_CONFIG_KEEP_ALIVE);
    out.write_long(id);
    return out;
}

// ClientboundFinishConfigurationPacket: StreamCodec.unit (id only, no body)
network::ByteBuffer encode_finish_configuration() {
    ByteBuffer out;
    out.write_varint(C_CONFIG_FINISH_CONFIGURATION);
    return out;
}

// ClientboundDisconnectPacket: utf JSON component (lenientJson(262144))
network::ByteBuffer encode_config_disconnect(const std::string& reason_json) {
    ByteBuffer out;
    out.write_varint(C_CONFIG_DISCONNECT);
    out.write_utf(reason_json, 262144);
    return out;
}

}  // namespace mc::protocol
