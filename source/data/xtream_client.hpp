#pragma once

#include <string>
#include <utility>
#include <vector>

namespace iptv {

// Percent-encode pra query string — exposto porque `catalog_repository`
// também monta URLs (get_live_streams&category_id=...) fora deste arquivo.
std::string urlEncode(const std::string& value);

// Porte de `xtreamClient.ts` — só o que a Fase 1 (login) precisa. As demais
// chamadas (get_live_categories, get_live_streams, etc.) entram na Fase 2.
struct Credentials {
    std::string serverUrl;
    std::string username;
    std::string password;
};

std::string normalizeServerUrl(const std::string& serverUrl);

// Monta a URL do player_api.php com username/password/action, igual `apiUrl()`.
std::string buildApiUrl(const Credentials& creds, const std::string& action = "",
                         const std::vector<std::pair<std::string, std::string>>& extraParams = {});

// Monta a URL da playlist M3U (`get.php?...&type=m3u_plus&output=ts`).
std::string buildM3uUrl(const Credentials& creds);

struct LoginResult {
    bool ok = false;
    std::string error;
};

// Sem proxy de CORS aqui (só existe pro navegador) — chama o servidor Xtream
// direto via libcurl, igual Android/Roku.
LoginResult xtreamLogin(const Credentials& creds);

// Confere se a URL responde com um conteúdo que parece playlist M3U.
LoginResult verifyM3uUrl(const std::string& m3uUrl);

// Porte de `EpgListing`/`getShortEpg` — título/descrição chegam em base64.
struct EpgListing {
    std::string title;
    std::string description;
    std::string start;
    std::string end;
    bool nowPlaying = false;
};

struct EpgResult {
    bool ok = false;
    std::string error;
    std::vector<EpgListing> listings;
};

EpgResult getShortEpg(const Credentials& creds, long streamId, int limit = 10);

// Porte de `XtreamVodInfo`/`getVodInfo`.
struct VodInfo {
    std::string plot;
    std::string cast;
    std::string director;
    std::string genre;
    std::string releaseDate;
    std::string rating;
};

struct VodInfoResult {
    bool ok = false;
    std::string error;
    VodInfo info;
};

VodInfoResult getVodInfo(const Credentials& creds, long vodId);

// Porte de `XtreamEpisode`/`XtreamSeriesInfo`/`getSeriesInfo`.
struct Episode {
    std::string id;
    int episodeNum = 0;
    std::string title;
    std::string containerExtension;
};

struct SeriesInfo {
    std::string plot;
    // vector (não map) pra preservar a ordem numérica das temporadas —
    // `nlohmann::json` ordena chaves de objeto lexicograficamente por
    // padrão ("10" viria antes de "2").
    std::vector<std::pair<std::string, std::vector<Episode>>> episodesBySeason;
};

struct SeriesInfoResult {
    bool ok = false;
    std::string error;
    SeriesInfo info;
};

SeriesInfoResult getSeriesInfo(const Credentials& creds, long seriesId);

}  // namespace iptv
