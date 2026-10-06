#pragma once
// from net/minecraft/server/network/ServerHandshakePacketListenerImpl.java
//   + net/minecraft/server/network/ServerStatusPacketListenerImpl.java
// (1:1 translation; offline-mode only, transfer rejected)
//
// java handleIntention switch:
//   LOGIN    -> beginLogin(packet, transfer=false)
//   STATUS   -> switch outbound+inbound to STATUS, reply to status requests
//   TRANSFER -> vanilla default acceptsTransfers()=false: switch to LOGIN
//               protocol, send ClientboundLoginDisconnectPacket(
//               multiplayer.disconnect.transfers_disabled), disconnect
// beginLogin: protocolVersion != 776 -> ClientboundLoginDisconnectPacket
//   (outdated_client < 754 / incompatible otherwise), disconnect;
//   else -> ServerLoginPacketListenerImpl.
//
// The status listener handles SERVERBOUND_STATUS_REQUEST (reply once) and
// SERVERBOUND_PING_REQUEST (pong + disconnect "multiplayer.status.request_handled").

#include <cstdint>

#include "mc/network/ByteBuffer.hpp"
#include "mc/protocol/PacketCodec.hpp"
#include "mc/protocol/ServerStatus.hpp"

namespace mc::server::network {

class Server;
class ServerConnection;

class ServerHandshakePacketListenerImpl {
public:
    ServerHandshakePacketListenerImpl(Server& server, ServerConnection& connection);

    // java: handleIntention — routes by intent and switches connection state.
    void handle_intention(const protocol::Handshake& packet);

private:
    void begin_login(const protocol::Handshake& packet, bool transfer);
    void disconnect_with_login_reason(const std::string& reason_json);

    Server& server_;
    ServerConnection& connection_;
};

class ServerStatusPacketListenerImpl {
public:
    ServerStatusPacketListenerImpl(const protocol::ServerStatusData& status,
                                   ServerConnection& connection);

    // java: handleStatusRequest / handlePingRequest. Returns false once the
    // connection is finished (pong sent -> disconnect).
    bool handle_packet(int32_t packet_id, mc::network::ByteBuffer& body);

private:
    protocol::ServerStatusData status_;
    ServerConnection& connection_;
    bool has_requested_status_ = false;
};

}  // namespace mc::server::network
