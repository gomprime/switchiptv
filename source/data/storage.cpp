#include "data/storage.hpp"

#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

#include "third_party/nlohmann/json.hpp"

namespace iptv {

namespace {

#ifdef __SWITCH__
const char* kDataPath = "sdmc:/switch/iptv-player/data.json";

void ensureDataDir() {
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/iptv-player", 0777);
}
#else
// Build de Desktop (iteração rápida, sem precisar do Switch toda hora) —
// grava ao lado do executável, sem depender de cartão SD nenhum.
const char* kDataPath = "iptv-player-data.json";

void ensureDataDir() {}
#endif

const int kMaxHistory = 50;

std::string readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return "";
    std::string content;
    char buffer[4096];
    size_t n = 0;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        content.append(buffer, n);
    }
    fclose(file);
    return content;
}

// Todo o estado local (sessão/favoritos/histórico) mora num único arquivo
// JSON, com uma chave por seção — mais simples do que 3 arquivos separados,
// já que não temos algo como localStorage aqui.
nlohmann::json loadRoot() {
    std::string raw = readFile(kDataPath);
    if (raw.empty()) return nlohmann::json::object();
    try {
        nlohmann::json root = nlohmann::json::parse(raw);
        if (!root.is_object()) return nlohmann::json::object();
        return root;
    } catch (const nlohmann::json::exception&) {
        return nlohmann::json::object();
    }
}

void saveRoot(const nlohmann::json& root) {
    ensureDataDir();
    FILE* file = fopen(kDataPath, "wb");
    if (!file) {
        // stdout/stderr chegam no console do nxlink — sem isso uma falha de
        // escrita no SD passaria batida e o usuário só veria "não salvou".
        printf("[storage] ERRO: fopen falhou ao salvar %s\n", kDataPath);
        return;
    }
    std::string dumped = root.dump();
    size_t written = fwrite(dumped.data(), 1, dumped.size(), file);
    fclose(file);
    if (written != dumped.size()) {
        printf("[storage] ERRO: escrita parcial (%zu de %zu bytes)\n", written, dumped.size());
    }
}

std::string kindToString(ContentKind kind) {
    switch (kind) {
        case ContentKind::Live:
            return "live";
        case ContentKind::Vod:
            return "vod";
        case ContentKind::Series:
            return "series";
        case ContentKind::Episode:
            return "episode";
    }
    return "live";
}

ContentKind kindFromString(const std::string& kind) {
    if (kind == "vod") return ContentKind::Vod;
    if (kind == "series") return ContentKind::Series;
    if (kind == "episode") return ContentKind::Episode;
    return ContentKind::Live;
}

}  // namespace

nlohmann::json favoriteItemToJson(const FavoriteItem& item) {
    nlohmann::json j;
    j["id"] = item.id;
    j["kind"] = kindToString(item.kind);
    j["name"] = item.name;
    if (!item.logo.empty()) j["logo"] = item.logo;
    j["categoryName"] = item.categoryName;
    if (item.streamId != 0) j["streamId"] = item.streamId;
    if (!item.containerExtension.empty()) j["containerExtension"] = item.containerExtension;
    if (!item.url.empty()) j["url"] = item.url;
    return j;
}

FavoriteItem favoriteItemFromJson(const nlohmann::json& j) {
    FavoriteItem item;
    item.id = j.value("id", "");
    item.kind = kindFromString(j.value("kind", "live"));
    item.name = j.value("name", "");
    item.logo = j.value("logo", "");
    item.categoryName = j.value("categoryName", "");
    item.streamId = j.value("streamId", 0L);
    item.containerExtension = j.value("containerExtension", "");
    item.url = j.value("url", "");
    return item;
}

nlohmann::json historyEntryToJson(const HistoryEntry& entry) {
    nlohmann::json j = favoriteItemToJson(entry);
    j["watchedAt"] = entry.watchedAt;
    if (entry.positionSeconds >= 0) j["positionSeconds"] = entry.positionSeconds;
    if (entry.durationSeconds >= 0) j["durationSeconds"] = entry.durationSeconds;
    return j;
}

HistoryEntry historyEntryFromJson(const nlohmann::json& j) {
    HistoryEntry entry;
    static_cast<FavoriteItem&>(entry) = favoriteItemFromJson(j);
    entry.watchedAt = j.value("watchedAt", 0LL);
    entry.positionSeconds = j.value("positionSeconds", -1.0);
    entry.durationSeconds = j.value("durationSeconds", -1.0);
    return entry;
}

void saveAuth(const StoredAuth& auth) {
    nlohmann::json root = loadRoot();

    nlohmann::json j;
    j["mode"] = auth.mode == AuthMode::Xtream ? "xtream" : "m3u";
    j["credentials"] = {
        {"serverUrl", auth.credentials.serverUrl},
        {"username", auth.credentials.username},
        {"password", auth.credentials.password},
    };
    if (auth.mode == AuthMode::M3u) {
        j["m3uUrl"] = auth.m3uUrl;
    }
    root["auth"] = j;
    saveRoot(root);
}

std::optional<StoredAuth> loadAuth() {
    nlohmann::json root = loadRoot();
    auto it = root.find("auth");
    if (it == root.end() || !it->is_object()) return std::nullopt;
    const nlohmann::json& j = *it;

    if (!j.contains("mode") || !j.contains("credentials")) return std::nullopt;

    StoredAuth auth;
    auth.mode = j["mode"].get<std::string>() == "m3u" ? AuthMode::M3u : AuthMode::Xtream;
    const auto& creds = j["credentials"];
    auth.credentials.serverUrl = creds.value("serverUrl", "");
    auth.credentials.username = creds.value("username", "");
    auth.credentials.password = creds.value("password", "");
    auth.m3uUrl = j.value("m3uUrl", "");
    return auth;
}

void clearAuth() {
    nlohmann::json root = loadRoot();
    root.erase("auth");
    saveRoot(root);
}

FavoriteItem toFavoriteItem(const CatalogItem& item) {
    FavoriteItem f;
    f.id = item.id;
    f.kind = item.kind;
    f.name = item.name;
    f.logo = item.logo;
    f.categoryName = item.categoryName;
    f.streamId = item.streamId;
    f.containerExtension = item.containerExtension;
    f.url = item.m3uUrl;
    return f;
}

CatalogItem fromFavoriteItem(const FavoriteItem& f) {
    CatalogItem item;
    item.id = f.id;
    item.kind = f.kind;
    item.name = f.name;
    item.logo = f.logo;
    item.categoryId = f.categoryName;
    item.categoryName = f.categoryName;
    item.streamId = f.streamId;
    item.containerExtension = f.containerExtension;
    item.m3uUrl = f.url;
    return item;
}

std::vector<FavoriteItem> loadFavorites() {
    nlohmann::json root = loadRoot();
    std::vector<FavoriteItem> result;
    auto it = root.find("favorites");
    if (it == root.end() || !it->is_array()) return result;
    for (const auto& entry : *it) {
        result.push_back(favoriteItemFromJson(entry));
    }
    return result;
}

bool isFavorite(const std::string& id) {
    auto favorites = loadFavorites();
    return std::any_of(favorites.begin(), favorites.end(), [&](const FavoriteItem& f) { return f.id == id; });
}

std::vector<FavoriteItem> toggleFavorite(const FavoriteItem& item) {
    auto current = loadFavorites();
    auto it = std::find_if(current.begin(), current.end(), [&](const FavoriteItem& f) { return f.id == item.id; });
    if (it != current.end()) {
        current.erase(it);
    } else {
        current.push_back(item);
    }
    replaceFavorites(current);
    return current;
}

void replaceFavorites(const std::vector<FavoriteItem>& items) {
    nlohmann::json root = loadRoot();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& item : items) arr.push_back(favoriteItemToJson(item));
    root["favorites"] = arr;
    saveRoot(root);
}

std::vector<HistoryEntry> loadHistory() {
    nlohmann::json root = loadRoot();
    std::vector<HistoryEntry> result;
    auto it = root.find("history");
    if (it == root.end() || !it->is_array()) return result;
    for (const auto& entry : *it) {
        result.push_back(historyEntryFromJson(entry));
    }
    return result;
}

namespace {
void saveHistory(std::vector<HistoryEntry> entries) {
    if (entries.size() > static_cast<size_t>(kMaxHistory)) {
        entries.resize(kMaxHistory);
    }
    nlohmann::json root = loadRoot();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& entry : entries) arr.push_back(historyEntryToJson(entry));
    root["history"] = arr;
    saveRoot(root);
}
}  // namespace

void recordHistory(const FavoriteItem& item) {
    auto current = loadHistory();
    auto existing = std::find_if(current.begin(), current.end(), [&](const HistoryEntry& h) { return h.id == item.id; });

    HistoryEntry entry;
    static_cast<FavoriteItem&>(entry) = item;
    entry.watchedAt = static_cast<long long>(time(nullptr)) * 1000;
    if (existing != current.end()) {
        entry.positionSeconds = existing->positionSeconds;
        entry.durationSeconds = existing->durationSeconds;
        current.erase(existing);
    }

    std::vector<HistoryEntry> next;
    next.push_back(entry);
    next.insert(next.end(), current.begin(), current.end());
    saveHistory(std::move(next));
}

void updateProgress(const std::string& id, double positionSeconds, double durationSeconds) {
    auto current = loadHistory();
    auto it = std::find_if(current.begin(), current.end(), [&](const HistoryEntry& h) { return h.id == id; });
    if (it == current.end()) return;
    it->positionSeconds = positionSeconds;
    it->durationSeconds = durationSeconds;
    saveHistory(current);
}

std::optional<Progress> getProgress(const std::string& id) {
    auto history = loadHistory();
    auto it = std::find_if(history.begin(), history.end(), [&](const HistoryEntry& h) { return h.id == id; });
    if (it == history.end() || it->positionSeconds < 0 || it->durationSeconds <= 0) return std::nullopt;
    Progress p;
    p.positionSeconds = it->positionSeconds;
    p.durationSeconds = it->durationSeconds;
    return p;
}

void clearHistory() {
    nlohmann::json root = loadRoot();
    root.erase("history");
    saveRoot(root);
}

void removeHistoryItem(const std::string& id) {
    auto current = loadHistory();
    current.erase(std::remove_if(current.begin(), current.end(), [&](const HistoryEntry& h) { return h.id == id; }),
                  current.end());
    saveHistory(current);
}

void replaceHistory(const std::vector<HistoryEntry>& entries) {
    saveHistory(entries);
}

}  // namespace iptv
