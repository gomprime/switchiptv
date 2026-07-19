#include "ui/poster_row_cell.hpp"

#include <mutex>
#include <unordered_map>

#include "data/http_client.hpp"
#include "data/storage.hpp"

namespace {
// 10px de padding na coluna (setado em CatalogContentView) + 4 colunas + 5px
// de margem de cada lado = 180px de imagem por pôster (a largura nunca foi
// o problema — era a altura fixa de 60px que vem de `RecyclerCell`).
constexpr float kImageWidth = 180;
constexpr float kImageHeight = 252;
constexpr float kSlotMargin = 5;
constexpr float kRowHeight = 270;

// Threads próprias (uma por imagem, ou até um pool fixo) derrubaram o
// homebrew loader inteiro — a pilha default de uma `std::thread` criada por
// nós no Switch é pequena demais pra aguentar a profundidade de chamada do
// curl/TLS, e estourava (stack overflow). `brls::async()` já é usado no app
// inteiro sem esse problema (pilha própria, testada), então voltamos a usar
// ele mesmo sendo uma fila única (mais devagar, mas seguro). O cache em
// memória por URL continua — essa parte nunca teve risco nenhum, e evita
// rebaixar a mesma imagem ao rolar a tela pra frente e voltar.
std::mutex imageCacheMutex;
std::unordered_map<std::string, std::string> imageBytesCache;
}  // namespace

PosterRowCell::PosterRowCell() {
    // `RecyclerCell` (classe base) já define uma altura fixa de 60px no
    // próprio construtor (pensada pra células de uma linha só) — sem
    // sobrescrever isso aqui, o conteúdo do pôster (bem mais alto) transborda
    // e "empilha" visualmente sobre a linha seguinte.
    this->setHeight(kRowHeight);
    this->setAxis(brls::Axis::ROW);
    this->setPadding(6, 0, 6, 0);

    for (int i = 0; i < kColumns; i++) {
        Slot& slot = slots[i];

        slot.box = new brls::Box();
        slot.box->setAxis(brls::Axis::COLUMN);
        slot.box->setGrow(1.0f);
        slot.box->setShrink(1.0f);
        slot.box->setAlignItems(brls::AlignItems::CENTER);
        slot.box->setFocusable(true);
        slot.box->setMarginLeft(kSlotMargin);
        slot.box->setMarginRight(kSlotMargin);

        // Tamanho fixo em pixels + `PositionType::ABSOLUTE` pra sobrepor a
        // imagem no rótulo de reserva — NUNCA `setWidthPercentage`/
        // `setHeightPercentage` aqui (causou uma recursão grave no Yoga
        // antes, ver histórico do projeto).
        slot.imageBox = new brls::Box();
        slot.imageBox->setWidth(kImageWidth);
        slot.imageBox->setHeight(kImageHeight);

        slot.fallbackLabel = new brls::Label();
        slot.fallbackLabel->setWidth(kImageWidth);
        slot.fallbackLabel->setHeight(kImageHeight);
        slot.fallbackLabel->setText("TV");
        slot.fallbackLabel->setFontSize(18);
        slot.fallbackLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        slot.fallbackLabel->setVerticalAlign(brls::VerticalAlign::CENTER);
        slot.imageBox->addView(slot.fallbackLabel);

        slot.image = new brls::Image();
        slot.image->setPositionType(brls::PositionType::ABSOLUTE);
        slot.image->setPositionTop(0);
        slot.image->setPositionLeft(0);
        slot.image->setWidth(kImageWidth);
        slot.image->setHeight(kImageHeight);
        slot.image->setScalingType(brls::ImageScalingType::FIT);
        slot.imageBox->addView(slot.image);

        // Nome sobreposto no topo do próprio pôster (fundo preto ~70% opaco,
        // texto branco) — em vez de ficar embaixo, onde um nome grande
        // quebrava linha e vazava pro pôster vizinho ou pra linha seguinte.
        // Sobreposto e absoluto, pode quebrar em mais de uma linha sem
        // atrapalhar mais nada — a caixa cresce com o conteúdo.
        slot.nameLabel = new brls::Label();
        slot.nameLabel->setPositionType(brls::PositionType::ABSOLUTE);
        slot.nameLabel->setPositionTop(0);
        slot.nameLabel->setPositionLeft(0);
        slot.nameLabel->setWidth(kImageWidth);
        slot.nameLabel->setFontSize(13);
        slot.nameLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        slot.nameLabel->setTextColor(nvgRGB(255, 255, 255));
        slot.nameLabel->setBackgroundColor(nvgRGBA(0, 0, 0, 178));
        slot.imageBox->addView(slot.nameLabel);

        // Estrela de favorito no canto superior direito — botão X (ver
        // registerAction em setItems).
        slot.favoriteLabel = new brls::Label();
        slot.favoriteLabel->setPositionType(brls::PositionType::ABSOLUTE);
        slot.favoriteLabel->setPositionTop(0);
        slot.favoriteLabel->setPositionRight(0);
        slot.favoriteLabel->setText("★");
        slot.favoriteLabel->setFontSize(20);
        slot.favoriteLabel->setTextColor(nvgRGB(255, 215, 0));
        slot.favoriteLabel->setVisibility(brls::Visibility::GONE);
        slot.imageBox->addView(slot.favoriteLabel);

        slot.box->addView(slot.imageBox);

        this->addView(slot.box);
    }
}

void PosterRowCell::setItems(const std::vector<iptv::CatalogItem>& itemsInRow,
                              std::function<void(int slot)> onSelect) {
    for (int i = 0; i < kColumns; i++) {
        Slot& slot = slots[i];

        if (i >= (int)itemsInRow.size()) {
            slot.box->setVisibility(brls::Visibility::INVISIBLE);
            continue;
        }
        slot.box->setVisibility(brls::Visibility::VISIBLE);

        const iptv::CatalogItem& item = itemsInRow[i];
        slot.nameLabel->setText(item.name);
        slot.image->clear();
        slot.favoriteLabel->setVisibility(iptv::isFavorite(item.id) ? brls::Visibility::VISIBLE
                                                                     : brls::Visibility::GONE);

        if (item.logo.empty()) {
            // Sem logo: só o rótulo de reserva, sem tentar carregar imagem.
            slot.fallbackLabel->setVisibility(brls::Visibility::VISIBLE);
        } else {
            // Esconde o rótulo de reserva assim que decidimos carregar uma
            // imagem de verdade — senão ele fica visível atrás da imagem
            // pra sempre e vaza nas bordas quando a proporção da logo não
            // preenche a caixa inteira (letterbox do `FIT`), parecendo uma
            // linha cortando o pôster.
            slot.fallbackLabel->setVisibility(brls::Visibility::GONE);
            std::string url = item.logo;
            slot.image->setImageAsync([url](std::function<void(const std::string&, size_t)> finish) {
                {
                    std::lock_guard<std::mutex> lock(imageCacheMutex);
                    auto it = imageBytesCache.find(url);
                    if (it != imageBytesCache.end()) {
                        finish(it->second, it->second.size());
                        return;
                    }
                }
                brls::async([url, finish]() {
                    iptv::HttpResponse response = iptv::httpGetWithTimeout(url, 6L);
                    if (response.ok) {
                        std::lock_guard<std::mutex> lock(imageCacheMutex);
                        imageBytesCache[url] = response.body;
                    }
                    finish(response.body, response.ok ? response.body.size() : 0);
                });
            });
        }

        slot.box->registerClickAction([onSelect, i](brls::View*) {
            if (onSelect) onSelect(i);
            return true;
        });

        iptv::CatalogItem itemCopy = item;
        brls::Label* favoriteLabel = slot.favoriteLabel;
        slot.box->registerAction("Favoritar", brls::BUTTON_X, [itemCopy, favoriteLabel](brls::View*) {
            iptv::toggleFavorite(iptv::toFavoriteItem(itemCopy));
            favoriteLabel->setVisibility(iptv::isFavorite(itemCopy.id) ? brls::Visibility::VISIBLE
                                                                        : brls::Visibility::GONE);
            return true;
        });
    }
}

void PosterRowCell::prepareForReuse() {
    for (int i = 0; i < kColumns; i++) {
        slots[i].image->clear();
        slots[i].fallbackLabel->setVisibility(brls::Visibility::VISIBLE);
        slots[i].favoriteLabel->setVisibility(brls::Visibility::GONE);
    }
}

brls::RecyclerCell* PosterRowCell::create() {
    return new PosterRowCell();
}
