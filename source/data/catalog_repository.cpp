#include "data/catalog_repository.hpp"

#include "data/http_client.hpp"
#include "data/text_utils.hpp"
#include "data/xtream_client.hpp"
#include "third_party/nlohmann/json.hpp"

namespace iptv {

namespace {

std::string actionForCategories(ContentTab tab) {
    switch (tab) {
        case ContentTab::Live:
            return "get_live_categories";
        case ContentTab::Vod:
            return "get_vod_categories";
        case ContentTab::Series:
            return "get_series_categories";
    }
    return "";
}

std::string actionForItems(ContentTab tab) {
    switch (tab) {
        case ContentTab::Live:
            return "get_live_streams";
        case ContentTab::Vod:
            return "get_vod_streams";
        case ContentTab::Series:
            return "get_series";
    }
    return "";
}

// Providers variam entre mandar `stream_id`/`series_id` como número ou como
// string — aceita os dois, igual o `Number(...)` implícito do lado web.
long jsonToLong(const nlohmann::json& value) {
    if (value.is_number()) return value.get<long>();
    if (value.is_string()) {
        try {
            return std::stol(value.get<std::string>());
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

std::string jsonToString(const nlohmann::json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number()) return value.dump();
    return "";
}

std::string categoryNameFor(const std::vector<Category>& categories, const std::string& categoryId) {
    for (const auto& c : categories) {
        if (c.id == categoryId) return c.name;
    }
    return "";
}

}  // namespace

CategoriesResult fetchCategories(const Credentials& creds, ContentTab tab) {
    CategoriesResult result;
    HttpResponse response = httpGet(buildApiUrl(creds, actionForCategories(tab)));
    if (!response.ok) {
        result.error = !response.error.empty() ? response.error : "Erro ao carregar categorias";
        return result;
    }

    try {
        nlohmann::json data = nlohmann::json::parse(response.body);
        if (!data.is_array()) {
            result.error = "Resposta inválida do servidor";
            return result;
        }
        for (const auto& entry : data) {
            Category category;
            category.id = jsonToString(entry.value("category_id", nlohmann::json("")));
            category.name = stripEmoji(jsonToString(entry.value("category_name", nlohmann::json(""))));
            result.categories.push_back(std::move(category));
        }
        result.ok = true;
    } catch (const nlohmann::json::exception&) {
        result.error = "Resposta inválida do servidor";
    }
    return result;
}

ItemsResult fetchItems(const Credentials& creds, ContentTab tab, const std::string& categoryId,
                        const std::vector<Category>& categories) {
    ItemsResult result;
    std::vector<std::pair<std::string, std::string>> extra;
    if (!categoryId.empty()) extra.push_back({"category_id", categoryId});

    HttpResponse response = httpGet(buildApiUrl(creds, actionForItems(tab), extra));
    if (!response.ok) {
        result.error = !response.error.empty() ? response.error : "Erro ao carregar conteúdo";
        return result;
    }

    try {
        nlohmann::json data = nlohmann::json::parse(response.body);
        if (!data.is_array()) {
            result.error = "Resposta inválida do servidor";
            return result;
        }
        for (const auto& entry : data) {
            CatalogItem item;
            std::string entryCategoryId = jsonToString(entry.value("category_id", nlohmann::json("")));
            item.categoryId = entryCategoryId;
            item.categoryName = categoryNameFor(categories, entryCategoryId);
            item.name = stripEmoji(jsonToString(entry.value("name", nlohmann::json(""))));
            if (item.name.empty()) item.name = "Sem nome";

            if (tab == ContentTab::Live) {
                item.kind = ContentKind::Live;
                item.streamId = jsonToLong(entry.value("stream_id", nlohmann::json(0)));
                item.id = "live-" + std::to_string(item.streamId);
                item.logo = jsonToString(entry.value("stream_icon", nlohmann::json("")));
                item.containerExtension = jsonToString(entry.value("container_extension", nlohmann::json("")));
                if (item.containerExtension.empty()) item.containerExtension = "ts";
            } else if (tab == ContentTab::Vod) {
                item.kind = ContentKind::Vod;
                item.streamId = jsonToLong(entry.value("stream_id", nlohmann::json(0)));
                item.id = "vod-" + std::to_string(item.streamId);
                item.logo = jsonToString(entry.value("stream_icon", nlohmann::json("")));
                item.containerExtension = jsonToString(entry.value("container_extension", nlohmann::json("")));
                if (item.containerExtension.empty()) item.containerExtension = "mp4";
            } else {
                item.kind = ContentKind::Series;
                item.streamId = jsonToLong(entry.value("series_id", nlohmann::json(0)));
                item.id = "series-" + std::to_string(item.streamId);
                item.logo = jsonToString(entry.value("cover", nlohmann::json("")));
            }

            result.items.push_back(std::move(item));
        }
        result.ok = true;
    } catch (const nlohmann::json::exception&) {
        result.error = "Resposta inválida do servidor";
    }
    return result;
}

M3uCatalogResult fetchM3uCatalog(const std::string& m3uUrl) {
    M3uCatalogResult result;
    HttpResponse response = httpGet(m3uUrl);
    if (!response.ok) {
        result.error = !response.error.empty() ? response.error : "Erro ao carregar playlist M3U";
        return result;
    }
    result.categories = parseM3u(response.body);
    result.ok = true;
    return result;
}

}  // namespace iptv
