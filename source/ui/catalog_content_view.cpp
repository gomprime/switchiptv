#include "ui/catalog_content_view.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

#include "data/xtream_client.hpp"
#include "ui/list_cell.hpp"
#include "ui/poster_row_cell.hpp"

CatalogContentView::CatalogContentView(iptv::StoredAuth auth, iptv::ContentTab tab,
                                        std::function<void(iptv::CatalogItem, std::vector<iptv::CatalogItem>)> onSelect)
    : auth(std::move(auth)), tab(tab), isM3u(this->auth.mode == iptv::AuthMode::M3u), onSelect(std::move(onSelect)) {
    // `grow`/stretch (não percentage) é o padrão que a própria borealis usa
    // em todo lugar pra preencher espaço disponível — ver histórico de bugs
    // no plano antes de mexer nisso de novo.
    this->setAxis(brls::Axis::COLUMN);

    // Header acima das colunas de categorias/itens: "Surpreenda-me" (quando
    // aplicável) à esquerda, bateria/wifi/relógio à direita (reaproveitando
    // os mesmos widgets que `brls::BottomBar` usa no rodapé padrão da
    // borealis). A busca em si mora em `SearchActivity` (botão R, registrado
    // globalmente por `CatalogActivity`) — não tem mais campo aqui.
    brls::Box* headerBox = new brls::Box();
    headerBox->setAxis(brls::Axis::ROW);
    headerBox->setHeight(70);
    headerBox->setAlignItems(brls::AlignItems::CENTER);
    headerBox->setPadding(0, 20, 0, 20);

    // "Surpreenda-me": só faz sentido na aba Filmes, sorteando entre todos
    // os filmes do provedor — porte de `ContentGrid.tsx`/`CatalogContext.tsx`.
    if (!isM3u && tab == iptv::ContentTab::Vod) {
        brls::Button* surpriseButton = (brls::Button*)brls::Button::create();
        surpriseButton->setText("Surpreenda-me");
        surpriseButton->registerClickAction([this](brls::View*) {
            pickSurpriseVod();
            return true;
        });
        headerBox->addView(surpriseButton);
    }

    brls::Box* statusBox = new brls::Box();
    statusBox->setAxis(brls::Axis::ROW);
    statusBox->setAlignItems(brls::AlignItems::CENTER);
    statusBox->setGrow(1.0f);
    statusBox->setJustifyContent(brls::JustifyContent::FLEX_END);

    brls::Platform* platform = brls::Application::getPlatform();

    brls::View* battery = brls::BatteryWidget::create();
    battery->setMarginRight(21);
    battery->setVisibility(platform->canShowBatteryLevel() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    statusBox->addView(battery);

    brls::View* wireless = brls::WirelessWidget::create();
    wireless->setMarginRight(21);
    wireless->setVisibility(platform->canShowWirelessLevel() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    statusBox->addView(wireless);

    clockLabel = new brls::Label();
    clockLabel->setFontSize(21.5f);
    clockLabel->setVerticalAlign(brls::VerticalAlign::CENTER);
    statusBox->addView(clockLabel);

    headerBox->addView(statusBox);
    this->addView(headerBox);

    // Linha com as colunas de categorias (esquerda) + itens (direita).
    contentRow = new brls::Box();
    contentRow->setAxis(brls::Axis::ROW);
    contentRow->setGrow(1.0f);

    // Coluna da esquerda: categorias.
    brls::Box* categoriesBox = new brls::Box();
    categoriesBox->setAxis(brls::Axis::COLUMN);
    categoriesBox->setWidth(280);

    categoriesStatusLabel = new brls::Label();
    categoriesStatusLabel->setFontSize(16);
    categoriesStatusLabel->setText("Carregando...");
    categoriesStatusLabel->setMargins(10, 10, 0, 10);
    categoriesBox->addView(categoriesStatusLabel);

    categoriesRecycler = new brls::RecyclerFrame();
    categoriesRecycler->setGrow(1.0f);
    categoriesRecycler->estimatedRowHeight = 42;
    categoriesRecycler->registerCell("Cell", []() { return ListCell::create(); });
    categoriesRecycler->setDataSource(new CategoriesDataSource(this));
    categoriesBox->addView(categoriesRecycler);
    contentRow->addView(categoriesBox);

    // Coluna da direita: grade de pôsteres da categoria selecionada.
    brls::Box* itemsBox = new brls::Box();
    itemsBox->setAxis(brls::Axis::COLUMN);
    itemsBox->setGrow(1.0f);
    itemsBox->setPadding(10, 10, 10, 10);

    itemsStatusLabel = new brls::Label();
    itemsStatusLabel->setFontSize(20);
    itemsBox->addView(itemsStatusLabel);

    itemsRecycler = new brls::RecyclerFrame();
    itemsRecycler->setGrow(1.0f);
    itemsRecycler->estimatedRowHeight = 290;
    itemsRecycler->registerCell("PosterRow", []() { return PosterRowCell::create(); });
    itemsRecycler->setDataSource(new ItemsDataSource(this));
    itemsBox->addView(itemsRecycler);
    contentRow->addView(itemsBox);

    this->addView(contentRow);

    setItemsStatus("Selecione uma categoria à esquerda.");
    loadCategories();
    updateClock();
}

void CatalogContentView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                               brls::FrameContext* ctx) {
    updateClock();
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

void CatalogContentView::updateClock() {
    auto timeNow = std::chrono::system_clock::now();
    auto inTimeT = std::chrono::system_clock::to_time_t(timeNow);
    auto tm = *std::localtime(&inTimeT);

    std::stringstream ss;
    ss << std::put_time(&tm, "%H:%M");
    if (ss.str() != lastClockText) {
        lastClockText = ss.str();
        clockLabel->setText(lastClockText);
    }
}

void CatalogContentView::setItemsStatus(const std::string& text) {
    itemsStatusLabel->setText(text);
    itemsStatusLabel->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void CatalogContentView::loadCategories() {
    categoriesStatusLabel->setText("Carregando...");
    categoriesStatusLabel->setVisibility(brls::Visibility::VISIBLE);

    // RecyclerFrame/TabFrame podem destruir esta view no meio de uma busca
    // (troca rápida de aba) — usa o mesmo par retain/release que a própria
    // borealis usa em `Image::setImageAsync` pra não tocar `this` depois de
    // destruído.
    if (isM3u) {
        std::string m3uUrl = auth.m3uUrl;
        ASYNC_RETAIN
        brls::async([ASYNC_TOKEN, m3uUrl]() {
            iptv::M3uCatalogResult result = iptv::fetchM3uCatalog(m3uUrl);
            brls::sync([ASYNC_TOKEN, result]() {
                ASYNC_RELEASE
                if (!result.ok) {
                    categoriesStatusLabel->setText(result.error);
                    setItemsStatus(result.error);
                    return;
                }
                categoriesStatusLabel->setVisibility(brls::Visibility::GONE);
                m3uCache = result.categories;
                categories.clear();
                for (const auto& cat : m3uCache) {
                    categories.push_back(iptv::Category{cat.name, cat.name});
                }
                categoriesRecycler->reloadData();
            });
        });
        return;
    }

    iptv::Credentials creds = auth.credentials;
    iptv::ContentTab contentTab = tab;
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, creds, contentTab]() {
        iptv::CategoriesResult result = iptv::fetchCategories(creds, contentTab);
        brls::sync([ASYNC_TOKEN, result]() {
            ASYNC_RELEASE
            if (!result.ok) {
                categoriesStatusLabel->setText(result.error);
                setItemsStatus(result.error);
                return;
            }
            categoriesStatusLabel->setVisibility(brls::Visibility::GONE);
            categories = result.categories;
            categoriesRecycler->reloadData();
        });
    });
}

void CatalogContentView::selectCategory(size_t index) {
    if (index >= categories.size()) return;
    const std::string categoryId = categories[index].id;

    if (isM3u) {
        for (const auto& cat : m3uCache) {
            if (cat.name == categoryId) {
                items = cat.channels;
                itemsRecycler->reloadData();
                setItemsStatus(items.empty() ? "Nada por aqui ainda." : "");
                return;
            }
        }
        items.clear();
        itemsRecycler->reloadData();
        setItemsStatus("Nada por aqui ainda.");
        return;
    }

    setItemsStatus("Carregando...");
    iptv::Credentials creds = auth.credentials;
    iptv::ContentTab contentTab = tab;
    std::vector<iptv::Category> categoriesCopy = categories;
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, creds, contentTab, categoryId, categoriesCopy]() {
        iptv::ItemsResult result = iptv::fetchItems(creds, contentTab, categoryId, categoriesCopy);
        brls::sync([ASYNC_TOKEN, result]() {
            ASYNC_RELEASE
            if (!result.ok) {
                setItemsStatus(result.error);
                return;
            }
            items = result.items;
            itemsRecycler->reloadData();
            setItemsStatus(items.empty() ? "Nada por aqui ainda." : "");
        });
    });
}

void CatalogContentView::pickSurpriseVod() {
    int vodIndex = static_cast<int>(iptv::ContentTab::Vod);
    if (allByTabLoaded[vodIndex]) {
        applySurprise();
        return;
    }

    setItemsStatus("Sorteando...");
    iptv::Credentials creds = auth.credentials;
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, creds, vodIndex]() {
        iptv::ItemsResult result = iptv::fetchItems(creds, iptv::ContentTab::Vod, "", {});
        brls::sync([ASYNC_TOKEN, result, vodIndex]() {
            ASYNC_RELEASE
            if (!result.ok) {
                setItemsStatus(result.error);
                return;
            }
            allByTab[vodIndex] = result.items;
            allByTabLoaded[vodIndex] = true;
            applySurprise();
        });
    });
}

void CatalogContentView::applySurprise() {
    std::vector<iptv::CatalogItem>& vodItems = allByTab[static_cast<int>(iptv::ContentTab::Vod)];
    if (vodItems.empty()) {
        setItemsStatus("Nenhum filme encontrado pra sortear.");
        return;
    }
    setItemsStatus("");
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, vodItems.size() - 1);
    const iptv::CatalogItem& picked = vodItems[dist(rng)];
    if (onSelect) onSelect(picked, vodItems);
}

// CategoriesDataSource

int CatalogContentView::CategoriesDataSource::numberOfRows(brls::RecyclerFrame*, int) {
    return static_cast<int>(owner->categories.size());
}

brls::RecyclerCell* CatalogContentView::CategoriesDataSource::cellForRow(brls::RecyclerFrame* recycler,
                                                                          brls::IndexPath index) {
    ListCell* cell = (ListCell*)recycler->dequeueReusableCell("Cell");
    cell->title->setText(owner->categories[index.row].name);
    cell->title->setFontSize(16);
    return cell;
}

void CatalogContentView::CategoriesDataSource::didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) {
    owner->selectCategory(index.row);
}

// ItemsDataSource

int CatalogContentView::ItemsDataSource::numberOfRows(brls::RecyclerFrame*, int) {
    size_t count = owner->items.size();
    return static_cast<int>((count + PosterRowCell::kColumns - 1) / PosterRowCell::kColumns);
}

brls::RecyclerCell* CatalogContentView::ItemsDataSource::cellForRow(brls::RecyclerFrame* recycler,
                                                                     brls::IndexPath index) {
    PosterRowCell* cell = (PosterRowCell*)recycler->dequeueReusableCell("PosterRow");

    size_t rowStart = static_cast<size_t>(index.row) * PosterRowCell::kColumns;
    size_t rowEnd = std::min(rowStart + PosterRowCell::kColumns, owner->items.size());
    std::vector<iptv::CatalogItem> itemsInRow(owner->items.begin() + rowStart, owner->items.begin() + rowEnd);

    CatalogContentView* ownerPtr = owner;
    cell->setItems(itemsInRow, [ownerPtr, rowStart](int slot) {
        size_t globalIndex = rowStart + static_cast<size_t>(slot);
        if (globalIndex >= ownerPtr->items.size()) return;
        if (ownerPtr->onSelect) ownerPtr->onSelect(ownerPtr->items[globalIndex], ownerPtr->items);
    });
    return cell;
}

void CatalogContentView::ItemsDataSource::didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath) {
    // Seleção acontece por pôster (ver `setItems`/callback em cellForRow),
    // não pela linha inteira.
}
