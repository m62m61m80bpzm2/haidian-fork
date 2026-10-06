// 1:1 translation of net/minecraft/server/network/ServerHandshakePacketListenerImpl.java
//   + net/minecraft/server/network/ServerStatusPacketListenerImpl.java
// (java file paths cited at each member; see the header for the flow).
#include "mc/server/network/ServerHandshakePacketListener.hpp"

#include <spdlog/spdlog.h>

#include "mc/core/UUIDUtil.hpp"
#include "mc/protocol/ConnectionState.hpp"
#include "mc/protocol/PacketCodec.hpp"
#include "mc/server/network/Server.hpp"
#include "mc/server/network/ServerConnection.hpp"

namespace mc::server::network {

// ---------------------------------------------------------------------------
// ServerHandshakePacketListenerImpl
// ---------------------------------------------------------------------------

ServerHandshakePacketListenerImpl::ServerHandshakePacketListenerImpl(Server& server,
                                                                     ServerConnection& connection)
    : server_(server), connection_(connection) {}

void ServerHandshakePacketListenerImpl::handle_intention(const protocol::Handshake& packet) {
    // java handleIntention switch (packet.intention())
    switch (packet.intent) {
        case 2:  // ClientIntent.LOGIN
            begin_login(packet, /*transfer=*/false);
            break;
        case 1:  // ClientIntent.STATUS
            // java: setupOutboundProtocol(StatusProtocols.CLIENTBOUND);
            //       setupInboundProtocol(StatusProtocols.SERVERBOUND, new ServerStatusPacketListenerImpl(...))
            connection_.set_state(protocol::ConnectionState::STATUS);
            spdlog::info("{}: intention STATUS (protocol {})",
                         connection_.peer(), packet.protocol_version);
            break;
        case 3:  // ClientIntent.TRANSFER
            // java: !server.acceptsTransfers() (vanilla default) -> login disconnect
            spdlog::info("{}: intention TRANSFER (transfers disabled)", connection_.peer());
            disconnect_with_login_reason("{\"translate\":\"multiplayer.disconnect.transfers_disabled\"}");
            break;
        default:
            // java: UnsupportedOperationException("Invalid intention " + id)
            // (already rejected by decode_handshake; unreachable here)
            disconnect_with_login_reason("{\"translate\":\"multiplayer.disconnect.transfers_disabled\"}");
            break;
    }
}

void ServerHandshakePacketListenerImpl::begin_login(const protocol::Handshake& packet,
                                                    bool transfer) {
    // java: setupOutboundProtocol(LoginProtocols.CLIENTBOUND)
    (void)transfer;
    if (packet.protocol_version != server_.config().protocol_version) {
        // java: < 754 -> outdated_client, else incompatible
        const char* key = packet.protocol_version < 754 ? "multiplayer.disconnect.outdated_client"
                                                        : "multiplayer.disconnect.incompatible";
        spdlog::info("{}: outdated client (protocol {}, expected {})", connection_.peer(),
                     packet.protocol_version, server_.config().protocol_version);
        disconnect_with_login_reason("{\"translate\":\"" + std::string(key) +
                                     "\",\"with\":[\"" + server_.config().version_name + "\"]}");
        return;
    }
    // java: setupInboundProtocol(LoginProtocols.SERVERBOUND,
    //        new ServerLoginPacketListenerImpl(server, connection, transfer))
    connection_.set_state(protocol::ConnectionState::LOGIN);
    spdlog::info("{}: intention LOGIN (protocol {})", connection_.peer(), packet.protocol_version);
}

void ServerHandshakePacketListenerImpl::disconnect_with_login_reason(
    const std::string& reason_json) {
    // java: connection.send(new ClientboundLoginDisconnectPacket(reason)); disconnect(reason)
    (void)connection_.send_packet(protocol::encode_login_disconnect(reason_json));
    connection_.close();
}

// ---------------------------------------------------------------------------
// ServerStatusPacketListenerImpl
// ---------------------------------------------------------------------------

ServerStatusPacketListenerImpl::ServerStatusPacketListenerImpl(
    const protocol::ServerStatusData& status, ServerConnection& connection)
    : status_(status), connection_(connection) {}

bool ServerStatusPacketListenerImpl::handle_packet(int32_t packet_id,
                                                   mc::network::ByteBuffer& body) {
    switch (packet_id) {
        case protocol::S_STATUS_REQUEST: {
            // java: if (hasRequestedStatus) disconnect(DISCONNECT_REASON);
            //       else { hasRequestedStatus = true; send(new ClientboundStatusResponsePacket(status)); }
            if (has_requested_status_) {
                connection_.close();
                return false;
            }
            has_requested_status_ = true;
            const std::string json = protocol::build_status_json(status_);
            spdlog::info("{}: status request -> response ({} bytes)",
                         connection_.peer(), json.size());
            if (!connection_.send_packet(protocol::encode_status_response(json))) {
                return false;
            }
            return true;
        }
        case protocol::S_PING_REQUEST: {
            // java: send(new ClientboundPongResponsePacket(packet.getTime()));
            //       disconnect(DISCONNECT_REASON);
            const protocol::PingRequest ping = protocol::decode_ping_request(body);
            (void)connection_.send_packet(protocol::encode_pong_response(ping.time));
            spdlog::info("{}: ping {} -> pong, closing", connection_.peer(), ping.time);
            connection_.close();
            return false;
        }
        default:
            // java: unknown id -> IdDispatchCodec DecoderException -> channel close
            spdlog::warn("{}: unknown status packet id {}", connection_.peer(), packet_id);
            connection_.close();
            return false;
    }
}

}  // namespace mc::server::network
