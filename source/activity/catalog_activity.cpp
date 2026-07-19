#include "activity/catalog_activity.hpp"

#include <algorithm>

#include "activity/login_activity.hpp"
#include "activity/movie_info_activity.hpp"
#include "activity/player_activity.hpp"
#include "activity/search_activity.hpp"
#include "activity/series_episodes_activity.hpp"
#include "data/catalog_repository.hpp"
#include "data/storage.hpp"
#include "ui/catalog_content_view.hpp"
#include "ui/local_list_content_view.hpp"

CatalogActivity::CatalogActivity(iptv::StoredAuth auth) : auth(std::move(auth)) {}

void CatalogActivity::logout() {
    // Cópia obrigatória: `Application::clear()` deleta TODAS as Activities
    // da pilha na hora — inclusive esta — então `this->auth` já não existe
    // mais na linha seguinte ao clear().
    iptv::StoredAuth authCopy = auth;
    iptv::clearAuth();
    brls::Application::clear();
    // Sem PIN nesta versão, redigitar tudo do zero seria bem chato — leva as
    // credenciais da sessão que acabou de sair já preenchidas no formulário.
    brls::Application::pushActivity(new LoginActivity(authCopy));
}

std::function<void(iptv::CatalogItem, std::vector<iptv::CatalogItem>)> CatalogActivity::buildOnSelect() {
    iptv::StoredAuth authCopy = auth;
    return [authCopy](iptv::CatalogItem item, std::vector<iptv::CatalogItem> items) {
        if (item.kind == iptv::ContentKind::Series) {
            brls::Application::pushActivity(new SeriesEpisodesActivity(authCopy, item));
            return;
        }

        // Filme no modo Xtream mostra ficha (enredo/elenco) antes de tocar,
        // igual `MovieInfo.tsx` — M3U não tem esse endpoint, toca direto.
        if (item.kind == iptv::ContentKind::Vod && authCopy.mode == iptv::AuthMode::Xtream) {
            brls::Application::pushActivity(new MovieInfoActivity(authCopy, item));
            return;
        }

        std::vector<iptv::CatalogItem> liveChannels;
        if (item.kind == iptv::ContentKind::Live) {
            std::copy_if(items.begin(), items.end(), std::back_inserter(liveChannels),
                          [](const iptv::CatalogItem& c) { return c.kind == iptv::ContentKind::Live; });
        }
        brls::Application::pushActivity(new PlayerActivity(authCopy, item, liveChannels));
    };
}

brls::View* CatalogActivity::createContentView() {
    iptv::StoredAuth authCopy = auth;
    auto onSelect = buildOnSelect();

    tabFrame = new brls::TabFrame();

    if (auth.mode == iptv::AuthMode::M3u) {
        tabFrame->addTab("Categorias", [authCopy, onSelect]() {
            return new CatalogContentView(authCopy, iptv::ContentTab::Live, onSelect);
        });
    } else {
        tabFrame->addTab("Ao Vivo", [authCopy, onSelect]() {
            return new CatalogContentView(authCopy, iptv::ContentTab::Live, onSelect);
        });
        tabFrame->addTab("Filmes", [authCopy, onSelect]() {
            return new CatalogContentView(authCopy, iptv::ContentTab::Vod, onSelect);
        });
        tabFrame->addTab("Séries", [authCopy, onSelect]() {
            return new CatalogContentView(authCopy, iptv::ContentTab::Series, onSelect);
        });
    }

    tabFrame->addTab("Favoritos", [onSelect]() {
        return new LocalListContentView(
            iptv::loadFavorites(), onSelect, "Seus itens favoritos aparecem aqui.",
            [](const iptv::FavoriteItem& item) { iptv::toggleFavorite(item); }, "Desfavoritar", "Favoritos");
    });
    tabFrame->addTab("Histórico", [onSelect]() {
        auto history = iptv::loadHistory();
        std::vector<iptv::FavoriteItem> asFavorites(history.begin(), history.end());
        return new LocalListContentView(
            asFavorites, onSelect, "O que você assistiu aparece aqui.",
            [](const iptv::FavoriteItem& item) { iptv::removeHistoryItem(item.id); }, "Remover do histórico",
            "Histórico");
    });

    tabFrame->setAboutAction("Sobre", [this]() { showAbout(); });
    tabFrame->setHintsText(
        "Atalhos:\n"
        "A - Selecionar\n"
        "B - Voltar\n"
        "X - FAV/DESFAV\n"
        "Y - Deslogar\n"
        "R - Buscar\n"
        "+ - Fechar App");

    return tabFrame;
}

void CatalogActivity::onContentAvailable() {
    getContentView()->registerAction("Sair", brls::BUTTON_Y, [this](brls::View*) {
        logout();
        return true;
    });
    getContentView()->registerAction("Buscar", brls::BUTTON_RB, [this](brls::View*) {
        triggerSearch();
        return true;
    });
    getContentView()->registerAction("Fechar App", brls::BUTTON_START, [](brls::View*) {
        brls::Application::quit();
        return true;
    });
}

void CatalogActivity::triggerSearch() {
    if (!tabFrame) return;
    brls::View* active = tabFrame->getActiveTab();

    // Aba de catálogo (Ao Vivo/Filmes/Séries/Categorias): busca numa Activity
    // separada, isolada do resto da navegação (ver `search_activity.hpp`
    // pro porquê). Favoritos/Histórico continuam com o filtro local simples
    // — não tem espera de rede, então não sofre do mesmo problema.
    if (dynamic_cast<CatalogContentView*>(active)) {
        brls::Application::pushActivity(new SearchActivity(auth, buildOnSelect()));
        return;
    }
    if (auto* localView = dynamic_cast<LocalListContentView*>(active)) {
        localView->openSearch();
    }
}

void CatalogActivity::showAbout() {
    brls::Dialog* dialog = new brls::Dialog(
        "IPTV Player\n\n"
        "Desenvolvido por GomGeek com apoio do Claude (Anthropic).\n\n"
        "Agradecimentos: Aurelioeb e CostelaBR.");
    dialog->addButton("Fechar", [] {});
    dialog->open();
}
