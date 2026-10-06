// P1-b: mc-server entry — spdlog banner + asio async accept loop feeding the
// protocol state machine (handshake -> status/login -> configuration -> PLAY
// boundary). Each accepted connection is handed to
// mc::server::network::Server::handle_connection, which serves it on a
// dedicated thread until the client disconnects or the PLAY boundary is
// reached (play protocol lands in a later phase).
#include <asio.hpp>
#include <spdlog/spdlog.h>

#include <csignal>
#include <exception>
#include <optional>
#include <string>
#include <system_error>

#include "mc/server/network/Server.hpp"
#include "mc/util/Mth.hpp"

namespace {

using asio::ip::tcp;

constexpr unsigned short kDefaultPort = 25565;

// Accept loop: for each accepted connection hand the socket to the Server
// (P1-b: the protocol handlers own the connection from here on).
class AcceptLoop {
public:
    AcceptLoop(asio::io_context& io, unsigned short port, mc::server::network::Server& server)
        : acceptor_(io, tcp::endpoint(tcp::v4(), port)), server_(server) {}

    void start() { doAccept(); }

private:
    void doAccept() {
        socket_.emplace(acceptor_.get_executor());
        acceptor_.async_accept(
            *socket_,
            [this](std::error_code ec) {
                if (ec == asio::error::operation_aborted) {
                    return;  // io_context stopped / acceptor closed — do not re-arm
                }
                if (!ec) {
                    std::error_code ep_ec;
                    const tcp::endpoint ep = socket_->remote_endpoint(ep_ec);
                    if (!ep_ec) {
                        spdlog::info("accepted connection from {}:{}",
                                     ep.address().to_string(), ep.port());
                    } else {
                        spdlog::info("accepted connection (peer endpoint unavailable: {})", ep_ec.message());
                    }
                    server_.handle_connection(std::move(*socket_));
                } else {
                    spdlog::warn("accept failed: {}", ec.message());
                }
                doAccept();
            });
    }

    tcp::acceptor acceptor_;
    mc::server::network::Server& server_;
    std::optional<tcp::socket> socket_;
};

}  // namespace

int main(int argc, char* argv[]) {
    long port_arg = kDefaultPort;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            try {
                port_arg = std::stol(argv[++i]);
            } catch (const std::exception&) {
                spdlog::error("invalid value for --port: {}", argv[i]);
                return 1;
            }
        } else {
            spdlog::error("usage: {} [--port N]", argv[0]);
            return 1;
        }
    }
    // Clamp to valid TCP port range (uses the P0-a Mth translation demo).
    const unsigned short port =
        static_cast<unsigned short>(mc::util::clamp(port_arg, 1L, 65535L));

    asio::io_context io;

    mc::server::network::Server server(mc::server::network::ServerConfig{});

    // Graceful shutdown: SIGINT/SIGTERM -> io_context.stop() (via asio, not a
    // raw signal handler — must not race the io_context event loop).
    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait(
        [&io](std::error_code /*ec*/, int signum) {
            spdlog::info("signal {} received, shutting down", signum);
            io.stop();
        });

    try {
        // acceptor must outlive io.run() — keep it scoped to this block.
        AcceptLoop acceptor(io, port, server);  // binds 0.0.0.0:port
        acceptor.start();

        spdlog::info("mc-cpp server 0.0.1 / MC 26.2 protocol 776 / P1-b protocol state machine");
        spdlog::info("listening on 0.0.0.0:{}", port);
        io.run();
    } catch (const asio::system_error& e) {
        spdlog::error("failed to listen on 0.0.0.0:{}: {}", port, e.what());
        return 1;
    }

    // io_context drained: break any blocking connection reads, then join.
    server.shutdown();
    spdlog::info("io_context drained, bye");
    return 0;
}
