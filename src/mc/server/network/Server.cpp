// see Server.hpp. The per-connection dispatch mirrors Connection#channelRead:
// frames -> packet (id, body) -> the listener of the current protocol state.
#include "mc/server/network/Server.hpp"

#include <spdlog/spdlog.h>

#include <exception>
#include <optional>
#include <utility>

#include "mc/network/ByteBuffer.hpp"
#include "mc/protocol/ConnectionState.hpp"
#include "mc/protocol/PacketCodec.hpp"
#include "mc/protocol/ServerStatus.hpp"
#include "mc/server/network/ServerConfigurationPacketListener.hpp"
#include "mc/server/network/ServerConnection.hpp"
#include "mc/server/network/ServerHandshakePacketListener.hpp"
#include "mc/server/network/ServerLoginPacketListener.hpp"

namespace mc::server::network {

Server::Server(ServerConfig config) : config_(std::move(config)) {}

void Server::handle_connection(asio::ip::tcp::socket socket) {
    auto connection = std::make_shared<ServerConnection>(std::move(socket));
    // netty multiplexes connections on its event loop; P1-b uses one thread
    // per connection so the listener code stays linear and 1:1. Threads block
    // on reads; shutdown() breaks them by closing the sockets.
    std::thread worker([this, connection]() { connection_loop(connection); });
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_.push_back(connection);
        workers_.push_back(std::move(worker));
    }
    spdlog::info("accepted connection from {}", connection->peer());
}

void Server::shutdown() {
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& connection : connections_) {
            connection->close();  // wakes blocking reads
        }
        workers = std::move(workers_);
        workers_.clear();
        connections_.clear();
    }
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void Server::connection_loop(const std::shared_ptr<ServerConnection>& connection) {
    // Listener set — java constructs each listener when the protocol switches
    // (ServerHandshakePacketListenerImpl.handleIntention /
    // ServerLoginPacketListenerImpl.handleLoginAcknowledgement); the C++ loop
    // owns them up front and routes by connection state instead.
    ServerHandshakePacketListenerImpl handshake(*this, *connection);
    ServerConfigurationPacketListenerImpl configuration(*this, *connection, "", mc::util::UUID{});
    ServerLoginPacketListenerImpl login(*this, *connection, configuration);

    std::optional<protocol::ServerStatusData> status_data;
    std::optional<ServerStatusPacketListenerImpl> status;

    while (connection->connected()) {
        std::optional<std::pair<int32_t, mc::network::ByteBuffer>> packet = connection->read_packet();
        if (!packet.has_value()) {
            spdlog::info("{}: connection closed", connection->peer());
            break;
        }
        auto [packet_id, body] = std::move(*packet);

        try {
            switch (connection->state()) {
                case protocol::ConnectionState::HANDSHAKE:
                    if (packet_id != protocol::S_INTENTION) {
                        // java: the handshake protocol registers exactly one packet
                        spdlog::warn("{}: expected intention packet, got id {}", connection->peer(),
                                     packet_id);
                        connection->close();
                        continue;
                    }
                    handshake.handle_intention(protocol::decode_handshake(body));
                    break;

                case protocol::ConnectionState::STATUS:
                    if (!status.has_value()) {
                        // java: new ServerStatusPacketListenerImpl(server.getStatus(), connection)
                        status_data = protocol::ServerStatusData{
                            config().motd,
                            config().max_players,
                            0,  // player list is empty in P1-b
                            config().version_name,
                            config().protocol_version,
                            config().enforce_secure_profile,
                        };
                        status.emplace(*status_data, *connection);
                    }
                    status->handle_packet(packet_id, body);  // pong closes the connection
                    break;

                case protocol::ConnectionState::LOGIN:
                    login.handle_packet(packet_id, body);
                    break;

                case protocol::ConnectionState::CONFIGURATION:
                    configuration.handle_packet(packet_id, body);
                    break;

                case protocol::ConnectionState::PLAY:
                    // handle_configuration_finished set this state and logged the
                    // boundary; P1-b has no play protocol.
                    connection->close();
                    continue;
            }
        } catch (const std::exception& e) {
            // java: DecoderException / Validate.validState IllegalStateException
            // -> exception pipeline -> channel close
            spdlog::warn("{}: protocol error: {}", connection->peer(), e.what());
            connection->close();
        }
    }

    connection->close();
    spdlog::info("{}: session ended", connection->peer());
}

}  // namespace mc::server::network
