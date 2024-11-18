#include "variant.h"

#include "rapidjson/document.h"
#include "util/hash_util.hpp"

namespace starrocks {

void Variant::add(int32_t index, size_t element_hash) {
    data.emplace_back(index);
    HashUtil::hash_combine(hash, element_hash);
}

bool Variant::equal_remap_for_testing(const Variant& other, const std::vector<int32_t>& map) const {
    if (data.size() != other.data.size()) {
        return false;
    }
    for (int i = 0; i < data.size(); i++) {
        if (data[i] != map[other.data[i]]) return false;
    }
    return true;
}

rapidjson::Value Variant::to_json(rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value a(rapidjson::kArrayType);
    for (int i = 0; i < data.size(); i++) {
        a.PushBack(data[i], allocator);
    }
    return a;
}

std::string Variant::debug_string() const {
    std::stringstream ss;
    ss << "activities (" << data.size() << ") [";
    std::string sep = "";
    for (int i = 0; i < data.size(); i++) {
        ss << sep << data[i];
        sep = ":";
    }
    ss << "] hash " << hash;
    return ss.str();
}

rapidjson::Value Edge::to_json(rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("src", src, allocator);
    obj.AddMember("dst", dst, allocator);
    return obj;
}

std::string Edge::debug_string() const {
    std::stringstream ss;
    ss << "src " << src << " dst " << dst;
    return ss.str();
}

} // namespace starrocks
