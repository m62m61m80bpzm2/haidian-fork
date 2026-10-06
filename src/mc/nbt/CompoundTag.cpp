// from net/minecraft/nbt/CompoundTag.java and net/minecraft/nbt/ListTag.java
// (the TagType<...>.load / write bodies live inside those files in java).

#include "mc/nbt/Tag.hpp"

#include "mc/nbt/NbtIo.hpp"  // read_tag_payload (TagTypes dispatch)

namespace mc::nbt {

// ============================ CompoundTag ============================

void CompoundTag::write(network::ByteBuffer& out) const {
    // from CompoundTag.write: writeNamedTag(key, tag, output) per entry, then
    // the 0 terminator. writeNamedTag:
    //   output.writeByte(tag.getId());
    //   if (tag.getId() != 0) { output.writeUTF(name); tag.write(output); }
    for (const auto& [name, tag] : tags_) {
        const uint8_t id = static_cast<uint8_t>(tag->type());
        out.write_byte(static_cast<int8_t>(id));
        if (id != 0) {
            write_java_utf8(out, name);
            tag->write(out);
        }
    }
    out.write_byte(0);
}

std::unique_ptr<CompoundTag> CompoundTag::read(network::ByteBuffer& in, NbtAccounter& accounter) {
    // from CompoundTag.TYPE.load -> loadCompound (accountBytes(48) inside the
    // pushDepth/popDepth pair):
    //   while ((tagType = input.readByte()) != 0) {
    //       key = readUTF;  accountBytes(28); accountBytes(2, key.length());
    //       tag = readNamedTagData(TagTypes.getType(tagType), key, input, accounter);
    //       if (values.put(key, tag) == null) accountBytes(36);
    //   }
    const detail::DepthGuard guard(accounter);
    auto tag = std::make_unique<CompoundTag>();
    accounter.account_bytes(48);
    for (;;) {
        const int8_t type_id = in.read_byte();
        if (type_id == 0) {
            break;
        }
        std::string key = read_java_utf8(in);
        accounter.account_bytes(28);  // Tag.STRING_SIZE
        accounter.account_bytes(2, static_cast<long>(network::ByteBuffer::utf16_length(key)));
        std::unique_ptr<Tag> value = read_tag_payload(type_id, in, accounter);
        const bool inserted = tag->tags_.emplace(std::move(key), std::move(value)).second;
        if (inserted) {
            accounter.account_bytes(36);
        }
    }
    return tag;
}

std::unique_ptr<Tag> CompoundTag::copy() const {
    // from CompoundTag.copy: deep copy every child
    Container new_tags;
    for (const auto& [name, tag] : tags_) {
        new_tags[name] = tag->copy();
    }
    return std::make_unique<CompoundTag>(std::move(new_tags));
}

std::unique_ptr<Tag> CompoundTag::put(const std::string& name, std::unique_ptr<Tag> tag) {
    // java: @Nullable Tag put(String name, Tag tag) — returns the previous value
    std::unique_ptr<Tag> previous;
    const auto it = tags_.find(name);
    if (it != tags_.end()) {
        previous = std::move(it->second);
    }
    tags_[name] = std::move(tag);
    return previous;
}

void CompoundTag::put_byte(const std::string& name, int8_t value) { put(name, std::make_unique<ByteTag>(value)); }
void CompoundTag::put_short(const std::string& name, int16_t value) { put(name, std::make_unique<ShortTag>(value)); }
void CompoundTag::put_int(const std::string& name, int32_t value) { put(name, std::make_unique<IntTag>(value)); }
void CompoundTag::put_long(const std::string& name, int64_t value) { put(name, std::make_unique<LongTag>(value)); }
void CompoundTag::put_float(const std::string& name, float value) { put(name, std::make_unique<FloatTag>(value)); }
void CompoundTag::put_double(const std::string& name, double value) { put(name, std::make_unique<DoubleTag>(value)); }
void CompoundTag::put_string(const std::string& name, std::string value) { put(name, std::make_unique<StringTag>(std::move(value))); }
void CompoundTag::put_boolean(const std::string& name, bool value) { put(name, std::make_unique<ByteTag>(value ? 1 : 0)); }
void CompoundTag::put_byte_array(const std::string& name, std::vector<uint8_t> value) {
    put(name, std::make_unique<ByteArrayTag>(std::move(value)));
}
void CompoundTag::put_int_array(const std::string& name, std::vector<int32_t> value) {
    put(name, std::make_unique<IntArrayTag>(std::move(value)));
}
void CompoundTag::put_long_array(const std::string& name, std::vector<int64_t> value) {
    put(name, std::make_unique<LongArrayTag>(std::move(value)));
}

std::unique_ptr<Tag> CompoundTag::remove(const std::string& name) {
    // java: @Nullable Tag remove(String name)
    const auto it = tags_.find(name);
    if (it == tags_.end()) {
        return nullptr;
    }
    std::unique_ptr<Tag> removed = std::move(it->second);
    tags_.erase(it);
    return removed;
}

const Tag* CompoundTag::get(const std::string& name) const {
    const auto it = tags_.find(name);
    return it == tags_.end() ? nullptr : it->second.get();
}

int8_t CompoundTag::get_byte_or(const std::string& name, int8_t default_value) const {
    // java: tags.get(name) instanceof NumericTag tag ? tag.byteValue() : default
    const Tag* tag = get(name);
    const NumericTag* numeric = tag != nullptr ? tag->as_numeric() : nullptr;
    return numeric != nullptr ? numeric->as_byte() : default_value;
}

int16_t CompoundTag::get_short_or(const std::string& name, int16_t default_value) const {
    const Tag* tag = get(name);
    const NumericTag* numeric = tag != nullptr ? tag->as_numeric() : nullptr;
    return numeric != nullptr ? numeric->as_short() : default_value;
}

int32_t CompoundTag::get_int_or(const std::string& name, int32_t default_value) const {
    const Tag* tag = get(name);
    const NumericTag* numeric = tag != nullptr ? tag->as_numeric() : nullptr;
    return numeric != nullptr ? numeric->as_int() : default_value;
}

int64_t CompoundTag::get_long_or(const std::string& name, int64_t default_value) const {
    const Tag* tag = get(name);
    const NumericTag* numeric = tag != nullptr ? tag->as_numeric() : nullptr;
    return numeric != nullptr ? numeric->as_long() : default_value;
}

float CompoundTag::get_float_or(const std::string& name, float default_value) const {
    const Tag* tag = get(name);
    const NumericTag* numeric = tag != nullptr ? tag->as_numeric() : nullptr;
    return numeric != nullptr ? numeric->as_float() : default_value;
}

double CompoundTag::get_double_or(const std::string& name, double default_value) const {
    const Tag* tag = get(name);
    const NumericTag* numeric = tag != nullptr ? tag->as_numeric() : nullptr;
    return numeric != nullptr ? numeric->as_double() : default_value;
}

std::string CompoundTag::get_string_or(const std::string& name, std::string default_value) const {
    // java: tags.get(name) instanceof StringTag(String s) ? s : default
    const Tag* tag = get(name);
    const StringTag* string_tag = dynamic_cast<const StringTag*>(tag);
    return string_tag != nullptr ? string_tag->value() : default_value;
}

bool CompoundTag::get_boolean_or(const std::string& name, bool default_value) const {
    // java: getByteOr(string, (byte)(defaultValue ? 1 : 0)) != 0
    return get_byte_or(name, static_cast<int8_t>(default_value ? 1 : 0)) != 0;
}

// ============================ ListTag ============================

void ListTag::write(network::ByteBuffer& out) const {
    // from ListTag.write:
    //   byte elementType = this.identifyRawElementType();
    //   output.writeByte(elementType);
    //   output.writeInt(this.list.size());
    //   for (Tag element : this.list) wrapIfNeeded(elementType, element).write(output);
    const uint8_t element_type = identify_raw_element_type();
    out.write_byte(static_cast<int8_t>(element_type));
    out.write_int(static_cast<int32_t>(list_.size()));
    for (const auto& tag : list_) {
        wrap_if_needed(element_type, *tag)->write(out);
    }
}

std::unique_ptr<ListTag> ListTag::read(network::ByteBuffer& in, NbtAccounter& accounter) {
    // from ListTag.TYPE.load -> loadList:
    //   accountBytes(36);
    //   byte typeId = input.readByte();
    //   int count = readListCount();            // negative -> NbtFormatException
    //   if (typeId == 0 && count > 0) throw "Missing type on ListTag";
    //   accountBytes(4, count);
    //   for i: list.addAndUnwrap(type.load(input, accounter));
    const detail::DepthGuard guard(accounter);
    auto list = std::make_unique<ListTag>();
    accounter.account_bytes(36);
    const uint8_t type_id = static_cast<uint8_t>(in.read_byte());
    const int32_t count = in.read_int();  // java readListCount
    if (count < 0) {
        throw NbtFormatError("ListTag length cannot be negative: " + std::to_string(count));
    }
    if (type_id == 0 && count > 0) {
        throw NbtFormatError("Missing type on ListTag");
    }
    accounter.account_bytes(4, count);
    for (int32_t i = 0; i < count; ++i) {
        list->add_and_unwrap(read_tag_payload(static_cast<int8_t>(type_id), in, accounter));
    }
    return list;
}

uint8_t ListTag::identify_raw_element_type() const {
    // from ListTag.identifyRawElementType: first element's id; 0 when empty;
    // 10 (compound) as soon as two elements disagree — a MIXED list claims
    // compound as its element type so the {"" : x} wrappers survive.
    uint8_t homogenous_type = 0;
    for (const auto& element : list_) {
        const uint8_t element_type = static_cast<uint8_t>(element->type());
        if (homogenous_type == 0) {
            homogenous_type = element_type;
        } else if (homogenous_type != element_type) {
            return 10;
        }
    }
    return homogenous_type;
}

bool ListTag::is_wrapper(const CompoundTag& tag) {
    // from ListTag.isWrapper: size == 1 && contains("")
    return tag.size() == 1 && tag.contains("");
}

std::unique_ptr<Tag> ListTag::wrap_element(const Tag& tag) {
    // from ListTag.wrapElement: new CompoundTag(Map.of("", tag))
    auto wrapper = std::make_unique<CompoundTag>();
    wrapper->put("", tag.copy());
    return wrapper;
}

std::unique_ptr<Tag> ListTag::wrap_if_needed(uint8_t element_type, const Tag& tag) {
    // from ListTag.wrapIfNeeded:
    //   if (elementType != 10) return tag;
    //   return tag instanceof CompoundTag c && !isWrapper(c) ? c : wrapElement(tag);
    // (a self-standing compound keeps its identity even in a mixed list)
    if (element_type != 10) {
        return tag.copy();
    }
    const CompoundTag* compound = dynamic_cast<const CompoundTag*>(&tag);
    if (compound != nullptr && !is_wrapper(*compound)) {
        return tag.copy();
    }
    return wrap_element(tag);
}

void ListTag::add_and_unwrap(std::unique_ptr<Tag> tag) {
    // from ListTag.addAndUnwrap: single-entry {"" : value} compounds unwrap
    // (java tryUnwrap returns the inner tag reference; we steal ownership)
    CompoundTag* compound = dynamic_cast<CompoundTag*>(tag.get());
    if (compound != nullptr && compound->size() == 1 && compound->contains("")) {
        std::unique_ptr<Tag> inner = compound->remove("");
        list_.push_back(std::move(inner));
        return;
    }
    list_.push_back(std::move(tag));
}

std::unique_ptr<Tag> ListTag::copy() const {
    // from ListTag.copy: deep copy every element
    auto copy = std::make_unique<ListTag>();
    for (const auto& tag : list_) {
        copy->add(tag->copy());
    }
    return copy;
}

}  // namespace mc::nbt
