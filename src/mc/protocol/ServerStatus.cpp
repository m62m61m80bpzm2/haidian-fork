// from net/minecraft/protocol/status/ServerStatus.java CODEC (see header).
#include "mc/protocol/ServerStatus.hpp"

#include <cstdio>
#include <string>

namespace mc::protocol {

std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                // GSON escapes < 0x20 as \u00XX; passes UTF-8 bytes through
                // (disableHtmlEscaping keeps >= 0x7f bytes untouched).
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string build_status_json(const ServerStatusData& status) {
    // Field order follows ServerStatus.CODEC (RecordCodecBuilder group).
    // Optional-with-default fields (sample, enforcesSecureChat, favicon) are
    // omitted when empty — vanilla GSON does not emit them for the defaults,
    // verified byte-level against a vanilla 26.2 server (capture diff).
    // description: vanilla 26.2 emits a plain JSON string (Component
    // serialization collapses plain-text components to bare strings);
    // verified byte-level against a vanilla 26.2 server (capture diff).
    std::string json = "{";
    json += "\"description\":\"" + json_escape(status.description_text) + "\"";
    json += ",\"players\":{\"max\":" + std::to_string(status.max_players);
    json += ",\"online\":" + std::to_string(status.online_players) + "}";
    json += ",\"version\":{\"name\":\"" + json_escape(status.version_name) + "\"";
    json += ",\"protocol\":" + std::to_string(status.protocol_version) + "}";
    json += "}";
    return json;
}

}  // namespace mc::protocol
