// P0-b: capture-proxy — TCP man-in-the-middle for Minecraft protocol capture.
//
// Sits between a Minecraft client and an upstream server (listen -> upstream)
// and forwards bytes both ways untouched (async, full duplex, single-threaded
// io_context). Per direction the raw byte stream is written to
// <dir>/session<NNN>_{c2s,s2c}.bin. With --parse, each direction is
// additionally split into MC frames (VarInt length prefix + payload, see
// net/minecraft/network/VarInt.java semantics) and logged one line per packet
// with a 64-byte hexdump to <dir>/session<NNN>_{c2s,s2c}.log.
//
// Frame format: VarInt(payload_len) + payload. VarInt = LEB128: 7 data bits
// per byte, little-endian order, high bit = "more bytes follow"; >5 bytes or
// payload > 2^21-1 is a protocol error -> log + fall back to raw forwarding.
//
// CLI: capture-proxy [--listen PORT] [--upstream HOST:PORT] [--dir OUTDIR] [--parse]
#include <asio.hpp>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

using asio::ip::tcp;
using Clock = std::chrono::steady_clock;

constexpr std::size_t kChunkSize = 64 * 1024;              // per-read chunk
constexpr std::size_t kMaxVarIntBytes = 5;                 // MC VarInt cap
constexpr uint32_t kMaxFramePayload = (1u << 21) - 1;      // 2^21 - 1
constexpr std::size_t kHexDumpBytes = 64;

enum class VarIntStatus { kOk, kNeedMore, kError };

// from net/minecraft/network/VarInt.java — LEB128: 7 data bits per byte in
// little-endian order, high bit = "more bytes follow". A 5th byte that still
// has the continuation bit set is malformed (Java reads at most 5 bytes).
VarIntStatus readVarInt(const uint8_t* data, std::size_t avail, uint32_t& out_value,
                        std::size_t& out_consumed) {
    out_value = 0;
    for (std::size_t i = 0; i < kMaxVarIntBytes; ++i) {
        if (i >= avail) {
            return VarIntStatus::kNeedMore;
        }
        const uint8_t byte = data[i];
        out_value |= static_cast<uint32_t>(byte & 0x7Fu) << (7U * i);
        if ((byte & 0x80u) == 0) {
            out_consumed = i + 1;
            return VarIntStatus::kOk;
        }
    }
    return VarIntStatus::kError;
}

struct Options {
    unsigned short listen_port = 25566;
    std::string upstream_host = "127.0.0.1";
    unsigned short upstream_port = 25565;
    std::string dir = "captures";
    bool parse = false;
};

bool parsePort(const std::string& text, unsigned short& out) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    unsigned long value = 0;
    try {
        value = std::stoul(text);
    } catch (const std::exception&) {
        return false;
    }
    if (value < 1 || value > 65535) {
        return false;
    }
    out = static_cast<unsigned short>(value);
    return true;
}

bool parseUpstream(const std::string& spec, Options& options) {
    const std::size_t colon = spec.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == spec.size()) {
        spdlog::error("invalid --upstream '{}': expected HOST:PORT", spec);
        return false;
    }
    options.upstream_host = spec.substr(0, colon);
    const std::string port_text = spec.substr(colon + 1);
    if (!parsePort(port_text, options.upstream_port)) {
        spdlog::error("invalid --upstream '{}': bad port '{}'", spec, port_text);
        return false;
    }
    return true;
}

void printUsage(const char* argv0) {
    spdlog::info("usage: {} [--listen PORT] [--upstream HOST:PORT] [--dir OUTDIR] [--parse]", argv0);
    spdlog::info("  defaults: --listen 25566 --upstream 127.0.0.1:25565 --dir captures");
}

// One captured connection: client socket <-> upstream socket, two independent
// half-duplex pumps (c2s reads client / writes upstream; s2c the reverse),
// per-direction raw capture file and optional MC frame log.
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(asio::io_context& io, uint32_t id, const std::filesystem::path& dir,
            bool parse_enabled)
        : id_(id), dir_(dir), parse_enabled_(parse_enabled), client_(io), upstream_(io),
          start_(Clock::now()) {
        c2s_.emplace("c2s", &client_, &upstream_);
        s2c_.emplace("s2c", &upstream_, &client_);
    }

    void setOnFinished(std::function<void()> callback) { on_finished_ = std::move(callback); }

    tcp::socket& clientSocket() { return client_; }
    bool finished() const { return finished_; }

    void logStart(const std::string& peer, const std::string& upstream) {
        spdlog::info("session {:03d}: client {} connected; upstream {} (frame parse: {})",
                     id_, peer, upstream, parse_enabled_ ? "on" : "off");
        openFiles();
    }

    void start(asio::io_context& io, const std::string& host, unsigned short port) {
        resolver_ = std::make_unique<tcp::resolver>(io);
        auto self = shared_from_this();
        resolver_->async_resolve(host, std::to_string(port),
            [self, this](std::error_code ec, const tcp::resolver::results_type& results) {
                if (finished_) {
                    return;
                }
                if (ec) {
                    spdlog::warn("session {:03d}: upstream resolve failed: {}", id_, ec.message());
                    finish("resolve failed");
                    return;
                }
                asio::async_connect(upstream_, results,
                    [self, this](std::error_code connect_ec, const tcp::endpoint& endpoint) {
                        if (finished_) {
                            return;
                        }
                        if (connect_ec) {
                            spdlog::warn("session {:03d}: upstream connect failed: {}", id_,
                                         connect_ec.message());
                            finish("connect failed");
                            return;
                        }
                        spdlog::info("session {:03d}: upstream connected ({}:{})", id_,
                                     endpoint.address().to_string(), endpoint.port());
                        pump(*c2s_);
                        pump(*s2c_);
                    });
            });
    }

    // Idempotent teardown: close sockets, flush+close capture files, log stats.
    void finish(const char* reason) {
        if (finished_) {
            return;
        }
        finished_ = true;
        if (resolver_) {
            resolver_->cancel();
        }
        std::error_code ec;
        client_.close(ec);
        upstream_.close(ec);
        closeFiles();
        c2s_->frame_buf.clear();
        c2s_->frame_buf.shrink_to_fit();
        s2c_->frame_buf.clear();
        s2c_->frame_buf.shrink_to_fit();
        const double duration = std::chrono::duration<double>(Clock::now() - start_).count();
        spdlog::info("session {:03d} finished ({}): c2s={} B / {} frame(s), s2c={} B / {} frame(s), duration {:.3f}s",
                     id_, reason, c2s_->bytes, c2s_->frames, s2c_->bytes, s2c_->frames, duration);
        if (on_finished_) {
            on_finished_();  // registry prunes this session from its vector
        }
    }

private:
    // One half-duplex pipeline: src is read, dst is written to.
    struct Direction {
        Direction(const char* dir_name, tcp::socket* from, tcp::socket* to)
            : name(dir_name), src(from), dst(to) {}

        const char* name;
        tcp::socket* src;
        tcp::socket* dst;
        std::FILE* raw = nullptr;   // <dir>/session<NNN>_<name>.bin
        std::FILE* log = nullptr;   // --parse frame log (null when disabled)
        std::vector<uint8_t> frame_buf;
        bool parse_active = false;
        uint32_t frames = 0;
        uint64_t bytes = 0;
        bool ended = false;
        std::array<char, kChunkSize> chunk;
    };

    void openFiles() {
        const std::string base = fmt::format("session{:03d}_", id_);
        c2s_->raw = openStream(base + "c2s.bin");
        s2c_->raw = openStream(base + "s2c.bin");
        if (parse_enabled_) {
            c2s_->log = openStream(base + "c2s.log");
            s2c_->log = openStream(base + "s2c.log");
            c2s_->parse_active = c2s_->log != nullptr;
            s2c_->parse_active = s2c_->log != nullptr;
        }
    }

    std::FILE* openStream(const std::string& name) {
        const std::filesystem::path path = dir_ / name;
        std::FILE* file = std::fopen(path.string().c_str(), "wb");
        if (file == nullptr) {
            spdlog::warn("session {:03d}: cannot open {} ({}) — stream capture disabled",
                         id_, path.string(), std::strerror(errno));
        }
        return file;
    }

    void closeFiles() {
        for (std::FILE* file : {c2s_->raw, s2c_->raw, c2s_->log, s2c_->log}) {
            if (file != nullptr) {
                std::fflush(file);
                std::fclose(file);
            }
        }
    }

    static void writeAll(std::FILE* file, const void* data, std::size_t n) {
        if (file == nullptr || n == 0) {
            return;
        }
        if (std::fwrite(data, 1, n, file) != n) {
            // Disk write failure: keep forwarding; capture for this stream is
            // silently incomplete (ferror is surfaced at fclose).
            spdlog::warn("capture write failed (disk full?)");
        }
    }

    // Serial per direction: read -> write -> read ... Zero-copy of payload
    // data (single chunk buffer), no loss: every byte read is written whole.
    void pump(Direction& d) {
        auto self = shared_from_this();
        d.src->async_read_some(asio::buffer(d.chunk),
            [self, this, &d](std::error_code ec, std::size_t n) {
                if (finished_) {
                    return;
                }
                if (ec == asio::error::eof) {
                    endDirection(d, "eof");
                    return;
                }
                if (ec) {
                    endDirection(d, ec.message());
                    return;
                }
                // Record first (capture what the wire carried), then forward.
                d.bytes += n;
                writeAll(d.raw, d.chunk.data(), n);
                if (d.parse_active) {
                    feedParser(d, d.chunk.data(), n);
                }
                asio::async_write(*d.dst, asio::buffer(d.chunk.data(), n),
                    [self, this, &d](std::error_code write_ec, std::size_t /*n*/) {
                        if (finished_) {
                            return;
                        }
                        if (write_ec) {
                            endDirection(d, write_ec.message());
                            return;
                        }
                        pump(d);
                    });
            });
    }

    void endDirection(Direction& d, const std::string& why) {
        if (d.ended) {
            return;
        }
        d.ended = true;
        // Close propagation: this side is done, so shut down the write side of
        // the peer socket. The peer drains what we already queued (the serial
        // pump guarantees it is fully flushed) and then sees EOF — residual
        // data is sent out as best as possible; no hard close needed here.
        std::error_code ec;
        d.dst->shutdown(tcp::socket::shutdown_send, ec);
        spdlog::info("session {:03d} {} direction ended ({}): {} B / {} frame(s)",
                     id_, d.name, why, d.bytes, d.frames);
        if (c2s_->ended && s2c_->ended) {
            finish("both directions ended");
        }
    }

    // Stream-safe frame splitting: accumulate into frame_buf, cut out one
    // frame whenever a full VarInt+payload is buffered; the remainder stays
    // for the next frame. Never assumes a read chunk == one packet.
    void feedParser(Direction& d, const char* data, std::size_t n) {
        d.frame_buf.insert(d.frame_buf.end(), data, data + n);
        while (d.parse_active) {
            uint32_t payload_len = 0;
            std::size_t consumed = 0;
            switch (readVarInt(d.frame_buf.data(), d.frame_buf.size(), payload_len, consumed)) {
                case VarIntStatus::kNeedMore:
                    return;  // VarInt incomplete — wait for more bytes
                case VarIntStatus::kError:
                    parseFail(d, "VarInt longer than 5 bytes");
                    return;
                case VarIntStatus::kOk:
                    break;
            }
            if (payload_len > kMaxFramePayload) {
                parseFail(d, fmt::format("frame payload {} exceeds 2^21-1", payload_len));
                return;
            }
            const std::size_t frame_total = consumed + payload_len;
            if (d.frame_buf.size() < frame_total) {
                return;  // payload not fully buffered yet
            }
            logFrame(d, payload_len, d.frame_buf.data() + consumed);
            d.frame_buf.erase(d.frame_buf.begin(),
                              d.frame_buf.begin() + static_cast<std::ptrdiff_t>(frame_total));
        }
    }

    void logFrame(Direction& d, uint32_t payload_len, const uint8_t* payload) {
        ++d.frames;
        if (d.log == nullptr) {
            return;
        }
        const double elapsed = std::chrono::duration<double>(Clock::now() - start_).count();
        std::string line = fmt::format("#{:03d} len={} t=+{:.3f}s ", d.frames, payload_len, elapsed);
        const std::size_t shown = std::min<std::size_t>(payload_len, kHexDumpBytes);
        static constexpr char kHexDigits[] = "0123456789abcdef";
        for (std::size_t i = 0; i < shown; ++i) {
            const uint8_t byte = payload[i];
            line.push_back(kHexDigits[byte >> 4]);
            line.push_back(kHexDigits[byte & 0x0Fu]);
            line.push_back(' ');
        }
        if (!line.empty() && line.back() == ' ') {
            line.back() = '\n';
        } else {
            line.push_back('\n');
        }
        writeAll(d.log, line.data(), line.size());
    }

    void parseFail(Direction& d, const std::string& why) {
        d.parse_active = false;
        d.frame_buf.clear();
        d.frame_buf.shrink_to_fit();
        spdlog::warn("session {:03d} {}: MC frame parse error ({}); falling back to raw forwarding",
                     id_, d.name, why);
    }

    uint32_t id_;
    std::filesystem::path dir_;
    bool parse_enabled_;
    tcp::socket client_;
    tcp::socket upstream_;
    std::optional<Direction> c2s_;
    std::optional<Direction> s2c_;
    Clock::time_point start_;
    std::unique_ptr<tcp::resolver> resolver_;
    std::function<void()> on_finished_;
    bool finished_ = false;
};

// Listener + session registry. Single-threaded: every callback runs on the
// one io_context thread, so no locking anywhere.
class Proxy {
public:
    Proxy(asio::io_context& io, const Options& options)
        : io_(io), options_(options),
          acceptor_(io, tcp::endpoint(tcp::v4(), options.listen_port)),
          signals_(io, SIGINT, SIGTERM) {}

    void start() {
        signals_.async_wait([this](std::error_code /*ec*/, int signal_number) {
            spdlog::info("signal {} received; stopping accept, flushing captures", signal_number);
            std::error_code ec;
            acceptor_.close(ec);
            const auto snapshot = sessions_;  // finish() prunes sessions_ via callback
            for (const auto& session : snapshot) {
                session->finish("signal shutdown");
            }
            sessions_.clear();
        });
        spdlog::info("capture-proxy: listening on 0.0.0.0:{} -> upstream {}:{} | dir: {} | frame parse: {}",
                     options_.listen_port, options_.upstream_host, options_.upstream_port,
                     options_.dir, options_.parse ? "on" : "off");
        doAccept();
    }

private:
    void doAccept() {
        pruneFinished();
        auto session = std::make_shared<Session>(io_, next_id_++, options_.dir, options_.parse);
        session->setOnFinished([this]() { pruneFinished(); });
        acceptor_.async_accept(session->clientSocket(),
            [this, session](std::error_code ec) {
                if (ec == asio::error::operation_aborted) {
                    return;  // acceptor closed — do not re-arm
                }
                if (ec) {
                    spdlog::warn("accept failed: {}", ec.message());
                } else {
                    std::string peer = "(unknown)";
                    std::error_code ep_ec;
                    const tcp::endpoint endpoint = session->clientSocket().remote_endpoint(ep_ec);
                    if (!ep_ec) {
                        peer = fmt::format("{}:{}", endpoint.address().to_string(), endpoint.port());
                    }
                    session->logStart(peer, fmt::format("{}:{}", options_.upstream_host,
                                                        options_.upstream_port));
                    session->start(io_, options_.upstream_host, options_.upstream_port);
                    sessions_.push_back(session);
                }
                doAccept();
            });
    }

    void pruneFinished() {
        sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                       [](const std::shared_ptr<Session>& s) { return s->finished(); }),
                        sessions_.end());
    }

    asio::io_context& io_;
    Options options_;
    tcp::acceptor acceptor_;
    asio::signal_set signals_;
    std::vector<std::shared_ptr<Session>> sessions_;
    uint32_t next_id_ = 1;
};

}  // namespace

int main(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--listen" && i + 1 < argc) {
            if (!parsePort(argv[++i], options.listen_port)) {
                spdlog::error("invalid --listen port: {}", argv[i]);
                return 1;
            }
        } else if (arg == "--upstream" && i + 1 < argc) {
            if (!parseUpstream(argv[++i], options)) {
                return 1;
            }
        } else if (arg == "--dir" && i + 1 < argc) {
            options.dir = argv[++i];
        } else if (arg == "--parse") {
            options.parse = true;
        } else {
            spdlog::error("unknown or incomplete argument: {}", arg);
            printUsage(argv[0]);
            return 1;
        }
    }

    std::error_code fs_ec;
    std::filesystem::create_directories(options.dir, fs_ec);
    if (fs_ec) {
        spdlog::error("cannot create capture dir {}: {}", options.dir, fs_ec.message());
        return 1;
    }

    asio::io_context io;
    try {
        Proxy proxy(io, options);
        proxy.start();
        io.run();
    } catch (const asio::system_error& e) {
        spdlog::error("fatal: {}", e.what());
        return 1;
    }

    spdlog::info("capture-proxy exited cleanly");
    return 0;
}
