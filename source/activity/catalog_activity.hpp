#pragma once

#include <borealis.hpp>
#include <functional>
#include <vector>

#include "data/catalog_types.hpp"
#include "data/storage.hpp"

// Tela principal pós-login — `brls::TabFrame` padrão (barra lateral própria
// com as abas: Ao Vivo/Filmes/Séries ou Categorias, no modo M3U, +
// Favoritos/Histórico) e o painel da aba atual ao lado.
class CatalogActivity : public brls::Activity {
  public:
    explicit CatalogActivity(iptv::StoredAuth auth);

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    iptv::StoredAuth auth;
    brls::TabFrame* tabFrame = nullptr;

    void logout();
    void triggerSearch();
    void showAbout();
    std::function<void(iptv::CatalogItem, std::vector<iptv::CatalogItem>)> buildOnSelect();
};
