#pragma once

#include <borealis.hpp>
#include <memory>

#include "data/catalog_types.hpp"
#include "data/storage.hpp"

// Porte de `MovieInfo.tsx` — detalhes do filme (enredo/elenco/direção) antes
// de tocar. Só existe no modo Xtream (M3U não tem esse endpoint, toca direto
// — ver `catalog_activity.cpp`).
class MovieInfoActivity : public brls::Activity {
  public:
    MovieInfoActivity(iptv::StoredAuth auth, iptv::CatalogItem item);
    ~MovieInfoActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    iptv::StoredAuth auth;
    iptv::CatalogItem item;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    brls::Box* rootBox = nullptr;
    brls::Label* statusLabel = nullptr;
    brls::Box* detailsBox = nullptr;
    brls::Image* posterImage = nullptr;
    brls::Label* posterFallback = nullptr;

    void loadInfo();
    void loadPoster();
    void play();
};
