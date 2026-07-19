#pragma once

#include <borealis.hpp>
#include <functional>
#include <vector>

#include "data/catalog_types.hpp"

// Uma linha da grade de pôsteres: `RecyclerFrame` só faz listas verticais de
// UMA coluna, então cada célula representa uma LINHA inteira com N pôsteres
// lado a lado (slots). Itens sem logo mostram um rótulo de reserva atrás da
// imagem (que só desenha algo depois que a textura carrega).
class PosterRowCell : public brls::RecyclerCell {
  public:
    static constexpr int kColumns = 4;

    PosterRowCell();
    static brls::RecyclerCell* create();

    void setItems(const std::vector<iptv::CatalogItem>& itemsInRow, std::function<void(int slot)> onSelect);
    void prepareForReuse() override;

  private:
    struct Slot {
        brls::Box* box = nullptr;
        brls::Box* imageBox = nullptr;
        brls::Label* fallbackLabel = nullptr;
        brls::Image* image = nullptr;
        brls::Label* nameLabel = nullptr;
        brls::Label* favoriteLabel = nullptr;
    };
    Slot slots[kColumns];
};
