#pragma once

#include <string>
#include <vector>

#include "data/catalog_types.hpp"

namespace iptv {

struct M3uCategory {
    std::string name;
    std::vector<CatalogItem> channels;
};

// Porte de `m3uParser.ts::parseM3U` — já devolve `CatalogItem` prontos
// (kind=Live, m3uUrl=url do canal), igual `m3uCategoriesToItems` faz do
// lado web, pra não precisar de um tipo `M3UChannel` intermediário.
std::vector<M3uCategory> parseM3u(const std::string& text);

}  // namespace iptv
