#include "activity/movie_info_activity.hpp"

#include "activity/player_activity.hpp"
#include "data/http_client.hpp"
#include "data/storage.hpp"
#include "data/xtream_client.hpp"

namespace {
constexpr float kPosterWidth = 220;
constexpr float kPosterHeight = 308;
}  // namespace

MovieInfoActivity::MovieInfoActivity(iptv::StoredAuth auth, iptv::CatalogItem item)
    : auth(std::move(auth)), item(std::move(item)) {}

MovieInfoActivity::~MovieInfoActivity() {
    *alive = false;
}

brls::View* MovieInfoActivity::createContentView() {
    rootBox = new brls::Box();
    rootBox->setAxis(brls::Axis::ROW);
    rootBox->setFocusable(true);
    rootBox->setPadding(30, 60, 30, 60);

    // Pôster à esquerda — mesmo padrão seguro de `PosterRowCell`: rótulo de
    // reserva embaixo (escondido assim que uma imagem de verdade é tentada),
    // imagem sobreposta em `PositionType::ABSOLUTE`.
    brls::Box* posterBox = new brls::Box();
    posterBox->setWidth(kPosterWidth);
    posterBox->setHeight(kPosterHeight);
    posterBox->setMarginRight(30);

    posterFallback = new brls::Label();
    posterFallback->setWidth(kPosterWidth);
    posterFallback->setHeight(kPosterHeight);
    posterFallback->setText("TV");
    posterFallback->setFontSize(18);
    posterFallback->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    posterFallback->setVerticalAlign(brls::VerticalAlign::CENTER);
    posterBox->addView(posterFallback);

    posterImage = new brls::Image();
    posterImage->setPositionType(brls::PositionType::ABSOLUTE);
    posterImage->setPositionTop(0);
    posterImage->setPositionLeft(0);
    posterImage->setWidth(kPosterWidth);
    posterImage->setHeight(kPosterHeight);
    posterImage->setScalingType(brls::ImageScalingType::FIT);
    posterBox->addView(posterImage);

    rootBox->addView(posterBox);

    // Coluna da direita: título/dicas/detalhes (era o conteúdo inteiro
    // antes de existir o pôster).
    brls::Box* infoColumn = new brls::Box();
    infoColumn->setAxis(brls::Axis::COLUMN);
    infoColumn->setGrow(1.0f);

    brls::Label* titleLabel = new brls::Label();
    titleLabel->setFontSize(28);
    titleLabel->setText(item.name);
    infoColumn->addView(titleLabel);

    brls::Label* hintLabel = new brls::Label();
    hintLabel->setFontSize(16);
    hintLabel->setText("A: assistir   Y: favoritar   B: voltar");
    hintLabel->setMarginTop(4);
    hintLabel->setMarginBottom(20);
    infoColumn->addView(hintLabel);

    statusLabel = new brls::Label();
    statusLabel->setFontSize(18);
    statusLabel->setText("Carregando informações...");
    infoColumn->addView(statusLabel);

    detailsBox = new brls::Box();
    detailsBox->setAxis(brls::Axis::COLUMN);
    detailsBox->setMarginTop(12);
    infoColumn->addView(detailsBox);

    rootBox->addView(infoColumn);

    return rootBox;
}

void MovieInfoActivity::onContentAvailable() {
    rootBox->registerAction("Assistir", brls::BUTTON_A, [this](brls::View*) {
        play();
        return true;
    });
    rootBox->registerAction("Voltar", brls::BUTTON_B, [this](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    rootBox->registerAction("Favoritar", brls::BUTTON_Y, [this](brls::View*) {
        iptv::toggleFavorite(iptv::toFavoriteItem(item));
        return true;
    });

    loadInfo();
    loadPoster();
}

void MovieInfoActivity::loadPoster() {
    if (item.logo.empty()) return;

    posterFallback->setVisibility(brls::Visibility::GONE);
    std::string url = item.logo;
    posterImage->setImageAsync([url](std::function<void(const std::string&, size_t)> finish) {
        brls::async([url, finish]() {
            iptv::HttpResponse response = iptv::httpGetWithTimeout(url, 6L);
            finish(response.body, response.ok ? response.body.size() : 0);
        });
    });
}

void MovieInfoActivity::loadInfo() {
    if (item.streamId == 0 || auth.mode != iptv::AuthMode::Xtream) {
        statusLabel->setVisibility(brls::Visibility::GONE);
        return;
    }

    auto aliveFlag = alive;
    iptv::Credentials creds = auth.credentials;
    long vodId = item.streamId;
    brls::async([this, aliveFlag, creds, vodId]() {
        iptv::VodInfoResult result = iptv::getVodInfo(creds, vodId);
        brls::sync([this, aliveFlag, result]() {
            if (!*aliveFlag) return;

            if (!result.ok) {
                statusLabel->setText(result.error);
                return;
            }

            const iptv::VodInfo& info = result.info;
            bool hasAnything = !info.plot.empty() || !info.director.empty() || !info.cast.empty() ||
                                !info.genre.empty() || !info.rating.empty();
            if (!hasAnything) {
                statusLabel->setText("Sem informações adicionais disponíveis.");
                return;
            }
            statusLabel->setVisibility(brls::Visibility::GONE);

            if (!info.genre.empty()) {
                brls::Label* genreLabel = new brls::Label();
                genreLabel->setFontSize(16);
                genreLabel->setText(info.genre);
                genreLabel->setMarginBottom(4);
                detailsBox->addView(genreLabel);
            }
            if (!info.rating.empty()) {
                brls::Label* ratingLabel = new brls::Label();
                ratingLabel->setFontSize(16);
                ratingLabel->setText("Nota: " + info.rating);
                ratingLabel->setMarginBottom(12);
                detailsBox->addView(ratingLabel);
            }
            if (!info.plot.empty()) {
                brls::Label* plotLabel = new brls::Label();
                plotLabel->setFontSize(18);
                plotLabel->setWidth(brls::Application::contentWidth - 120 - kPosterWidth - 30);
                plotLabel->setIsWrapping(true);
                plotLabel->setText(info.plot);
                plotLabel->setMarginBottom(12);
                detailsBox->addView(plotLabel);
            }
            if (!info.director.empty()) {
                brls::Label* directorLabel = new brls::Label();
                directorLabel->setFontSize(16);
                directorLabel->setText("Direção: " + info.director);
                directorLabel->setMarginBottom(4);
                detailsBox->addView(directorLabel);
            }
            if (!info.cast.empty()) {
                brls::Label* castLabel = new brls::Label();
                castLabel->setFontSize(16);
                castLabel->setWidth(brls::Application::contentWidth - 120 - kPosterWidth - 30);
                castLabel->setIsWrapping(true);
                castLabel->setText("Elenco: " + info.cast);
                detailsBox->addView(castLabel);
            }
        });
    });
}

void MovieInfoActivity::play() {
    brls::Application::pushActivity(new PlayerActivity(auth, item, {}));
}
