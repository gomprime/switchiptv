#pragma once

#include <borealis.hpp>
#include <functional>
#include <vector>

#include "data/catalog_repository.hpp"
#include "data/storage.hpp"

// Painel de uma aba de conteúdo: categorias numa barra lateral à esquerda +
// lista de itens à direita. Porte de `Sidebar.tsx` + `ContentGrid.tsx`. A
// busca em si mora em `SearchActivity` (tela separada, ver esse arquivo pro
// porquê) — esta view só ainda mantém o cache de "todos os itens da aba"
// usado pelo "Surpreenda-me".
class CatalogContentView : public brls::Box {
  public:
    // `tab` só é relevante quando `auth.mode == Xtream`; no modo M3U as
    // categorias vêm do parse único da playlist.
    CatalogContentView(iptv::StoredAuth auth, iptv::ContentTab tab, std::function<void(iptv::CatalogItem, std::vector<iptv::CatalogItem>)> onSelect);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    class CategoriesDataSource : public brls::RecyclerDataSource {
      public:
        explicit CategoriesDataSource(CatalogContentView* owner) : owner(owner) {}
        int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
        brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
        void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath index) override;

      private:
        CatalogContentView* owner;
    };

    class ItemsDataSource : public brls::RecyclerDataSource {
      public:
        explicit ItemsDataSource(CatalogContentView* owner) : owner(owner) {}
        int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
        brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
        void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath index) override;

      private:
        CatalogContentView* owner;
    };

    iptv::StoredAuth auth;
    iptv::ContentTab tab;
    bool isM3u;
    std::function<void(iptv::CatalogItem, std::vector<iptv::CatalogItem>)> onSelect;

    brls::Box* contentRow = nullptr;
    brls::RecyclerFrame* categoriesRecycler = nullptr;
    brls::RecyclerFrame* itemsRecycler = nullptr;
    brls::Label* categoriesStatusLabel = nullptr;
    brls::Label* itemsStatusLabel = nullptr;
    brls::Label* clockLabel = nullptr;
    std::string lastClockText;

    std::vector<iptv::Category> categories;
    std::vector<iptv::CatalogItem> items;
    std::vector<iptv::M3uCategory> m3uCache;

    // "Surpreenda-me" (só aba Filmes, modo Xtream) — sorteia entre TODOS os
    // filmes (não só a categoria selecionada). `kSectionCount`/`allByTab`
    // seguem o mesmo agrupamento Ao Vivo/Filmes/Séries que a busca global
    // usava; mantido aqui só pelo índice de Filmes.
    static constexpr int kSectionCount = 3;
    std::vector<iptv::CatalogItem> allByTab[kSectionCount];
    bool allByTabLoaded[kSectionCount] = {false, false, false};

    void loadCategories();
    void updateClock();
    void selectCategory(size_t index);
    void setItemsStatus(const std::string& text);

    void pickSurpriseVod();
    void applySurprise();
};
