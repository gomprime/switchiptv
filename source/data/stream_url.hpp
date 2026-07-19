#pragma once

#include <string>

#include "data/catalog_types.hpp"
#include "data/storage.hpp"

namespace iptv {

// Porte de `streamUrl.ts::buildXtreamStreamUrl` + a lógica de
// `App.tsx::resolveOriginalUrl` — decide a URL final a partir do modo de
// login (Xtream monta `/live|movie|series/user/pass/id.ext`; M3U já tem o
// link pronto em `item.m3uUrl`).
std::string resolveStreamUrl(const StoredAuth& auth, const CatalogItem& item);

}  // namespace iptv
