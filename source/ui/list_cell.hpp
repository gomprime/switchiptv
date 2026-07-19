#pragma once

#include <borealis.hpp>

// Célula genérica de uma linha (categoria ou item) — só um `Label`. Poster
// com logo real fica pra Fase 8 (polimento); ver notas no plano.
class ListCell : public brls::RecyclerCell {
  public:
    ListCell();

    BRLS_BIND(brls::Label, title, "title");

    static brls::RecyclerCell* create();
};
