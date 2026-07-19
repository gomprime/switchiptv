#pragma once

#include <borealis.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "data/catalog_types.hpp"
#include "data/storage.hpp"

// Busca — tela dedicada (não mais um campo inline no cabeçalho da aba). Antes
// disso, a busca ficava embutida em `CatalogContentView`, e enquanto o
// resultado ainda estava carregando (sem nenhuma célula focável ainda), um
// D-pad sem querer conseguia escapar pro Sidebar do `TabFrame` e trocar de
// aba, perdendo o estado da busca. Como Activity separada empilhada, o D-pad
// não alcança mais nada fora dela até o usuário apertar B.
class SearchActivity : public brls::Activity {
  public:
    SearchActivity(iptv::StoredAuth auth, std::function<void(iptv::CatalogItem, std::vector<iptv::CatalogItem>)> onSelect);
    ~SearchActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    class ResultsDataSource : public brls::RecyclerDataSource {
      public:
        explicit ResultsDataSource(SearchActivity* owner) : owner(owner) {}
        int numberOfSections(brls::RecyclerFrame* recycler) override;
        int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
        brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
        std::string titleForHeader(brls::RecyclerFrame* recycler, int section) override;
        float heightForHeader(brls::RecyclerFrame* recycler, int section) override;
        void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath index) override;

      private:
        SearchActivity* owner;
    };

    iptv::StoredAuth auth;
    bool isM3u;
    std::function<void(iptv::CatalogItem, std::vector<iptv::CatalogItem>)> onSelect;

    // Ver `player_activity.hpp`/outras Activities — mesmo motivo: callbacks
    // adiados (async, IME) não podem tocar `this` depois que o usuário sair
    // da tela antes deles rodarem.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    brls::Label* statusLabel = nullptr;
    brls::RecyclerFrame* resultsRecycler = nullptr;

    std::string query;
    // Xtream: 3 seções fixas (Ao Vivo/Filmes/Séries). M3U: 1 seção só (a
    // playlist inteira, sem divisão por tipo de conteúdo).
    std::vector<std::string> sectionLabels;
    std::vector<std::vector<iptv::CatalogItem>> allByTab;
    std::vector<bool> allByTabLoaded;
    std::vector<std::vector<iptv::CatalogItem>> filteredByTab;

    void runSearch(const std::string& text);
    void loadThenFilter();
    void applyFilter();
};
