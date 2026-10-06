// from net/minecraft/network/ConnectionProtocol.java + the listener classes
// listed in ConnectionState.hpp (see file header there for the java sources
// of each edge).
#include "mc/protocol/ConnectionState.hpp"

#include <array>

namespace mc::protocol {

namespace {

struct Edge {
    ConnectionState from;
    ConnectionState to;
};

// Each row corresponds to one java call site:
//  - HANDSHAKE->STATUS / HANDSHAKE->LOGIN:
//      ServerHandshakePacketListenerImpl.handleIntention / beginLogin
//  - LOGIN->CONFIGURATION:
//      ServerLoginPacketListenerImpl.handleLoginAcknowledgement
//  - CONFIGURATION->PLAY:
//      ServerConfigurationPacketListenerImpl.handleConfigurationFinished
//      (GameProtocols.CLIENTBOUND_TEMPLATE.bind(...) switch)
constexpr std::array<Edge, 4> kEdges{{
    {ConnectionState::HANDSHAKE, ConnectionState::STATUS},
    {ConnectionState::HANDSHAKE, ConnectionState::LOGIN},
    {ConnectionState::LOGIN, ConnectionState::CONFIGURATION},
    {ConnectionState::CONFIGURATION, ConnectionState::PLAY},
}};

}  // namespace

bool can_transition(ConnectionState from, ConnectionState to) {
    for (const Edge& e : kEdges) {
        if (e.from == from && e.to == to) {
            return true;
        }
    }
    return false;
}

}  // namespace mc::protocol
