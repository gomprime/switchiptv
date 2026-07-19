#include "ui/local_list_content_view.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "ui/list_cell.hpp"

LocalListContentView::LocalListContentView(std::vector<iptv::FavoriteItem> items,
                                            std::function<void(iptv::CatalogItem, std::vector<iptv::CatalogItem>)> onSelect,
                                            std::string emptyMessage,
                                            std::function<void(const iptv::FavoriteItem&)> onRemove,
                                            std::string removeHint,
                                            std::string listTitle)
    : allItems(std::move(items)), displayedItems(allItems), onSelect(std::move(onSelect)),
      emptyMessage(std::move(emptyMessage)), onRemove(std::move(onRemove)), removeHint(std::move(removeHint)),
      listTitle(std::move(listTitle)) {
    this->setAxis(brls::Axis::COLUMN);

    // Header: busca à esquerda, bateria/wifi/relógio à direita — mesmo
    // padrão do header de `CatalogContentView`.
    brls::Box* headerBox = new brls::Box();
    headerBox->setAxis(brls::Axis::ROW);
    headerBox->setHeight(70);
    headerBox->setAlignItems(brls::AlignItems::CENTER);
    headerBox->setPadding(0, 20, 0, 20);

    brls::InputCell* searchCell = new brls::InputCell();
    searchCell->init("Buscar", "", [this](std::string text) { onSearchChanged(text); }, "Nome do item salvo");
    searchCell->setGrow(1.0f);
    headerBox->addView(searchCell);

    brls::Box* statusBox = new brls::Box();
    statusBox->setAxis(brls::Axis::ROW);
    statusBox->setAlignItems(brls::AlignItems::CENTER);
    statusBox->setMarginLeft(20);

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

    // Conteúdo: status (mensagem vazia/nada encontrado) + lista.
    statusLabel = new brls::Label();
    statusLabel->setFontSize(18);
    statusLabel->setMargins(20, 20, 0, 20);
    this->addView(statusLabel);

    recycler = new brls::RecyclerFrame();
    recycler->setGrow(1.0f);
    recycler->estimatedRowHeight = 50;
    recycler->registerCell("Cell", []() { return ListCell::create(); });
    recycler->setDataSource(new ItemsDataSource(this));
    this->addView(recycler);

    setStatus(allItems.empty() ? emptyMessage : "");
    updateClock();
}

void LocalListContentView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                                 brls::FrameContext* ctx) {
    updateClock();
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

void LocalListContentView::openSearch() {
    brls::Application::getImeManager()->openForText(
        [this](std::string text) { onSearchChanged(text); }, "Buscar", "Nome do item salvo", 60, searchQuery);
}

void LocalListContentView::updateClock() {
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

void LocalListContentView::setStatus(const std::string& text) {
    statusLabel->setText(text);
    statusLabel->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void LocalListContentView::onSearchChanged(const std::string& query) {
    searchQuery = query;
    applySearchFilter();
}

void LocalListContentView::applySearchFilter() {
    std::string query = searchQuery;
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return std::tolower(c); });

    if (query.empty()) {
        displayedItems = allItems;
    } else {
        displayedItems.clear();
        for (const auto& item : allItems) {
            std::string name = item.name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            if (name.find(query) != std::string::npos) displayedItems.push_back(item);
        }
    }

    recycler->reloadData();

    if (allItems.empty()) {
        setStatus(emptyMessage);
    } else if (displayedItems.empty()) {
        setStatus("Nada encontrado.");
    } else {
        setStatus("");
    }
}

int LocalListContentView::ItemsDataSource::numberOfRows(brls::RecyclerFrame*, int) {
    return static_cast<int>(owner->displayedItems.size());
}

std::string LocalListContentView::ItemsDataSource::titleForHeader(brls::RecyclerFrame*, int) {
    return owner->listTitle;
}

float LocalListContentView::ItemsDataSource::heightForHeader(brls::RecyclerFrame*, int) {
    // Altura real pro cabeçalho da seção 0 — com o padrão da lib (altura 0)
    // o primeiro item da lista renderizava sem o texto (mesmo bug já
    // corrigido na lista de episódios de série).
    return 44;
}

brls::RecyclerCell* LocalListContentView::ItemsDataSource::cellForRow(brls::RecyclerFrame* recycler,
                                                                       brls::IndexPath index) {
    ListCell* cell = (ListCell*)recycler->dequeueReusableCell("Cell");
    cell->title->setText(owner->displayedItems[index.row].name);

    if (owner->onRemove) {
        // Captura o id (não o índice): depois de uma remoção a lista
        // recarrega e os índices mudam, mas a célula reciclada pode ainda
        // estar viva com a action antiga registrada. `registerAction` no
        // mesmo botão substitui a anterior, então reciclar não acumula.
        std::string itemId = owner->displayedItems[index.row].id;
        LocalListContentView* ownerPtr = owner;
        cell->registerAction(owner->removeHint, brls::BUTTON_X, [ownerPtr, itemId](brls::View*) {
            ownerPtr->handleRemove(itemId);
            return true;
        });
    }
    return cell;
}

void LocalListContentView::handleRemove(const std::string& id) {
    auto it = std::find_if(allItems.begin(), allItems.end(),
                            [&](const iptv::FavoriteItem& f) { return f.id == id; });
    if (it == allItems.end()) return;

    if (onRemove) onRemove(*it);
    allItems.erase(it);
    // Reaproveita o filtro atual: refaz displayedItems + reloadData + status
    // (inclusive a mensagem de lista vazia se esse era o último item).
    applySearchFilter();
}

void LocalListContentView::ItemsDataSource::didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) {
    size_t globalIndex = static_cast<size_t>(index.row);
    if (globalIndex >= owner->displayedItems.size()) return;
    if (!owner->onSelect) return;

    iptv::CatalogItem item = iptv::fromFavoriteItem(owner->displayedItems[globalIndex]);
    std::vector<iptv::CatalogItem> liveItems;
    for (const auto& f : owner->allItems) {
        if (f.kind == iptv::ContentKind::Live) liveItems.push_back(iptv::fromFavoriteItem(f));
    }
    owner->onSelect(item, liveItems);
}
