#pragma once
// from net/minecraft/server/network/ServerLoginPacketListenerImpl.java
// (offline-mode path; encryption KEY/AUTHENTICATING states unreachable with
// online-mode=false and no singleplayer profile)
//
// java State enum (L261-270) reduced to the offline-reachable subset:
//   HELLO -> (offline profile) VERIFYING -> (tick) PROTOCOL_SWITCHING -> ACCEPTED
// Wire timeline (verifyLoginAndFinishConnectionSetup L140-163):
//   1. ClientboundLoginCompressionPacket(threshold) — sent UNCOMPRESSED;
//      Connection.setupCompression(threshold, true) runs when the packet has
//      been flushed (PacketSendListener.thenRun) -> everything after,
//      including Login Finished, is compression-framed.
//   2. ClientboundLoginFinishedPacket(profile, sessionId).
// handleLoginAcknowledgement (L241-249): valid only in PROTOCOL_SWITCHING;
//   switches to CONFIGURATION and calls startConfiguration() on the new
//   ServerConfigurationPacketListenerImpl.

#include <string>

#include "mc/network/ByteBuffer.hpp"
#include "mc/util/UUID.hpp"

namespace mc::server::network {

class Server;
class ServerConnection;
class ServerConfigurationPacketListenerImpl;

class ServerLoginPacketListenerImpl {
public:
    // java ServerLoginPacketListenerImpl.State — offline-reachable subset
    enum class State { HELLO, VERIFYING, PROTOCOL_SWITCHING, ACCEPTED };

    ServerLoginPacketListenerImpl(Server& server, ServerConnection& connection,
                                  ServerConfigurationPacketListenerImpl& configuration_listener);

    // Dispatches one LOGIN-state serverbound packet; false = stop the loop.
    bool handle_packet(int32_t packet_id, mc::network::ByteBuffer& body);

    State state() const { return state_; }

private:
    // java: handleHello — Validate.validState(state == HELLO);
    //   Validate.validState(StringUtil.isValidPlayerName(name)); then the
    //   offline branch of the usesAuthentication check.
    void handle_hello(mc::network::ByteBuffer& body);

    // java: startClientVerification — stores the profile, state = VERIFYING.
    void start_client_verification(const std::string& name, const mc::util::UUID& id);

    // java: verifyLoginAndFinishConnectionSetup (runs on the next tick in
    // java; inline here — same wire order). Sends Set Compression, enables
    // compression, then Login Finished.
    void verify_login_and_finish_connection_setup();

    // java: finishLoginAndWaitForClient — state = PROTOCOL_SWITCHING;
    //   send(new ClientboundLoginFinishedPacket(profile, sessionId)).
    void finish_login_and_wait_for_client();

    // java: handleLoginAcknowledgement — Validate.validState(
    //   state == PROTOCOL_SWITCHING); switch to CONFIGURATION;
    //   configPacketListener.startConfiguration(); state = ACCEPTED.
    void handle_login_acknowledgement();

    void disconnect(const std::string& reason_json, const std::string& log_reason);

    Server& server_;
    ServerConnection& connection_;
    ServerConfigurationPacketListenerImpl& configuration_listener_;
    State state_ = State::HELLO;
    std::string requested_username_;
    mc::util::UUID profile_id_;
    bool transferred_ = false;
};

}  // namespace mc::server::network
