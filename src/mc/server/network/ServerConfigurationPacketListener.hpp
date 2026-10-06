#pragma once
// from net/minecraft/server/network/ServerConfigurationPacketListenerImpl.java
//   + net/minecraft/server/network/ServerCommonPacketListenerImpl.java
// (offline configuration phase, P1-b minimal set)
//
// java startConfiguration() (L83-101) sends, in order:
//   1. ClientboundCustomPayloadPacket(BrandPayload)          [always]
//   2. ClientboundServerLinksPacket                          [only if non-empty]
//   3. ClientboundUpdateEnabledFeaturesPacket                [always]
//   then queues tasks: synchronize_registries -> prepare_spawn -> join_world
//   and starts the first task, which sends ClientboundSelectKnownPacks.
//
// Task pipeline (java) vs P1-b (wire-visible parts):
//   synchronize_registries: client select_known_packs reply ->
//     ClientboundRegistryDataPacket x N + ClientboundUpdateTagsPacket
//     (SynchronizeRegistriesTask.sendRegistries L35-44)
//   prepare_spawn: async spawn-chunk prep, emits no configuration packet
//   join_world: sends ClientboundFinishConfigurationPacket (JoinWorldTask.start)
// P1-b collapses prepare_spawn (no world yet): registry data, update tags and
// finish configuration are written back-to-back after the known-packs reply.
//
// handleConfigurationFinished (L160-183): validates the join_world task, then
// switches to GameProtocols (PLAY). P1-b stops at this boundary: log + close.

#include <cstdint>

#include "mc/core/UUIDUtil.hpp"
#include "mc/network/ByteBuffer.hpp"
#include "mc/server/level/ClientInformation.hpp"

namespace mc::server::network {

class Server;
class ServerConnection;

class ServerConfigurationPacketListenerImpl {
public:
    ServerConfigurationPacketListenerImpl(Server& server, ServerConnection& connection,
                                          const std::string& player_name,
                                          const mc::util::UUID& player_id);

    // java: startConfiguration — brand payload + feature flags + queue the
    // synchronize-registries task (select_known_packs request).
    void start_configuration();

    // java: the profile reaches this listener through CommonListenerCookie
    // (handleLoginAcknowledgement -> createInitial(authenticatedProfile)); the
    // C++ login listener hands the offline profile over before starting.
    void set_player(const std::string& name, const mc::util::UUID& id);

    // Dispatches one CONFIGURATION-state serverbound packet; false = stop.
    bool handle_packet(int32_t packet_id, mc::network::ByteBuffer& body);

private:
    // java: handleClientInformation — stores ClientInformation.
    void handle_client_information(mc::network::ByteBuffer& body);

    // java: handleSelectKnownPacks -> SynchronizeRegistriesTask.handleResponse
    // -> sendRegistries; then (P1-b) JoinWorldTask.start = finish config.
    void handle_select_known_packs(mc::network::ByteBuffer& body);

    // java: handleConfigurationFinished -> PLAY protocol switch (P1-b: log
    // boundary, hold, then close).
    void handle_configuration_finished();

    Server& server_;
    ServerConnection& connection_;
    std::string player_name_;
    mc::util::UUID player_id_;
    mc::server::level::ClientInformation client_information_;
    bool registries_sent_ = false;
    bool play_boundary_reached_ = false;
};

}  // namespace mc::server::network
