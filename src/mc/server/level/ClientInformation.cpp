// from net/minecraft/server/level/ClientInformation.java (see header).
#include "mc/server/level/ClientInformation.hpp"

namespace mc::server::level {

ClientInformation ClientInformation::read(mc::network::ByteBuffer& buf) {
    ClientInformation info;
    info.language = buf.read_utf(16);
    info.view_distance = buf.read_byte();
    info.chat_visibility = buf.read_varint();   // readEnum(ChatVisiblity)
    info.chat_colors = buf.read_byte() != 0;
    info.model_customisation = static_cast<uint8_t>(buf.read_byte());  // readUnsignedByte
    info.main_hand = buf.read_varint();          // readEnum(HumanoidArm)
    info.text_filtering_enabled = buf.read_byte() != 0;
    info.allows_listing = buf.read_byte() != 0;
    info.particle_status = buf.read_varint();    // readEnum(ParticleStatus)
    return info;
}

void ClientInformation::write(mc::network::ByteBuffer& buf) const {
    buf.write_utf(this->language, 32767);        // writeUtf(this.language) — default cap
    buf.write_byte(this->view_distance);
    buf.write_varint(this->chat_visibility);     // writeEnum
    buf.write_byte(this->chat_colors ? 1 : 0);
    buf.write_byte(static_cast<int8_t>(this->model_customisation));
    buf.write_varint(this->main_hand);
    buf.write_byte(this->text_filtering_enabled ? 1 : 0);
    buf.write_byte(this->allows_listing ? 1 : 0);
    buf.write_varint(this->particle_status);
}

}  // namespace mc::server::level
