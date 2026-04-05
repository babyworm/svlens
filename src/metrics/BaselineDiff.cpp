#include "BaselineDiff.h"

#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace metrics {

namespace {

// Minimal JSON value extraction — finds "key": value in a flat object context.
// Handles string, number, and bool values. Not a full parser.
std::string extractJsonString(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

int64_t extractJsonInt(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    try {
        return std::stoll(json.substr(pos));
    } catch (...) {
        return 0;
    }
}

struct BaselineRoot {
    std::string root_id;
    std::string root_kind;
    uint32_t raw_node_count = 0;
    uint32_t logic_depth_est = 0;
    uint32_t normalized_transform_count = 0;
};

// Parse roots array from baseline JSON. Simple block-level extraction.
std::vector<BaselineRoot> parseBaselineRoots(const std::string& json) {
    std::vector<BaselineRoot> roots;
    auto arrStart = json.find("\"roots\"");
    if (arrStart == std::string::npos) return roots;
    arrStart = json.find('[', arrStart);
    if (arrStart == std::string::npos) return roots;
    auto arrEnd = json.find(']', arrStart);
    if (arrEnd == std::string::npos) return roots;

    // Find each { ... } block within the array
    size_t pos = arrStart;
    while (pos < arrEnd) {
        auto objStart = json.find('{', pos);
        if (objStart == std::string::npos || objStart >= arrEnd) break;
        auto objEnd = json.find('}', objStart);
        if (objEnd == std::string::npos) break;

        std::string obj = json.substr(objStart, objEnd - objStart + 1);
        BaselineRoot r;
        r.root_id = extractJsonString(obj, "root_id");
        r.root_kind = extractJsonString(obj, "root_kind");
        r.raw_node_count = static_cast<uint32_t>(extractJsonInt(obj, "raw_node_count"));
        r.logic_depth_est = static_cast<uint32_t>(extractJsonInt(obj, "logic_depth_est"));
        r.normalized_transform_count = static_cast<uint32_t>(extractJsonInt(obj, "normalized_transform_count"));
        roots.push_back(r);

        pos = objEnd + 1;
    }
    return roots;
}

} // anonymous namespace

BaselineDiffResult computeBaselineDiff(
    const std::string& baselineFile,
    const std::vector<std::tuple<std::string, std::string, uint32_t, uint32_t, uint32_t>>& currentRoots) {

    BaselineDiffResult result;

    std::ifstream ifs(baselineFile);
    if (!ifs) {
        result.has_baseline = false;
        return result;
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string json = oss.str();
    result.has_baseline = true;

    auto baseRoots = parseBaselineRoots(json);

    // Build baseline map: "kind/id" -> root
    std::map<std::string, BaselineRoot> baseMap;
    for (auto& br : baseRoots)
        baseMap[br.root_kind + "/" + br.root_id] = br;

    // Compare
    for (auto& [id, kind, raw, depth, norm] : currentRoots) {
        RootDiff diff;
        diff.root_id = id;
        diff.root_kind = kind;

        auto key = kind + "/" + id;
        auto it = baseMap.find(key);
        if (it == baseMap.end()) {
            diff.is_new = true;
            diff.raw_delta = static_cast<int32_t>(raw);
            diff.norm_delta = static_cast<int32_t>(norm);
            ++result.new_roots;
        } else {
            auto& base = it->second;
            diff.raw_delta = static_cast<int32_t>(raw) - static_cast<int32_t>(base.raw_node_count);
            diff.depth_delta = static_cast<int32_t>(depth) - static_cast<int32_t>(base.logic_depth_est);
            diff.norm_delta = static_cast<int32_t>(norm) - static_cast<int32_t>(base.normalized_transform_count);
            if (diff.raw_delta > 0) ++result.regressions;
            else if (diff.raw_delta < 0) ++result.improvements;
            baseMap.erase(it);
        }
        result.total_raw_delta += diff.raw_delta;
        result.total_norm_delta += diff.norm_delta;
        result.root_diffs.push_back(std::move(diff));
    }

    // Remaining baseline roots were removed
    for (auto& [key, base] : baseMap) {
        RootDiff diff;
        diff.root_id = base.root_id;
        diff.root_kind = base.root_kind;
        diff.is_removed = true;
        diff.raw_delta = -static_cast<int32_t>(base.raw_node_count);
        ++result.removed_roots;
        result.root_diffs.push_back(std::move(diff));
    }

    return result;
}

} // namespace metrics
