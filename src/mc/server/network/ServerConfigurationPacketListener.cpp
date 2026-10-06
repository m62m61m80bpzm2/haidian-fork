// 1:1 translation of net/minecraft/server/network/ServerConfigurationPacketListenerImpl.java
//   + the task classes it queues (see header for the P1-b minimal-set notes).
#include "mc/server/network/ServerConfigurationPacketListener.hpp"

#include <spdlog/spdlog.h>

#include "mc/protocol/ConnectionState.hpp"
#include "mc/protocol/PacketCodec.hpp"
#include "mc/server/level/ClientInformation.hpp"
#include "mc/server/network/Server.hpp"
#include "mc/server/network/ServerConnection.hpp"

namespace mc::server::network {

ServerConfigurationPacketListenerImpl::ServerConfigurationPacketListenerImpl(
    Server& server, ServerConnection& connection, const std::string& player_name,
    const mc::util::UUID& player_id)
    : server_(server), connection_(connection), player_name_(player_name), player_id_(player_id) {
    // java: this.clientInformation = cookie.clientInformation()
    // (CommonListenerCookie.createInitial -> ClientInformation.createDefault())
    client_information_ = mc::server::level::ClientInformation{};
}

void ServerConfigurationPacketListenerImpl::set_player(const std::string& name,
                                                       const mc::util::UUID& id) {
    // java: CommonListenerCookie.createInitial(profile, transferred) carries the
    // authenticated profile into the configuration listener.
    player_name_ = name;
    player_id_ = id;
}

void ServerConfigurationPacketListenerImpl::start_configuration() {
    // java: send(new ClientboundCustomPayloadPacket(new BrandPayload(
    //         server.getServerModName())))
    if (!connection_.send_packet(protocol::encode_custom_payload_brand(server_.config().brand))) {
        return;
    }

    // java: ServerLinks — empty for a fresh vanilla server, skipped
    // (if (!serverLinks.isEmpty()) send(...)).

    // java: send(new ClientboundUpdateEnabledFeaturesPacket(
    //         FeatureFlags.REGISTRY.toNames(server.getWorldData().enabledFeatures())))
    if (!connection_.send_packet(
            protocol::encode_update_enabled_features(server_.config().enabled_features))) {
        return;
    }

    // java: synchronizeRegistriesTask = new SynchronizeRegistriesTask(knownPacks,
    //         registries); configurationTasks.add(...); ... returnToWorld();
    //       startNextTask() -> SynchronizeRegistriesTask.start ->
    //         send(new ClientboundSelectKnownPacks(requestedPacks))
    (void)connection_.send_packet(protocol::encode_select_known_packs(server_.config().known_packs));
    spdlog::info("{}: configuration started (brand \"{}\", {} registry key(s))",
                 connection_.peer(), server_.config().brand,
                 server_.config().registry_keys.size());
}

bool ServerConfigurationPacketListenerImpl::handle_packet(int32_t packet_id,
                                                          mc::network::ByteBuffer& body) {
    switch (packet_id) {
        case protocol::S_CONFIG_CLIENT_INFORMATION:
            handle_client_information(body);
            return true;
        case protocol::S_CONFIG_SELECT_KNOWN_PACKS:
            handle_select_known_packs(body);
            return connection_.connected() && !play_boundary_reached_;
        case protocol::S_CONFIG_FINISH_CONFIGURATION:
            handle_configuration_finished();
            return !play_boundary_reached_;
        case protocol::S_CONFIG_KEEP_ALIVE: {
            // java: ServerCommonPacketListenerImpl.handleKeepAlive records the
            // echoed id (no reply); P1-b's configuration phase is short so no
            // keepalive is ever sent.
            const int64_t id = body.read_long();
            spdlog::debug("{}: keep_alive {}", connection_.peer(), id);
            return true;
        }
        case protocol::S_CONFIG_COOKIE_RESPONSE:
        case protocol::S_CONFIG_CUSTOM_PAYLOAD:
        case protocol::S_CONFIG_PONG:
        case protocol::S_CONFIG_RESOURCE_PACK:
        case protocol::S_CONFIG_CUSTOM_CLICK_ACTION:
        case protocol::S_CONFIG_ACCEPT_CODE_OF_CONDUCT:
            // valid per protocol but not part of the P1-b flow — payload
            // skipped (java would route to the matching task/state machine).
            spdlog::info("{}: configuration packet {} (payload {} B, ignored)",
                         connection_.peer(), packet_id, body.readable_bytes());
            return true;
        default:
            // java: unknown id -> IdDispatchCodec DecoderException
            spdlog::warn("{}: unknown configuration packet id {}", connection_.peer(), packet_id);
            connection_.close();
            return false;
    }
}

void ServerConfigurationPacketListenerImpl::handle_client_information(mc::network::ByteBuffer& body) {
    // java: handleClientInformation — this.clientInformation = packet.information();
    client_information_ = mc::server::level::ClientInformation::read(body);
    spdlog::info("{}: client information (lang=\"{}\", viewDistance={})",
                 connection_.peer(), client_information_.language,
                 client_information_.view_distance);
}

void ServerConfigurationPacketListenerImpl::handle_select_known_packs(mc::network::ByteBuffer& body) {
    // java: if (synchronizeRegistriesTask == null) throw IllegalStateException
    //       ("Unexpected response from client: received pack selection, but no
    //         negotiation ongoing")
    if (registries_sent_) {
        spdlog::warn("{}: duplicate select_known_packs", connection_.peer());
        connection_.close();
        return;
    }
    registries_sent_ = true;
    const std::vector<protocol::KnownPack> accepted = protocol::decode_select_known_packs(body);
    spdlog::info("{}: client knows {} pack(s)", connection_.peer(), accepted.size());

    // java SynchronizeRegistriesTask.sendRegistries:
    //   for each synchronized registry -> ClientboundRegistryDataPacket(key, entries)
    //   then ClientboundUpdateTagsPacket(serializeTagsToNetwork(registries))
    // P1-b minimal set: the vanilla registry KEY list with empty entries and
    // an empty tag map (decision + vanilla capture in worklog Task 3-b).
    for (const std::string& key : server_.config().registry_keys) {
        if (!connection_.send_packet(protocol::encode_registry_data(key))) {
            return;
        }
    }
    if (!connection_.send_packet(protocol::encode_update_tags_empty())) {
        return;
    }

    // java: finishCurrentTask(synchronize_registries) -> prepare_spawn task
    // (no packets) -> join_world task -> JoinWorldTask.start sends
    // ClientboundFinishConfigurationPacket. P1-b has no world to prepare.
    if (!connection_.send_packet(protocol::encode_finish_configuration())) {
        return;
    }
    spdlog::info("{}: registry data ({} packet(s)) + update tags + finish configuration sent",
                 connection_.peer(), server_.config().registry_keys.size());
}

void ServerConfigurationPacketListenerImpl::handle_configuration_finished() {
    // java: finishCurrentTask(JoinWorldTask.TYPE); setupOutboundProtocol(
    //         GameProtocols.CLIENTBOUND_TEMPLATE.bind(...)) -> PLAY
    if (play_boundary_reached_) {
        spdlog::warn("{}: duplicate finish configuration", connection_.peer());
        connection_.close();
        return;
    }
    play_boundary_reached_ = true;
    connection_.set_state(protocol::ConnectionState::PLAY);
    // P1-b stops here: the play protocol (login/registry sync in-play) lands
    // in a later phase. java would now spawn the player (spawnPlayer) and keep
    // the connection; P1-b has no play protocol, so the session ends here —
    // close now (the dispatch loop would otherwise block on the next read).
    spdlog::info(
        "{}: PLAY boundary reached for {} ({}); play protocol not implemented in P1-b, closing",
        connection_.peer(), player_name_, mc::core::uuid_util::to_string(player_id_));
    connection_.close();
}

}  // namespace mc::server::network
