#include "data/xtream_client.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>

#include "data/http_client.hpp"
#include "third_party/nlohmann/json.hpp"

namespace iptv {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Xtream manda title/description da EPG em base64 — porte de
// `decodeEpgText` (que usa atob() no navegador).
std::string decodeBase64(const std::string& input) {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int lookup[256];
    std::fill(std::begin(lookup), std::end(lookup), -1);
    for (size_t i = 0; i < alphabet.size(); i++) lookup[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);

    std::string output;
    output.reserve(input.size() * 3 / 4 + 3);

    int buffer = 0;
    int bits = 0;
    for (char c : input) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int value = lookup[static_cast<unsigned char>(c)];
        if (value < 0) continue;
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return output;
}

// Alguns provedores mandam campos como `rating` em número em vez de string
// (variação comum entre implementações da API Xtream) — `.value(key, "")`
// lança exceção nesse caso porque o tipo não bate com o default. Aceita os
// dois formatos, igual já fazemos pra `auth`/`now_playing`.
std::string jsonFieldToString(const nlohmann::json& obj, const std::string& key) {
    if (!obj.contains(key)) return "";
    const auto& field = obj[key];
    if (field.is_string()) return field.get<std::string>();
    if (field.is_number()) return field.dump();
    return "";
}

}  // namespace

std::string urlEncode(const std::string& value) {
    char* escaped = curl_easy_escape(nullptr, value.c_str(), static_cast<int>(value.size()));
    std::string result = escaped ? escaped : value;
    if (escaped) curl_free(escaped);
    return result;
}

std::string normalizeServerUrl(const std::string& serverUrl) {
    std::string trimmed = trim(serverUrl);
    while (!trimmed.empty() && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return trimmed;
}

std::string buildApiUrl(const Credentials& creds, const std::string& action,
                         const std::vector<std::pair<std::string, std::string>>& extraParams) {
    std::string base = normalizeServerUrl(creds.serverUrl);
    std::string url = base + "/player_api.php?username=" + urlEncode(creds.username) +
                       "&password=" + urlEncode(creds.password);
    if (!action.empty()) {
        url += "&action=" + urlEncode(action);
    }
    for (const auto& param : extraParams) {
        url += "&" + urlEncode(param.first) + "=" + urlEncode(param.second);
    }
    return url;
}

std::string buildM3uUrl(const Credentials& creds) {
    std::string base = normalizeServerUrl(creds.serverUrl);
    return base + "/get.php?username=" + urlEncode(creds.username) + "&password=" + urlEncode(creds.password) +
           "&type=m3u_plus&output=ts";
}

LoginResult xtreamLogin(const Credentials& creds) {
    LoginResult result;
    HttpResponse response = httpGet(buildApiUrl(creds));
    if (!response.ok) {
        result.error = !response.error.empty() ? response.error : "Servidor Xtream não respondeu";
        return result;
    }

    nlohmann::json data;
    try {
        data = nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::exception&) {
        result.error = "Resposta inválida do servidor Xtream";
        return result;
    }

    auto userInfo = data.find("user_info");
    if (userInfo == data.end()) {
        result.error = "Login Xtream inválido";
        return result;
    }

    // `auth` pode vir como número ou string dependendo do provedor.
    int auth = 0;
    const auto& authField = (*userInfo)["auth"];
    if (authField.is_number()) {
        auth = authField.get<int>();
    } else if (authField.is_string()) {
        try {
            auth = std::stoi(authField.get<std::string>());
        } catch (...) {
            auth = 0;
        }
    }

    if (auth != 1) {
        result.error = "Login Xtream inválido";
        return result;
    }

    result.ok = true;
    return result;
}

EpgResult getShortEpg(const Credentials& creds, long streamId, int limit) {
    EpgResult result;
    std::vector<std::pair<std::string, std::string>> extra = {
        {"stream_id", std::to_string(streamId)},
        {"limit", std::to_string(limit)},
    };
    HttpResponse response = httpGet(buildApiUrl(creds, "get_short_epg", extra));
    if (!response.ok) {
        result.error = !response.error.empty() ? response.error : "Erro ao carregar programação";
        return result;
    }

    try {
        nlohmann::json data = nlohmann::json::parse(response.body);
        auto listingsField = data.find("epg_listings");
        if (listingsField == data.end() || !listingsField->is_array()) {
            result.ok = true;  // sem EPG disponível não é erro, só lista vazia
            return result;
        }
        for (const auto& entry : *listingsField) {
            EpgListing listing;
            listing.title = decodeBase64(jsonFieldToString(entry, "title"));
            listing.description = decodeBase64(jsonFieldToString(entry, "description"));
            listing.start = jsonFieldToString(entry, "start");
            listing.end = jsonFieldToString(entry, "end");
            // `now_playing` varia entre número e string dependendo do provedor.
            listing.nowPlaying = false;
            if (entry.contains("now_playing")) {
                const auto& nowPlayingField = entry["now_playing"];
                if (nowPlayingField.is_number()) {
                    listing.nowPlaying = nowPlayingField.get<int>() == 1;
                } else if (nowPlayingField.is_string()) {
                    listing.nowPlaying = nowPlayingField.get<std::string>() == "1";
                }
            }
            result.listings.push_back(std::move(listing));
        }
        result.ok = true;
    } catch (const nlohmann::json::exception&) {
        result.error = "Resposta inválida do servidor";
    }
    return result;
}

LoginResult verifyM3uUrl(const std::string& m3uUrl) {
    LoginResult result;
    HttpResponse response = httpGet(m3uUrl);
    if (!response.ok) {
        result.error = !response.error.empty() ? response.error : "Servidor M3U não respondeu";
        return result;
    }
    if (response.body.find("#EXTM3U") == std::string::npos && response.body.find("#EXTINF") == std::string::npos) {
        result.error = "O conteúdo retornado não parece ser uma playlist M3U válida";
        return result;
    }
    result.ok = true;
    return result;
}

VodInfoResult getVodInfo(const Credentials& creds, long vodId) {
    VodInfoResult result;
    std::vector<std::pair<std::string, std::string>> extra = {{"vod_id", std::to_string(vodId)}};
    HttpResponse response = httpGet(buildApiUrl(creds, "get_vod_info", extra));
    if (!response.ok) {
        result.error = !response.error.empty() ? response.error : "Erro ao carregar informações do filme";
        return result;
    }

    try {
        nlohmann::json data = nlohmann::json::parse(response.body);
        auto infoField = data.find("info");
        if (infoField == data.end() || !infoField->is_object()) {
            result.ok = true;  // sem detalhes não é erro
            return result;
        }
        result.info.plot = jsonFieldToString(*infoField, "plot");
        result.info.cast = jsonFieldToString(*infoField, "cast");
        result.info.director = jsonFieldToString(*infoField, "director");
        result.info.genre = jsonFieldToString(*infoField, "genre");
        result.info.releaseDate = jsonFieldToString(*infoField, "releasedate");
        result.info.rating = jsonFieldToString(*infoField, "rating");
        result.ok = true;
    } catch (const nlohmann::json::exception&) {
        result.error = "Resposta inválida do servidor";
    }
    return result;
}

SeriesInfoResult getSeriesInfo(const Credentials& creds, long seriesId) {
    SeriesInfoResult result;
    std::vector<std::pair<std::string, std::string>> extra = {{"series_id", std::to_string(seriesId)}};
    HttpResponse response = httpGet(buildApiUrl(creds, "get_series_info", extra));
    if (!response.ok) {
        result.error = !response.error.empty() ? response.error : "Erro ao carregar episódios";
        return result;
    }

    try {
        nlohmann::json data = nlohmann::json::parse(response.body);
        auto infoField = data.find("info");
        if (infoField != data.end() && infoField->is_object()) {
            result.info.plot = jsonFieldToString(*infoField, "plot");
        }

        auto episodesField = data.find("episodes");
        if (episodesField != data.end() && episodesField->is_object()) {
            // Ordena as temporadas numericamente (não lexicograficamente).
            std::vector<int> seasonNumbers;
            for (auto it = episodesField->begin(); it != episodesField->end(); ++it) {
                try {
                    seasonNumbers.push_back(std::stoi(it.key()));
                } catch (...) {
                }
            }
            std::sort(seasonNumbers.begin(), seasonNumbers.end());

            for (int season : seasonNumbers) {
                std::string seasonKey = std::to_string(season);
                const auto& episodesJson = (*episodesField)[seasonKey];
                if (!episodesJson.is_array()) continue;

                std::vector<Episode> episodes;
                for (const auto& epJson : episodesJson) {
                    Episode ep;
                    ep.id = jsonFieldToString(epJson, "id");
                    ep.title = jsonFieldToString(epJson, "title");
                    ep.containerExtension = jsonFieldToString(epJson, "container_extension");
                    if (ep.containerExtension.empty()) ep.containerExtension = "mp4";
                    const auto& epNumField = epJson.value("episode_num", nlohmann::json(0));
                    ep.episodeNum = epNumField.is_number() ? epNumField.get<int>() : 0;
                    episodes.push_back(std::move(ep));
                }
                result.info.episodesBySeason.push_back({seasonKey, std::move(episodes)});
            }
        }
        result.ok = true;
    } catch (const nlohmann::json::exception&) {
        result.error = "Resposta inválida do servidor";
    }
    return result;
}

}  // namespace iptv
