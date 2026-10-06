#pragma once
// from net/minecraft/server/level/ClientInformation.java
// (namespace mapped per plan §2 rule 3: net.minecraft.server.level ->
// mc::server::level)
//
// record ClientInformation(
//     String language, int viewDistance, ChatVisiblity chatVisibility,
//     boolean chatColors, int modelCustomisation, HumanoidArm mainHand,
//     boolean textFilteringEnabled, boolean allowsListing,
//     ParticleStatus particleStatus)
//
// Wire format = the record's FriendlyByteBuf (de)serialiser:
//   readUtf(16), readByte, readEnum(ChatVisiblity), readBoolean,
//   readUnsignedByte, readEnum(HumanoidArm), readBoolean, readBoolean,
//   readEnum(ParticleStatus)
// readEnum/writeEnum are VarInt ordinals (FriendlyByteBuf L467-473).
// The enums are stored as their ordinals here (P1-b only stores the payload;
// the enum classes land with the entity work).

#include <cstdint>
#include <string>

#include "mc/network/ByteBuffer.hpp"

namespace mc::server::level {

struct ClientInformation {
    std::string language = "en_us";     // createDefault(): "en_us"
    int8_t view_distance = 2;           // createDefault(): 2
    int32_t chat_visibility = 0;        // ChatVisiblity.FULL ordinal
    bool chat_colors = true;            // createDefault(): true
    uint8_t model_customisation = 0;    // createDefault(): 0
    int32_t main_hand = 1;              // Player.DEFAULT_MAIN_HAND = RIGHT ordinal 1
    bool text_filtering_enabled = false;
    bool allows_listing = false;
    int32_t particle_status = 0;        // ParticleStatus.ALL ordinal

    // java: ClientInformation(FriendlyByteBuf input)
    static ClientInformation read(mc::network::ByteBuffer& buf);

    // java: ClientInformation.write(FriendlyByteBuf output)
    void write(mc::network::ByteBuffer& buf) const;
};

}  // namespace mc::server::level
