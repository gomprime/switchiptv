#pragma once

#include <borealis.hpp>
#include <functional>
#include <vector>

#include "data/catalog_types.hpp"
#include "data/storage.hpp"

// Lista simples (sem categorias, sem rede) pras abas locais Favoritos e
// Histórico — porte da parte "sem categoria" de `Sidebar.tsx`/`ContentGrid.tsx`.
// Diferente de `CatalogContentView`, os dados já vêm prontos (do
// armazenamento local), sem fetch nenhum — a busca aqui só filtra em memória.
class LocalListContentView : public brls::Box {
  public:
    // `onRemove` (opcional) habilita remover um item com X: o callback deve
    // persistir a remoção (ex.: `toggleFavorite`); a view atualiza a lista
    // em memória sozinha. `removeHint` é o texto no rodapé de atalhos.
    // `listTitle` vira o cabeçalho da seção — precisa ser não-vazio: com o
    // cabeçalho da seção 0 colapsado (altura 0, padrão da lib), o primeiro
    // item da lista renderiza sem o texto (mesmo bug já visto e corrigido
    // na lista de episódios de série, dando altura real ao cabeçalho).
    LocalListContentView(std::vector<iptv::FavoriteItem> items,
                          std::function<void(iptv::CatalogItem, std::vector<iptv::CatalogItem>)> onSelect,
                          std::string emptyMessage,
                          std::function<void(const iptv::FavoriteItem&)> onRemove = nullptr,
                          std::string removeHint = "Remover",
                          std::string listTitle = "Itens");

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    // Abre o teclado pra digitar o termo de busca — acionado pelo botão R
    // (`CatalogActivity` registra essa ação globalmente pra aba atual).
    void openSearch();

  private:
    class ItemsDataSource : public brls::RecyclerDataSource {
      public:
        explicit ItemsDataSource(LocalListContentView* owner) : owner(owner) {}
        int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
        brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
        brls::RecyclerCell* cellForHeader(brls::RecyclerFrame* recycler, int section) override;
        std::string titleForHeader(brls::RecyclerFrame* recycler, int section) override;
        float heightForHeader(brls::RecyclerFrame* recycler, int section) override;
        void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath index) override;

      private:
        LocalListContentView* owner;
    };

    std::vector<iptv::FavoriteItem> allItems;
    std::vector<iptv::FavoriteItem> displayedItems;
    std::function<void(iptv::CatalogItem, std::vector<iptv::CatalogItem>)> onSelect;
    std::string emptyMessage;
    std::function<void(const iptv::FavoriteItem&)> onRemove;
    std::string removeHint;
    std::string listTitle;
    std::string searchQuery;

    brls::RecyclerFrame* recycler = nullptr;
    brls::Label* statusLabel = nullptr;
    brls::Label* clockLabel = nullptr;
    std::string lastClockText;
    bool initialReloadDone = false;

    void updateClock();
    void onSearchChanged(const std::string& query);
    void applySearchFilter();
    void setStatus(const std::string& text);
    void handleRemove(const std::string& id);
};
