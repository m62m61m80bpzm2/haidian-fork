#pragma once
// from net/minecraft/protocol/status/ServerStatus.java (record + CODEC)
//   + net/minecraft/server/MinecraftServer.java buildServerStatus()
//
// The status response JSON is produced by
// ByteBufCodecs.lenientJson(32767).apply(fromCodec(ServerStatus.CODEC)):
// GSON (disableHtmlEscaping) serialisation of the codec-recorded object.
// Codec field order (RecordCodecBuilder group): description, players, version,
// favicon, enforcesSecureChat. Optional-with-default fields are always written
// (DFU optionalFieldOf(name, default) encodes unconditionally), so vanilla's
// JSON always carries players.sample and enforcesSecureChat. favicon is only
// written when present (lenientOptionalFieldOf without default) — P1-b omits it.
//
// P1-b hand-writes the JSON (plan: 轻量手写拼接); field names must match
// ServerStatus.CODEC exactly. Component nullToEmpty(motd) serialises a plain
// literal as {"text": "<motd>"} (ComponentSerializationCompact: a bare string
// codec writes {"text": ...}).

#include <string>

namespace mc::protocol {

struct ServerStatusData {
    std::string description_text;   // Component.nullToEmpty(motd)
    int max_players = 20;           // ServerStatus.Players.max
    int online_players = 0;         // ServerStatus.Players.online
    std::string version_name;       // SharedConstants.getCurrentVersion().name()
    int protocol_version = 776;     // WorldVersion.protocolVersion()
    bool enforces_secure_chat = true;  // MinecraftServer.enforceSecureProfile()
};

// Builds the full status JSON in codec field order:
// {"description":{"text":...},"players":{"max":N,"online":N,"sample":[]},
//  "version":{"name":...,"protocol":N},"enforcesSecureChat":bool}
std::string build_status_json(const ServerStatusData& status);

// Minimal JSON string escaper (GSON behaviour for the characters that can
// appear in a server motd: quote, backslash and the C0 control range).
std::string json_escape(const std::string& text);

}  // namespace mc::protocol
