#include "data/stream_url.hpp"

#include "data/xtream_client.hpp"

namespace iptv {

std::string resolveStreamUrl(const StoredAuth& auth, const CatalogItem& item) {
    if (auth.mode == AuthMode::M3u) {
        return item.m3uUrl;
    }

    std::string kind;
    switch (item.kind) {
        case ContentKind::Live:
            kind = "live";
            break;
        case ContentKind::Vod:
            kind = "movie";
            break;
        case ContentKind::Series:
        case ContentKind::Episode:
            kind = "series";
            break;
    }

    std::string extension = item.containerExtension.empty() ? "ts" : item.containerExtension;
    std::string base = normalizeServerUrl(auth.credentials.serverUrl);
    return base + "/" + kind + "/" + auth.credentials.username + "/" + auth.credentials.password + "/" +
           std::to_string(item.streamId) + "." + extension;
}

}  // namespace iptv
