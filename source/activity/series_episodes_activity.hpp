#pragma once

#include <borealis.hpp>
#include <memory>

#include "data/catalog_types.hpp"
#include "data/storage.hpp"
#include "data/xtream_client.hpp"

// Porte de `SeriesEpisodes.tsx` — lista de episódios agrupados por
// temporada (uma seção da RecyclerFrame por temporada, com o cabeçalho
// automático dela).
class SeriesEpisodesActivity : public brls::Activity {
  public:
    SeriesEpisodesActivity(iptv::StoredAuth auth, iptv::CatalogItem series);
    ~SeriesEpisodesActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    class EpisodesDataSource : public brls::RecyclerDataSource {
      public:
        explicit EpisodesDataSource(SeriesEpisodesActivity* owner) : owner(owner) {}
        int numberOfSections(brls::RecyclerFrame* recycler) override;
        int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
        std::string titleForHeader(brls::RecyclerFrame* recycler, int section) override;
        float heightForHeader(brls::RecyclerFrame* recycler, int section) override;
        brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
        void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath index) override;

      private:
        SeriesEpisodesActivity* owner;
    };

    iptv::StoredAuth auth;
    iptv::CatalogItem series;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    iptv::SeriesInfo info;

    brls::Label* statusLabel = nullptr;
    brls::RecyclerFrame* episodesRecycler = nullptr;

    void loadEpisodes();
    void playEpisode(const iptv::Episode& ep);
};
