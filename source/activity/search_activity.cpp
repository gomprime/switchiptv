#include "activity/search_activity.hpp"

#include <algorithm>
#include <cctype>

#include "data/catalog_repository.hpp"
#include "data/xtream_client.hpp"
#include "ui/poster_row_cell.hpp"

SearchActivity::SearchActivity(iptv::StoredAuth auth,
                                std::function<void(iptv::CatalogItem, std::vector<iptv::CatalogItem>)> onSelect)
    : auth(std::move(auth)), isM3u(this->auth.mode == iptv::AuthMode::M3u), onSelect(std::move(onSelect)) {
    sectionLabels = isM3u ? std::vector<std::string>{"Resultados"}
                          : std::vector<std::string>{"Ao Vivo", "Filmes", "Séries"};
    allByTab.resize(sectionLabels.size());
    allByTabLoaded.assign(sectionLabels.size(), false);
    filteredByTab.resize(sectionLabels.size());
}

SearchActivity::~SearchActivity() {
    *alive = false;
}

brls::View* SearchActivity::createContentView() {
    brls::Box* rootBox = new brls::Box();
    rootBox->setAxis(brls::Axis::COLUMN);
    rootBox->setPadding(30, 60, 30, 60);

    brls::Label* titleLabel = new brls::Label();
    titleLabel->setFontSize(28);
    titleLabel->setText("Busca");
    titleLabel->setMarginBottom(16);
    rootBox->addView(titleLabel);

    statusLabel = new brls::Label();
    statusLabel->setFontSize(18);
    statusLabel->setText("Digite um termo para buscar...");
    statusLabel->setMarginBottom(10);
    rootBox->addView(statusLabel);

    resultsRecycler = new brls::RecyclerFrame();
    resultsRecycler->setGrow(1.0f);
    resultsRecycler->estimatedRowHeight = 290;
    resultsRecycler->registerCell("PosterRow", []() { return PosterRowCell::create(); });
    resultsRecycler->setDataSource(new ResultsDataSource(this));
    rootBox->addView(resultsRecycler);

    return rootBox;
}

void SearchActivity::onContentAvailable() {
    getContentView()->registerAction("Voltar", brls::BUTTON_B, [this](brls::View*) {
        brls::Application::popActivity();
        return true;
    });

    auto aliveFlag = alive;
    bool confirmed = brls::Application::getImeManager()->openForText(
        [this, aliveFlag](std::string text) {
            if (!*aliveFlag) return;
            runSearch(text);
        },
        "Busca", isM3u ? "Nome do canal" : "Nome do item", 60, "");

    if (!confirmed) {
        // `popActivity()` mexe na pilha de Activities — ainda estamos dentro
        // do `onContentAvailable()` desta própria Activity, chamado ANTES
        // dela ser empilhada (mesmo motivo do dialog de retomada do player:
        // ver `PlayerActivity::promptResumeIfNeeded`). Chamar direto aqui
        // faria pop na Activity errada (a que estava embaixo desta na
        // pilha). Adiar um tick garante que esta já esteja empilhada antes.
        auto aliveFlag2 = alive;
        brls::delay(1, [aliveFlag2]() {
            if (!*aliveFlag2) return;
            brls::Application::popActivity();
        });
    }
}

void SearchActivity::runSearch(const std::string& text) {
    query = text;
    if (query.empty()) {
        statusLabel->setText("Digite um termo para buscar...");
        return;
    }
    statusLabel->setText("Buscando...");
    loadThenFilter();
}

void SearchActivity::loadThenFilter() {
    if (isM3u) {
        if (allByTabLoaded[0]) {
            applyFilter();
            return;
        }
        std::string m3uUrl = auth.m3uUrl;
        auto aliveFlag = alive;
        brls::async([this, aliveFlag, m3uUrl]() {
            iptv::M3uCatalogResult result = iptv::fetchM3uCatalog(m3uUrl);
            brls::sync([this, aliveFlag, result]() {
                if (!*aliveFlag) return;
                if (result.ok) {
                    std::vector<iptv::CatalogItem> flat;
                    for (const auto& cat : result.categories) {
                        flat.insert(flat.end(), cat.channels.begin(), cat.channels.end());
                    }
                    allByTab[0] = std::move(flat);
                    allByTabLoaded[0] = true;
                }
                applyFilter();
            });
        });
        return;
    }

    bool allLoaded = true;
    for (bool loaded : allByTabLoaded) allLoaded = allLoaded && loaded;
    if (allLoaded) {
        applyFilter();
        return;
    }

    iptv::Credentials creds = auth.credentials;
    for (size_t i = 0; i < allByTabLoaded.size(); i++) {
        if (allByTabLoaded[i]) continue;
        iptv::ContentTab sectionTab = static_cast<iptv::ContentTab>(i);
        auto aliveFlag = alive;
        brls::async([this, aliveFlag, creds, sectionTab, i]() {
            // categoryId vazio = todos os itens dessa aba, sem filtro de
            // categoria.
            iptv::ItemsResult result = iptv::fetchItems(creds, sectionTab, "", {});
            brls::sync([this, aliveFlag, result, i]() {
                if (!*aliveFlag) return;
                if (result.ok) {
                    allByTab[i] = result.items;
                    allByTabLoaded[i] = true;
                }
                applyFilter();
            });
        });
    }
}

void SearchActivity::applyFilter() {
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return std::tolower(c); });

    size_t totalResults = 0;
    for (size_t i = 0; i < allByTab.size(); i++) {
        filteredByTab[i].clear();
        for (const auto& item : allByTab[i]) {
            std::string name = item.name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            if (name.find(q) != std::string::npos) filteredByTab[i].push_back(item);
        }
        totalResults += filteredByTab[i].size();
    }

    resultsRecycler->reloadData();
    statusLabel->setText(totalResults == 0 ? "Nenhum resultado encontrado." : "");
}

// ResultsDataSource

int SearchActivity::ResultsDataSource::numberOfSections(brls::RecyclerFrame*) {
    return static_cast<int>(owner->sectionLabels.size());
}

int SearchActivity::ResultsDataSource::numberOfRows(brls::RecyclerFrame*, int section) {
    size_t count = owner->filteredByTab[section].size();
    return static_cast<int>((count + PosterRowCell::kColumns - 1) / PosterRowCell::kColumns);
}

std::string SearchActivity::ResultsDataSource::titleForHeader(brls::RecyclerFrame*, int section) {
    return owner->sectionLabels[section];
}

float SearchActivity::ResultsDataSource::heightForHeader(brls::RecyclerFrame*, int) {
    // Sempre mostra o título da seção, mesmo a primeira (o padrão da lib
    // esconde a altura da seção 0 — mesmo bug já visto nos episódios de série).
    return 44;
}

brls::RecyclerCell* SearchActivity::ResultsDataSource::cellForRow(brls::RecyclerFrame* recycler,
                                                                    brls::IndexPath index) {
    PosterRowCell* cell = (PosterRowCell*)recycler->dequeueReusableCell("PosterRow");

    int section = index.section;
    std::vector<iptv::CatalogItem>& sectionItems = owner->filteredByTab[section];
    size_t rowStart = static_cast<size_t>(index.row) * PosterRowCell::kColumns;
    size_t rowEnd = std::min(rowStart + PosterRowCell::kColumns, sectionItems.size());
    std::vector<iptv::CatalogItem> itemsInRow(sectionItems.begin() + rowStart, sectionItems.begin() + rowEnd);

    SearchActivity* ownerPtr = owner;
    cell->setItems(itemsInRow, [ownerPtr, section, rowStart](int slot) {
        std::vector<iptv::CatalogItem>& items = ownerPtr->filteredByTab[section];
        size_t globalIndex = rowStart + static_cast<size_t>(slot);
        if (globalIndex >= items.size()) return;
        if (ownerPtr->onSelect) ownerPtr->onSelect(items[globalIndex], items);
    });
    return cell;
}

void SearchActivity::ResultsDataSource::didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath) {
    // Seleção acontece por pôster (ver `setItems`/callback em cellForRow).
}
