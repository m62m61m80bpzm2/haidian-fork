#pragma once
// from net/minecraft/network/ConnectionProtocol.java (enum constants
// HANDSHAKING/STATUS/LOGIN/CONFIGURATION/PLAY) + net/minecraft/network/
// Connection.java (setupInboundProtocol / setupOutboundProtocol protocol
// switches driven by the listeners).
//
// The 26.2 server-side state machine (plan §4 P1-b):
//
//   HANDSHAKE --intent=STATUS--> STATUS          (ServerHandshakePacketListenerImpl.handleIntention)
//   HANDSHAKE --intent=LOGIN---> LOGIN           (beginLogin, after protocolVersion == 776 check)
//   HANDSHAKE --intent=TRANSFER-> LOGIN(transfer) — vanilla rejects by default
//   LOGIN --login_acknowledged--> CONFIGURATION  (ServerLoginPacketListenerImpl.handleLoginAcknowledgement)
//   CONFIGURATION --finish_configuration ack--> PLAY (ServerConfigurationPacketListenerImpl.handleConfigurationFinished)
//
// STATUS/CONFIGURATION are terminal in P1-b (PLAY is only a boundary: logged,
// then the connection is closed — play protocol lands in later phases).

#include <cstdint>

namespace mc::protocol {

enum class ConnectionState : uint8_t {
    HANDSHAKE = 0,
    STATUS = 1,
    LOGIN = 2,
    CONFIGURATION = 3,
    PLAY = 4,
};

// Legal server-side transitions of the connection state machine (the explicit
// edge list above). java has no single predicate — each listener validates the
// expected protocol in its handler (Validate.validState) — this table is the
// C++ consolidation used by ServerConnection::set_state.
bool can_transition(ConnectionState from, ConnectionState to);

}  // namespace mc::protocol
