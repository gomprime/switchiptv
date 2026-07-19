#include "activity/series_episodes_activity.hpp"

#include "activity/player_activity.hpp"
#include "data/storage.hpp"
#include "ui/list_cell.hpp"

SeriesEpisodesActivity::SeriesEpisodesActivity(iptv::StoredAuth auth, iptv::CatalogItem series)
    : auth(std::move(auth)), series(std::move(series)) {}

SeriesEpisodesActivity::~SeriesEpisodesActivity() {
    *alive = false;
}

brls::View* SeriesEpisodesActivity::createContentView() {
    brls::Box* rootBox = new brls::Box();
    rootBox->setAxis(brls::Axis::COLUMN);
    rootBox->setPadding(30, 60, 30, 60);

    // Título + legenda na mesma linha — economiza espaço vertical pra lista
    // de episódios (era uma linha inteira só pra legenda antes).
    brls::Box* titleRow = new brls::Box();
    titleRow->setAxis(brls::Axis::ROW);
    titleRow->setAlignItems(brls::AlignItems::FLEX_END);
    titleRow->setMarginBottom(16);

    brls::Label* titleLabel = new brls::Label();
    titleLabel->setFontSize(28);
    titleLabel->setText(series.name);
    titleRow->addView(titleLabel);

    brls::Label* hintLabel = new brls::Label();
    hintLabel->setFontSize(16);
    hintLabel->setText("A: assistir episódio   Y: favoritar série   B: voltar");
    hintLabel->setMarginLeft(20);
    titleRow->addView(hintLabel);

    rootBox->addView(titleRow);

    statusLabel = new brls::Label();
    statusLabel->setFontSize(18);
    statusLabel->setText("Carregando episódios...");
    rootBox->addView(statusLabel);

    episodesRecycler = new brls::RecyclerFrame();
    episodesRecycler->setGrow(1.0f);
    episodesRecycler->estimatedRowHeight = 50;
    episodesRecycler->registerCell("Cell", []() { return ListCell::create(); });
    episodesRecycler->setDataSource(new EpisodesDataSource(this));
    rootBox->addView(episodesRecycler);

    return rootBox;
}

void SeriesEpisodesActivity::onContentAvailable() {
    episodesRecycler->registerAction("Voltar", brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    episodesRecycler->registerAction("Favoritar série", brls::BUTTON_Y, [this](brls::View*) {
        iptv::toggleFavorite(iptv::toFavoriteItem(series));
        return true;
    });
    loadEpisodes();
}

void SeriesEpisodesActivity::loadEpisodes() {
    if (series.streamId == 0 || auth.mode != iptv::AuthMode::Xtream) {
        statusLabel->setText("Sem episódios disponíveis.");
        return;
    }

    auto aliveFlag = alive;
    iptv::Credentials creds = auth.credentials;
    long seriesId = series.streamId;
    brls::async([this, aliveFlag, creds, seriesId]() {
        iptv::SeriesInfoResult result = iptv::getSeriesInfo(creds, seriesId);
        brls::sync([this, aliveFlag, result]() {
            if (!*aliveFlag) return;

            if (!result.ok) {
                statusLabel->setText(result.error);
                return;
            }
            info = result.info;
            if (info.episodesBySeason.empty()) {
                statusLabel->setText("Nenhum episódio encontrado.");
                return;
            }
            statusLabel->setVisibility(brls::Visibility::GONE);
            episodesRecycler->reloadData();
            // Sem isso, o foco fica "preso" em nada: quando a Activity foi
            // criada a lista ainda não tinha nenhuma célula (dados só
            // chegam depois, via rede), então não havia o que focar ainda.
            brls::Application::giveFocus(episodesRecycler);
        });
    });
}

void SeriesEpisodesActivity::playEpisode(const iptv::Episode& ep) {
    iptv::CatalogItem item;
    item.id = "series-ep-" + ep.id;
    item.kind = iptv::ContentKind::Episode;
    item.name = series.name + " - " + ep.title;
    item.logo = series.logo;
    item.categoryId = series.categoryId;
    item.categoryName = series.categoryName;
    item.containerExtension = ep.containerExtension;
    try {
        item.streamId = std::stol(ep.id);
    } catch (...) {
        item.streamId = 0;
    }
    brls::Application::pushActivity(new PlayerActivity(auth, item, {}));
}

// EpisodesDataSource

int SeriesEpisodesActivity::EpisodesDataSource::numberOfSections(brls::RecyclerFrame*) {
    return static_cast<int>(owner->info.episodesBySeason.size());
}

int SeriesEpisodesActivity::EpisodesDataSource::numberOfRows(brls::RecyclerFrame*, int section) {
    if (section < 0 || static_cast<size_t>(section) >= owner->info.episodesBySeason.size()) return 0;
    return static_cast<int>(owner->info.episodesBySeason[section].second.size());
}

std::string SeriesEpisodesActivity::EpisodesDataSource::titleForHeader(brls::RecyclerFrame*, int section) {
    if (section < 0 || static_cast<size_t>(section) >= owner->info.episodesBySeason.size()) return "";
    return "Temporada " + owner->info.episodesBySeason[section].first;
}

float SeriesEpisodesActivity::EpisodesDataSource::heightForHeader(brls::RecyclerFrame*, int) {
    return 44;
}

brls::RecyclerCell* SeriesEpisodesActivity::EpisodesDataSource::cellForRow(brls::RecyclerFrame* recycler,
                                                                            brls::IndexPath index) {
    ListCell* cell = (ListCell*)recycler->dequeueReusableCell("Cell");
    const iptv::Episode& ep = owner->info.episodesBySeason[index.section].second[index.row];
    std::string title = std::to_string(ep.episodeNum) + ". " + ep.title;
    // Mesmo id que `playEpisode()` grava no histórico — se existe progresso
    // salvo, o episódio já foi assistido (ao menos iniciado) antes.
    if (iptv::getProgress("series-ep-" + ep.id).has_value()) {
        title += "  (Assistido)";
    }
    cell->title->setText(title);
    return cell;
}

void SeriesEpisodesActivity::EpisodesDataSource::didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) {
    const iptv::Episode& ep = owner->info.episodesBySeason[index.section].second[index.row];
    owner->playEpisode(ep);
}
