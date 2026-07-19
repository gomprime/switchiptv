#include "data/m3u_parser.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "data/text_utils.hpp"

namespace iptv {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string getAttr(const std::string& line, const std::string& key) {
    std::string pattern = key + "=\"";
    size_t pos = line.find(pattern);
    if (pos == std::string::npos) return "";
    pos += pattern.size();
    size_t end = line.find('"', pos);
    if (end == std::string::npos) return "";
    return line.substr(pos, end - pos);
}

struct PendingChannel {
    std::string name;
    std::string logo;
    std::string group;
};

}  // namespace

std::vector<M3uCategory> parseM3u(const std::string& text) {
    std::istringstream stream(text);
    std::string line;

    std::vector<std::pair<PendingChannel, std::string>> channels;
    bool hasPending = false;
    PendingChannel pending;

    while (std::getline(stream, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        if (trimmed.rfind("#EXTINF", 0) == 0) {
            pending.logo = getAttr(trimmed, "tvg-logo");
            pending.group = stripEmoji(getAttr(trimmed, "group-title"));
            if (pending.group.empty()) pending.group = "Sem categoria";
            // O nome do canal é tudo depois da primeira vírgula (os atributos
            // são separados por espaço, não vírgula, então normalmente há só
            // uma na linha) — igual `m3uParser.ts`.
            size_t commaPos = trimmed.find(',');
            pending.name = commaPos != std::string::npos ? stripEmoji(trim(trimmed.substr(commaPos + 1))) : "Sem nome";
            if (pending.name.empty()) pending.name = "Sem nome";
            hasPending = true;
            continue;
        }

        if (trimmed[0] == '#') continue;

        if (hasPending) {
            channels.emplace_back(pending, trimmed);
            hasPending = false;
        }
    }

    std::vector<M3uCategory> result;
    for (auto& entry : channels) {
        const PendingChannel& info = entry.first;
        const std::string& url = entry.second;

        auto it = std::find_if(result.begin(), result.end(),
                                [&](const M3uCategory& c) { return c.name == info.group; });
        if (it == result.end()) {
            result.push_back(M3uCategory{info.group, {}});
            it = result.end() - 1;
        }

        CatalogItem item;
        item.id = "m3u-" + url;
        item.kind = ContentKind::Live;
        item.name = info.name;
        item.logo = info.logo;
        item.categoryId = info.group;
        item.categoryName = info.group;
        item.m3uUrl = url;
        it->channels.push_back(std::move(item));
    }
    return result;
}

}  // namespace iptv
