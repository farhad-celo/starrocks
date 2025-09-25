#include "exprs/celonis/agg/variant_stats_utils.h"

namespace starrocks {

rapidjson::Value EdgeStats::to_json(rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("count", count, allocator);
    obj.AddMember("count_case", count_case, allocator);
    return obj;
}

std::string EdgeStats::debug_string() const {
    std::stringstream ss;
    ss << "count " << count << " count_case " << count_case;
    return ss.str();
}

rapidjson::Value ActivityStats::to_json(rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("count", count, allocator);
    obj.AddMember("count_case", count_case, allocator);
    obj.AddMember("count_start", count_start, allocator);
    obj.AddMember("count_end", count_end, allocator);
    return obj;
}

std::string ActivityStats::debug_string() const {
    std::stringstream ss;
    ss << "count " << count << " count_case " << count_case << " count_start " << count_start << " count_end "
       << count_end;
    return ss.str();
}

} // namespace starrocks

