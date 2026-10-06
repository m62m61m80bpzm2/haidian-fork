#pragma once
// Minimal server facade for P1-b — the C++ counterpart of the pieces of
// net/minecraft/server/MinecraftServer.java that the three listeners need
// (status payload, compression threshold, protocol version) plus the
// connection-thread registry that replaces netty's event loop.
//
// Per-connection dispatch (Server::connection_loop) mirrors
// Connection#channelRead: each decoded packet is handed to the listener of the
// connection's current protocol state. The listeners are 1:1 ports:
//   ServerHandshakePacketListenerImpl (this dir)
//   ServerStatusPacketListenerImpl    (this dir, created on the STATUS switch)
//   ServerLoginPacketListenerImpl     (this dir)
//   ServerConfigurationPacketListenerImpl (this dir)

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "mc/protocol/PacketCodec.hpp"

namespace mc::server::network {

class ServerConnection;

// Server-wide constants (vanilla DedicatedServerProperties defaults unless a
// value is fixed by the P1-b plan).
struct ServerConfig {
    std::string motd = "mc-cpp P1";
    int max_players = 20;                       // server.properties max-players
    int compression_threshold = 256;            // network-compression-threshold (DedicatedServerProperties L107)
    std::string brand = "mc-cpp";               // BrandPayload (vanilla: getServerModName() = "vanilla")
    std::string version_name = "26.2";          // SharedConstants.getCurrentVersion().name()
    int protocol_version = 776;                 // WorldVersion.protocolVersion()
    bool enforce_secure_profile = true;         // vanilla default -> enforcesSecureChat
    // configuration registry_data keys: the 29 SYNCHRONIZED_REGISTRIES entries
    // in registration order (net/minecraft/resources/RegistryDataLoader.java
    // L135-165). RegistrySynchronization.packRegistries walks exactly this list
    // and emits one ClientboundRegistryDataPacket per loaded registry.
    std::vector<std::string> registry_keys = {
        "minecraft:worldgen/biome",         // Registries.BIOME
        "minecraft:chat_type",             // Registries.CHAT_TYPE
        "minecraft:trim_pattern",          // Registries.TRIM_PATTERN
        "minecraft:trim_material",         // Registries.TRIM_MATERIAL
        "minecraft:wolf_variant",          // Registries.WOLF_VARIANT
        "minecraft:wolf_sound_variant",    // Registries.WOLF_SOUND_VARIANT
        "minecraft:pig_variant",           // Registries.PIG_VARIANT
        "minecraft:pig_sound_variant",     // Registries.PIG_SOUND_VARIANT
        "minecraft:frog_variant",          // Registries.FROG_VARIANT
        "minecraft:cat_variant",           // Registries.CAT_VARIANT
        "minecraft:cat_sound_variant",     // Registries.CAT_SOUND_VARIANT
        "minecraft:cow_sound_variant",     // Registries.COW_SOUND_VARIANT
        "minecraft:cow_variant",           // Registries.COW_VARIANT
        "minecraft:chicken_sound_variant",  // Registries.CHICKEN_SOUND_VARIANT
        "minecraft:chicken_variant",       // Registries.CHICKEN_VARIANT
        "minecraft:zombie_nautilus_variant",  // Registries.ZOMBIE_NAUTILUS_VARIANT
        "minecraft:painting_variant",      // Registries.PAINTING_VARIANT
        "minecraft:sulfur_cube_archetype",  // Registries.SULFUR_CUBE_ARCHETYPE
        "minecraft:dimension_type",        // Registries.DIMENSION_TYPE
        "minecraft:damage_type",           // Registries.DAMAGE_TYPE
        "minecraft:banner_pattern",        // Registries.BANNER_PATTERN
        "minecraft:enchantment",           // Registries.ENCHANTMENT
        "minecraft:jukebox_song",          // Registries.JUKEBOX_SONG
        "minecraft:instrument",            // Registries.INSTRUMENT
        "minecraft:test_environment",      // Registries.TEST_ENVIRONMENT
        "minecraft:test_instance",         // Registries.TEST_INSTANCE
        "minecraft:dialog",                // Registries.DIALOG
        "minecraft:world_clock",           // Registries.WORLD_CLOCK
        "minecraft:timeline",              // Registries.TIMELINE
    };
    // java: FeatureFlags.REGISTRY.toNames(getWorldData().enabledFeatures()) —
    // a fresh world enables FeatureFlags.VANILLA_SET = {vanilla}
    // (FeatureFlags.java L34-41, WorldDataConfiguration.DEFAULT).
    std::vector<std::string> enabled_features = {"minecraft:vanilla"};
    // java: getResourceManager().listPacks() flatMap knownPackInfo — a fresh
    // vanilla server exposes the single built-in datapack whose location info
    // is BuiltInPackSource.CORE_PACK_INFO = KnownPack.vanilla("core")
    // (ServerPacksSource.java VANILLA_PACK_INFO) with version id "26.2"
    // (version.json in mc-26.2-server.jar).
    std::vector<protocol::KnownPack> known_packs = {{"minecraft", "core", "26.2"}};
};

class Server {
public:
    explicit Server(ServerConfig config);

    const ServerConfig& config() const { return config_; }

    // Accept-loop entry: takes ownership of the socket and serves it on a
    // dedicated thread (java: netty child channel on the event loop).
    void handle_connection(asio::ip::tcp::socket socket);

    // Closes live connections (breaking their blocking reads) and joins the
    // worker threads. Called after the io_context has drained.
    void shutdown();

private:
    void connection_loop(const std::shared_ptr<ServerConnection>& connection);

    ServerConfig config_;
    std::mutex mutex_;
    std::vector<std::shared_ptr<ServerConnection>> connections_;
    std::vector<std::thread> workers_;
};

}  // namespace mc::server::network
