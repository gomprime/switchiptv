#pragma once

#include <string>

namespace iptv {

// Porte de `CatalogItem`/`Category` de `CatalogContext.tsx`.
enum class ContentKind { Live, Vod, Series, Episode };

struct CatalogItem {
    std::string id;
    ContentKind kind = ContentKind::Live;
    std::string name;
    std::string logo;
    std::string categoryId;
    std::string categoryName;
    long streamId = 0;  // 0 = não aplicável (ex.: canal vindo de playlist M3U)
    std::string containerExtension;
    std::string m3uUrl;  // presente quando o item vem de uma playlist M3U
};

struct Category {
    std::string id;
    std::string name;
};

}  // namespace iptv
