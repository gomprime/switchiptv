#pragma once

#include <borealis.hpp>
#include <memory>
#include <vector>

#include "data/catalog_types.hpp"
#include "data/storage.hpp"
#include "data/xtream_client.hpp"

class MpvView;

namespace brls {
class Rectangle;
}

// Player — porte de `PlayerView.tsx`. Canal ao vivo abre pequeno com a
// programação completa embaixo (Cima/Baixo trocam de canal, A alterna tela
// cheia); filme/episódio vai direto pra tela cheia. Mesmo comportamento já
// portado 3x (Android, web, Roku).
class PlayerActivity : public brls::Activity {
  public:
    PlayerActivity(iptv::StoredAuth auth, iptv::CatalogItem item, std::vector<iptv::CatalogItem> liveChannels);
    ~PlayerActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    iptv::StoredAuth auth;
    iptv::CatalogItem item;
    std::vector<iptv::CatalogItem> liveChannels;
    bool isLive;
    bool fullscreen;
    std::vector<iptv::EpgListing> epg;

    // Ver `catalog_content_view.cpp` — mesmo motivo: Activity não tem o
    // mecanismo de deletionToken de View, então usamos um flag manual pra
    // não tocar `this` se o usuário sair da tela antes da EPG responder.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    brls::Box* rootBox = nullptr;
    brls::Box* headerBox = nullptr;
    brls::Label* titleLabel = nullptr;
    brls::Label* hintLabel = nullptr;
    brls::Label* timeLabel = nullptr;
    brls::Rectangle* progressBarFill = nullptr;
    float progressBarWidth = 0;
    brls::Box* videoBox = nullptr;
    MpvView* mpvView = nullptr;
    brls::Label* loadingLabel = nullptr;
    brls::Label* errorLabel = nullptr;
    bool playbackStarted = false;
    std::string lastPlaybackUrl;
    bool lastPlaybackWasResume = false;
    size_t resumeTimeoutToken = 0;
    brls::Box* epgBox = nullptr;
    brls::Label* epgStatusLabel = nullptr;
    brls::Box* epgListBox = nullptr;

    void promptResumeIfNeeded();
    void playCurrentItem(double startSeconds);
    void retryFromZeroAfterResumeFailure();
    void loadEpg();
    void populateEpgList();
    void changeChannel(int step);
    void toggleFullscreen();
    void togglePause();
    void seekBy(double deltaSeconds);
    void applyLayout();
    void handleBack();
    void updateTimeLabel(double position, double duration);
};
