// 1:1 translation of net/minecraft/server/network/ServerLoginPacketListenerImpl.java
// (offline path; see header). Java method names in comments.
#include "mc/server/network/ServerLoginPacketListener.hpp"

#include <spdlog/spdlog.h>

#include "mc/core/UUIDUtil.hpp"
#include "mc/protocol/ConnectionState.hpp"
#include "mc/protocol/PacketCodec.hpp"
#include "mc/server/network/Server.hpp"
#include "mc/server/network/ServerConfigurationPacketListener.hpp"
#include "mc/server/network/ServerConnection.hpp"

namespace mc::server::network {

namespace {
// java: StringUtil.isValidPlayerName — length <= 16 and all chars in 33..126
bool is_valid_player_name(const std::string& name) {
    if (name.size() > 16) {
        return false;
    }
    for (const char c : name) {
        const int ch = static_cast<unsigned char>(c);
        if (ch <= 32 || ch >= 127) {
            return false;
        }
    }
    return true;
}
}  // namespace

ServerLoginPacketListenerImpl::ServerLoginPacketListenerImpl(
    Server& server, ServerConnection& connection,
    ServerConfigurationPacketListenerImpl& configuration_listener)
    : server_(server),
      connection_(connection),
      configuration_listener_(configuration_listener) {}

bool ServerLoginPacketListenerImpl::handle_packet(int32_t packet_id, mc::network::ByteBuffer& body) {
    switch (packet_id) {
        case protocol::S_LOGIN_HELLO:
            handle_hello(body);
            return connection_.connected();
        case protocol::S_LOGIN_ACKNOWLEDGED:
            handle_login_acknowledgement();
            return connection_.connected();
        case protocol::S_LOGIN_KEY:
        case protocol::S_LOGIN_CUSTOM_QUERY_ANSWER:
        case protocol::S_LOGIN_COOKIE_RESPONSE:
            // java: offline server never sends hello/custom_query/cookie
            // requests, so these arrive only from a misbehaving client;
            // DISCONNECT_UNEXPECTED_QUERY path for the query/cookie ones.
            spdlog::warn("{}: unexpected login packet id {}", connection_.peer(), packet_id);
            disconnect("{\"translate\":\"multiplayer.disconnect.unexpected_query_response\"}",
                       "unexpected query");
            return false;
        default:
            spdlog::warn("{}: unknown login packet id {}", connection_.peer(), packet_id);
            connection_.close();
            return false;
    }
}

void ServerLoginPacketListenerImpl::handle_hello(mc::network::ByteBuffer& body) {
    // decode before state validation so a malformed packet errors like java's
    // decoder (DecoderException) rather than Validate.validState
    const protocol::LoginHello hello = protocol::decode_login_hello(body);

    // java: Validate.validState(this.state == State.HELLO, "Unexpected hello packet")
    if (state_ != State::HELLO) {
        throw std::runtime_error("Unexpected hello packet");
    }
    // java: Validate.validState(StringUtil.isValidPlayerName(packet.name()),
    //                           "Invalid characters in username")
    if (!is_valid_player_name(hello.name)) {
        throw std::runtime_error("Invalid characters in username");
    }

    requested_username_ = hello.name;
    spdlog::info("{}: login start (name=\"{}\", intended profile {})", connection_.peer(),
                 hello.name, mc::core::uuid_util::to_string(hello.profile_id));

    // java: server.usesAuthentication() == false (online-mode=false) ->
    //   startClientVerification(UUIDUtil.createOfflineProfile(requestedUsername))
    start_client_verification(hello.name,
                              mc::core::uuid_util::create_offline_player_uuid(hello.name));
}

void ServerLoginPacketListenerImpl::start_client_verification(const std::string& name,
                                                              const mc::util::UUID& id) {
    requested_username_ = name;
    profile_id_ = id;
    state_ = State::VERIFYING;
    // java defers this to the next tick(); we run it inline (same wire order).
    verify_login_and_finish_connection_setup();
}

void ServerLoginPacketListenerImpl::verify_login_and_finish_connection_setup() {
    // java: canPlayerLogin check (whitelist/ban/full) omitted — P1-b has no
    // player list; the offline profile always passes.

    const int threshold = server_.config().compression_threshold;
    // java: if (server.getCompressionThreshold() >= 0 && !connection.isMemoryConnection()) {
    //   connection.send(new ClientboundLoginCompressionPacket(threshold),
    //     PacketSendListener.thenRun(() -> connection.setupCompression(threshold, true)));
    // }
    // The Set Compression packet itself goes out UNCOMPRESSED; compression is
    // armed the moment it has been written & flushed.
    if (threshold >= 0) {
        if (!connection_.send_packet(protocol::encode_login_compression(threshold))) {
            return;
        }
        connection_.enable_compression(threshold);
        spdlog::info("{}: set compression threshold {}", connection_.peer(), threshold);
    }

    // java: disconnectAllPlayersWithProfile -> WAITING_FOR_DUPE_DISCONNECT
    // (no player list in P1-b: no dupes possible) else
    //   finishLoginAndWaitForClient(profile)
    finish_login_and_wait_for_client();
}

void ServerLoginPacketListenerImpl::finish_login_and_wait_for_client() {
    state_ = State::PROTOCOL_SWITCHING;
    // java: send(new ClientboundLoginFinishedPacket(gameProfile,
    //         server.getConnection().getSessionId()))
    if (!connection_.send_packet(
            protocol::encode_login_finished(profile_id_, requested_username_))) {
        return;
    }
    spdlog::info("{}: login finished (uuid {})", connection_.peer(),
                 mc::core::uuid_util::to_string(profile_id_));
}

void ServerLoginPacketListenerImpl::handle_login_acknowledgement() {
    // java: Validate.validState(this.state == State.PROTOCOL_SWITCHING,
    //                           "Unexpected login acknowledgement packet")
    if (state_ != State::PROTOCOL_SWITCHING) {
        throw std::runtime_error("Unexpected login acknowledgement packet");
    }
    // java: connection.setupOutboundProtocol(ConfigurationProtocols.CLIENTBOUND);
    //       connection.setupInboundProtocol(ConfigurationProtocols.SERVERBOUND,
    //                                       configPacketListener);
    //       configPacketListener.startConfiguration();
    connection_.set_state(protocol::ConnectionState::CONFIGURATION);
    state_ = State::ACCEPTED;
    spdlog::info("{}: login acknowledged -> configuration", connection_.peer());
    // java: CommonListenerCookie.createInitial(authenticatedProfile, transferred)
    // passes the verified profile into the configuration listener.
    configuration_listener_.set_player(requested_username_, profile_id_);
    configuration_listener_.start_configuration();
}

void ServerLoginPacketListenerImpl::disconnect(const std::string& reason_json,
                                               const std::string& log_reason) {
    // java: LOGGER.info("Disconnecting {}: {}", getUserName(), reason); send
    //       ClientboundLoginDisconnectPacket; disconnect.
    spdlog::info("{}: disconnecting ({})", connection_.peer(), log_reason);
    (void)connection_.send_packet(protocol::encode_login_disconnect(reason_json));
    connection_.close();
}

}  // namespace mc::server::network
