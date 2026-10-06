// P0-a: empty mc-server entry — spdlog banner + asio async accept loop.
// Accepts TCP connections, logs the peer address, closes immediately.
// Protocol handling (handshake/status/login) lands in P1 (plan §4).
#include <asio.hpp>
#include <spdlog/spdlog.h>

#include <csignal>
#include <exception>
#include <optional>
#include <string>
#include <system_error>

#include "mc/util/Mth.hpp"

namespace {

using asio::ip::tcp;

constexpr unsigned short kDefaultPort = 25565;

// Accept loop: for each accepted connection log the peer and drop it (P0).
// Re-arms itself until the acceptor is aborted (io_context stop / shutdown).
class AcceptLoop {
public:
    AcceptLoop(asio::io_context& io, unsigned short port)
        : acceptor_(io, tcp::endpoint(tcp::v4(), port)) {}

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
                        spdlog::info("accepted connection from {}:{} (closing; protocol handling lands in P1)",
                                     ep.address().to_string(), ep.port());
                    } else {
                        spdlog::info("accepted connection (peer endpoint unavailable: {})", ep_ec.message());
                    }
                    std::error_code close_ec;
                    socket_->close(close_ec);  // P0: accept-and-drop
                } else {
                    spdlog::warn("accept failed: {}", ec.message());
                }
                doAccept();
            });
    }

    tcp::acceptor acceptor_;
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
        AcceptLoop acceptor(io, port);  // binds 0.0.0.0:port
        acceptor.start();

        spdlog::info("mc-cpp server 0.0.1 / MC 26.2 protocol 776 / P0 skeleton");
        spdlog::info("listening on 0.0.0.0:{}", port);
        io.run();
    } catch (const asio::system_error& e) {
        spdlog::error("failed to listen on 0.0.0.0:{}: {}", port, e.what());
        return 1;
    }

    spdlog::info("io_context drained, bye");
    return 0;
}
