#include "activity/player_activity.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>

#include "data/storage.hpp"
#include "data/stream_url.hpp"
#include "ui/mpv_view.hpp"

namespace {

std::string formatEpgTime(const std::string& datetime) {
    size_t spacePos = datetime.find(' ');
    if (spacePos != std::string::npos && datetime.size() >= spacePos + 6) {
        std::string timePart = datetime.substr(spacePos + 1, 5);
        if (timePart.size() == 5 && timePart[2] == ':') return timePart;
    }

    // Alguns provedores mandam timestamp Unix cru (só dígitos) em vez de
    // data formatada — mesmo bug corrigido no Android/web.
    bool allDigits = !datetime.empty() && std::all_of(datetime.begin(), datetime.end(), [](unsigned char c) { return std::isdigit(c); });
    if (allDigits) {
        time_t epochSeconds = static_cast<time_t>(std::stoll(datetime));
        struct tm tmResult{};
        localtime_r(&epochSeconds, &tmResult);
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", tmResult.tm_hour, tmResult.tm_min);
        return std::string(buf);
    }
    return datetime;
}

}  // namespace

PlayerActivity::PlayerActivity(iptv::StoredAuth auth, iptv::CatalogItem item,
                                std::vector<iptv::CatalogItem> liveChannels)
    : auth(std::move(auth)),
      item(std::move(item)),
      liveChannels(std::move(liveChannels)),
      isLive(this->item.kind == iptv::ContentKind::Live),
      fullscreen(!isLive) {}

PlayerActivity::~PlayerActivity() {
    *alive = false;
}

brls::View* PlayerActivity::createContentView() {
    rootBox = new brls::Box();
    rootBox->setAxis(brls::Axis::COLUMN);
    rootBox->setFocusable(true);

    headerBox = new brls::Box();
    headerBox->setAxis(brls::Axis::ROW);
    headerBox->setHeight(60);
    headerBox->setPadding(12, 20, 12, 20);

    titleLabel = new brls::Label();
    titleLabel->setFontSize(22);
    titleLabel->setGrow(1.0f);
    titleLabel->setText(item.name);
    headerBox->addView(titleLabel);

    hintLabel = new brls::Label();
    hintLabel->setFontSize(16);
    hintLabel->setText(isLive ? "Cima/Baixo: trocar canal   A: tela cheia   Y: favoritar   B: voltar"
                               : "A: pausar/tocar   Esquerda/Direita: -/+10s   Y: favoritar   B: voltar");
    headerBox->addView(hintLabel);

    rootBox->addView(headerBox);

    brls::Box* videoWrapperBox = new brls::Box();
    videoWrapperBox->setAxis(brls::Axis::ROW);
    videoWrapperBox->setJustifyContent(brls::JustifyContent::CENTER);
    videoWrapperBox->setWidth(brls::Application::contentWidth);

    videoBox = new brls::Box();
    mpvView = new MpvView();
    // videoBox é ROW (padrão do Box) — grow preenche a largura (eixo
    // principal); a altura (eixo cruzado) já estica sozinha por padrão. Sem
    // isso, mpvView fica com largura zero e `draw()` nunca roda de verdade.
    mpvView->setGrow(1.0f);
    mpvView->setOnProgress([this](double position, double duration) {
        if (!playbackStarted) {
            playbackStarted = true;
            loadingLabel->setVisibility(brls::Visibility::GONE);
            errorLabel->setVisibility(brls::Visibility::GONE);
            if (resumeTimeoutToken != 0) {
                brls::cancelDelay(resumeTimeoutToken);
                resumeTimeoutToken = 0;
            }
            // Reprodução de fato começou — mostra o OSD uma vez (some
            // sozinho em 4s se o usuário não mexer).
            showOsd();
        }
        updateTimeLabel(position, duration);
        // Ao vivo não tem "posição" que faça sentido retomar depois.
        if (item.kind == iptv::ContentKind::Live) return;
        iptv::updateProgress(item.id, position, duration);
    });
    mpvView->setOnError([this](const std::string& message) {
        // Alguns provedores não aceitam retomar (seek) direto na URL de VOD/
        // episódio — só percebemos isso porque agora o erro do mpv realmente
        // chega até aqui (antes ficava só em `MpvPlayer::lastError`, nunca
        // lido, e a tela ficava preta sem explicação nenhuma). Se o erro
        // aconteceu tentando retomar de onde parou, tenta de novo do zero
        // antes de desistir e mostrar o erro pro usuário.
        if (resumeTimeoutToken != 0) {
            brls::cancelDelay(resumeTimeoutToken);
            resumeTimeoutToken = 0;
        }
        if (lastPlaybackWasResume) {
            retryFromZeroAfterResumeFailure();
            return;
        }
        loadingLabel->setVisibility(brls::Visibility::GONE);
        errorLabel->setText("Erro na reprodução: " + message);
        errorLabel->setVisibility(brls::Visibility::VISIBLE);
    });
    videoBox->addView(mpvView);

    loadingLabel = new brls::Label();
    loadingLabel->setFontSize(20);
    loadingLabel->setText("Carregando...");
    loadingLabel->setPositionType(brls::PositionType::ABSOLUTE);
    loadingLabel->setPositionTop(0);
    loadingLabel->setPositionLeft(0);
    loadingLabel->setPositionRight(0);
    loadingLabel->setPositionBottom(0);
    loadingLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    videoBox->addView(loadingLabel);

    errorLabel = new brls::Label();
    errorLabel->setFontSize(18);
    errorLabel->setPositionType(brls::PositionType::ABSOLUTE);
    errorLabel->setPositionTop(0);
    errorLabel->setPositionLeft(20);
    errorLabel->setPositionRight(20);
    errorLabel->setPositionBottom(0);
    errorLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    errorLabel->setVisibility(brls::Visibility::GONE);
    videoBox->addView(errorLabel);

    if (!isLive) {
        // OSD sobreposto na parte de baixo do vídeo: tempo + barra de
        // progresso, estilo apps de streaming. Visibilidade controlada por
        // `showOsd()` (auto-esconde após 4s; volta com seek/pausa).
        osdBox = new brls::Box();
        osdBox->setAxis(brls::Axis::COLUMN);
        osdBox->setPositionType(brls::PositionType::ABSOLUTE);
        osdBox->setPositionLeft(20);
        osdBox->setPositionRight(20);
        osdBox->setPositionBottom(15);

        timeLabel = new brls::Label();
        timeLabel->setFontSize(16);
        timeLabel->setText("--:-- / --:--");
        timeLabel->setTextColor(nvgRGB(255, 255, 255));
        timeLabel->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        timeLabel->setMarginBottom(6);
        osdBox->addView(timeLabel);

        brls::Box* progressBarBox = new brls::Box();
        progressBarBox->setHeight(6);

        brls::Rectangle* progressBarBg = new brls::Rectangle(nvgRGBA(255, 255, 255, 60));
        progressBarBg->setGrow(1.0f);
        progressBarBg->setHeight(6);
        progressBarBg->setCornerRadius(3);
        progressBarBox->addView(progressBarBg);

        // Azul-turquesa da UI do próprio Switch (destaque neon).
        progressBarFill = new brls::Rectangle(nvgRGB(0, 195, 227));
        progressBarFill->setPositionType(brls::PositionType::ABSOLUTE);
        progressBarFill->setPositionTop(0);
        progressBarFill->setPositionLeft(0);
        progressBarFill->setHeight(6);
        progressBarFill->setWidth(0);
        progressBarFill->setCornerRadius(3);
        progressBarBox->addView(progressBarFill);
        // VOD é sempre tela cheia — a largura útil do OSD é a tela menos as
        // margens laterais (20 + 20).
        progressBarWidth = brls::Application::contentWidth - 40;

        osdBox->addView(progressBarBox);
        videoBox->addView(osdBox);
    }

    videoWrapperBox->addView(videoBox);

    rootBox->addView(videoWrapperBox);

    epgBox = new brls::Box();
    epgBox->setAxis(brls::Axis::COLUMN);
    epgBox->setPadding(12, 20, 20, 20);

    epgStatusLabel = new brls::Label();
    epgStatusLabel->setFontSize(18);
    epgStatusLabel->setText("Carregando programação...");
    epgBox->addView(epgStatusLabel);

    epgListBox = new brls::Box();
    epgListBox->setAxis(brls::Axis::COLUMN);
    epgBox->addView(epgListBox);

    rootBox->addView(epgBox);

    return rootBox;
}

void PlayerActivity::onContentAvailable() {
    applyLayout();

    rootBox->registerAction("Trocar canal", brls::BUTTON_UP, [this](brls::View*) {
        if (!isLive) return false;
        changeChannel(-1);
        return true;
    });
    rootBox->registerAction("Trocar canal", brls::BUTTON_DOWN, [this](brls::View*) {
        if (!isLive) return false;
        changeChannel(1);
        return true;
    });
    rootBox->registerAction("Tela cheia/Pausar", brls::BUTTON_A, [this](brls::View*) {
        // Ao vivo já começa janela pequena (A entra em tela cheia); filme/
        // episódio já começa em tela cheia (A não tinha função nenhuma até
        // agora), então reaproveita o botão pra pausar/tocar.
        if (isLive) {
            if (!fullscreen) toggleFullscreen();
        } else {
            togglePause();
        }
        return true;
    });
    rootBox->registerAction("Retroceder", brls::BUTTON_LEFT, [this](brls::View*) {
        if (isLive) return false;
        seekBy(-10);
        return true;
    });
    rootBox->registerAction("Avançar", brls::BUTTON_RIGHT, [this](brls::View*) {
        if (isLive) return false;
        seekBy(10);
        return true;
    });
    rootBox->registerAction("Voltar", brls::BUTTON_B, [this](brls::View*) {
        handleBack();
        return true;
    });
    rootBox->registerAction("Favoritar", brls::BUTTON_Y, [this](brls::View*) {
        iptv::toggleFavorite(iptv::toFavoriteItem(item));
        return true;
    });

    promptResumeIfNeeded();
    if (isLive) loadEpg();
}

void PlayerActivity::promptResumeIfNeeded() {
    // Ao vivo não tem "posição" que faça sentido retomar depois.
    if (isLive) {
        playCurrentItem(0);
        return;
    }

    auto progress = iptv::getProgress(item.id);
    // Só oferece continuar se não estiver "quase no fim" (95%) — igual
    // `PlayerView.tsx`, pra não perguntar bobagem bem no final por engano.
    if (!progress || progress->positionSeconds >= progress->durationSeconds * 0.95) {
        playCurrentItem(0);
        return;
    }

    double resumeSeconds = progress->positionSeconds;
    // O dialog não é filho da view da Activity (é um overlay à parte), então
    // sobrevive se o usuário sair da tela (B) antes de escolher um botão —
    // sem o `alive`, o clique chamaria `playCurrentItem` numa Activity já
    // destruída (mesmo motivo de todo outro callback adiado neste arquivo).
    auto aliveFlag = alive;
    // `dialog->open()` chama `Application::pushActivity` — se feito direto
    // daqui, ainda estamos dentro do `onContentAvailable()` desta própria
    // Activity, chamado ANTES dela mesma ser empilhada (ver
    // `Application::pushActivity`: só dá `push_back` na pilha depois de
    // rodar `onContentAvailable()`). Isso fazia o diálogo entrar na pilha
    // primeiro e o player ser empilhado por cima logo em seguida — abrindo
    // "atrás". Adiar um tick garante que o player já esteja na pilha antes.
    brls::delay(1, [this, aliveFlag, resumeSeconds]() {
        if (!*aliveFlag) return;
        brls::Dialog* dialog = new brls::Dialog("Você já assistiu parte disso. Deseja continuar de onde parou?");
        dialog->addButton("Continuar de onde parou", [this, aliveFlag, resumeSeconds]() {
            if (!*aliveFlag) return;
            playCurrentItem(resumeSeconds);
        });
        dialog->addButton("Do início", [this, aliveFlag]() {
            if (!*aliveFlag) return;
            playCurrentItem(0);
        });
        dialog->setCancelable(false);
        dialog->open();
    });
}

void PlayerActivity::playCurrentItem(double startSeconds) {
    playbackStarted = false;
    loadingLabel->setVisibility(brls::Visibility::VISIBLE);
    errorLabel->setVisibility(brls::Visibility::GONE);
    if (resumeTimeoutToken != 0) {
        brls::cancelDelay(resumeTimeoutToken);
        resumeTimeoutToken = 0;
    }

    std::string url = iptv::resolveStreamUrl(auth, item);

    lastPlaybackUrl = url;
    lastPlaybackWasResume = startSeconds > 0;
    mpvView->player()->loadUrl(url, startSeconds);
    mpvView->setReadyToRender(true);
    if (lastPlaybackWasResume) {
        // Alguns provedores travam tentando dar seek (retomar) direto numa
        // URL de VOD/episódio, sem nunca emitir erro nem EOF pro mpv — só
        // fica "carregando" pra sempre. Sem erro explícito, o `setOnError`
        // nunca dispara sozinho, então usamos um prazo: se não começar a
        // tocar de verdade em 8s, desiste da retomada e toca do zero.
        auto aliveFlag = alive;
        resumeTimeoutToken = brls::delay(8000, [this, aliveFlag]() {
            if (!*aliveFlag) return;
            resumeTimeoutToken = 0;
            if (playbackStarted) return;
            retryFromZeroAfterResumeFailure();
        });
    }
    titleLabel->setText(item.name);
    iptv::recordHistory(iptv::toFavoriteItem(item));
}

void PlayerActivity::retryFromZeroAfterResumeFailure() {
    lastPlaybackWasResume = false;
    playbackStarted = false;
    mpvView->player()->loadUrl(lastPlaybackUrl, 0);
    // As duas labels ocupam a mesma área (overlay ABSOLUTE cobrindo o
    // vídeo inteiro) — sem esconder o "Carregando..." os dois textos
    // ficavam desenhados um em cima do outro.
    loadingLabel->setVisibility(brls::Visibility::GONE);
    errorLabel->setText("Não foi possível continuar de onde parou. Iniciando do começo...");
    errorLabel->setVisibility(brls::Visibility::VISIBLE);
}

void PlayerActivity::loadEpg() {
    if (item.streamId == 0 || auth.mode != iptv::AuthMode::Xtream) {
        epgStatusLabel->setText("Sem informações de programação disponíveis.");
        return;
    }

    epgStatusLabel->setText("Carregando programação...");
    epgListBox->clearViews();

    auto aliveFlag = alive;
    iptv::Credentials creds = auth.credentials;
    long streamId = item.streamId;
    brls::async([this, aliveFlag, creds, streamId]() {
        iptv::EpgResult result = iptv::getShortEpg(creds, streamId, 10);
        brls::sync([this, aliveFlag, result]() {
            if (!*aliveFlag) return;
            if (!result.ok) {
                epgStatusLabel->setText("Sem informações de programação disponíveis.");
                return;
            }
            epg = result.listings;
            populateEpgList();
        });
    });
}

void PlayerActivity::populateEpgList() {
    epgListBox->clearViews();

    if (epg.empty()) {
        epgStatusLabel->setText("Sem informações de programação disponíveis.");
        return;
    }
    epgStatusLabel->setText("Programação");

    for (const auto& listing : epg) {
        brls::Box* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setPaddingTop(4);
        row->setPaddingBottom(4);

        brls::Label* timeLabel = new brls::Label();
        timeLabel->setFontSize(16);
        timeLabel->setWidth(120);
        timeLabel->setText(formatEpgTime(listing.start) + "-" + formatEpgTime(listing.end));
        row->addView(timeLabel);

        brls::Label* titleRowLabel = new brls::Label();
        titleRowLabel->setFontSize(16);
        titleRowLabel->setGrow(1.0f);
        titleRowLabel->setText(listing.title);
        row->addView(titleRowLabel);

        epgListBox->addView(row);
    }
}

void PlayerActivity::changeChannel(int step) {
    if (liveChannels.empty()) return;
    auto it = std::find_if(liveChannels.begin(), liveChannels.end(),
                            [this](const iptv::CatalogItem& c) { return c.id == item.id; });
    if (it == liveChannels.end()) return;

    size_t idx = static_cast<size_t>(it - liveChannels.begin());
    size_t count = liveChannels.size();
    size_t nextIdx = (idx + static_cast<size_t>(step + static_cast<int>(count))) % count;

    item = liveChannels[nextIdx];
    playCurrentItem(0);
    loadEpg();
}

void PlayerActivity::toggleFullscreen() {
    fullscreen = !fullscreen;
    applyLayout();
}

void PlayerActivity::applyLayout() {
    if (fullscreen) {
        headerBox->setVisibility(brls::Visibility::GONE);
        epgBox->setVisibility(brls::Visibility::GONE);
        videoBox->setWidth(brls::Application::contentWidth);
        videoBox->setHeight(brls::Application::contentHeight);
    } else {
        headerBox->setVisibility(brls::Visibility::VISIBLE);
        epgBox->setVisibility(isLive ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        videoBox->setWidth(960);
        videoBox->setHeight(540);
    }
}

void PlayerActivity::togglePause() {
    mpvView->player()->setPaused(!mpvView->player()->isPaused());
    showOsd();
}

void PlayerActivity::seekBy(double deltaSeconds) {
    mpvView->player()->seek(deltaSeconds);
    showOsd();
}

void PlayerActivity::showOsd() {
    if (!osdBox) return;
    osdBox->setVisibility(brls::Visibility::VISIBLE);

    if (osdHideToken != 0) {
        brls::cancelDelay(osdHideToken);
        osdHideToken = 0;
    }
    // Pausado, o OSD fica na tela — despausar chama showOsd() de novo e aí
    // sim o auto-esconder é agendado.
    if (mpvView->player()->isPaused()) return;

    auto aliveFlag = alive;
    osdHideToken = brls::delay(4000, [this, aliveFlag]() {
        if (!*aliveFlag) return;
        osdHideToken = 0;
        if (osdBox) osdBox->setVisibility(brls::Visibility::GONE);
    });
}

void PlayerActivity::updateTimeLabel(double position, double duration) {
    if (!timeLabel) return;
    auto formatTime = [](double seconds) {
        if (seconds < 0) seconds = 0;
        int totalSeconds = static_cast<int>(seconds);
        int mins = totalSeconds / 60;
        int secs = totalSeconds % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
        return std::string(buf);
    };
    timeLabel->setText(formatTime(position) + " / " + formatTime(duration));

    if (progressBarFill && duration > 0) {
        float ratio = static_cast<float>(std::min(1.0, std::max(0.0, position / duration)));
        progressBarFill->setWidth(progressBarWidth * ratio);
    }
}

void PlayerActivity::handleBack() {
    if (isLive && fullscreen) {
        fullscreen = false;
        applyLayout();
        return;
    }
    brls::Application::popActivity();
}
