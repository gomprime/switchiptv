#include <borealis.hpp>
#include <curl/curl.h>

#include "activity/catalog_activity.hpp"
#include "activity/login_activity.hpp"
#include "data/auth_repository.hpp"
#include "data/updater.hpp"

/**
 * Porta de IPTV Player pra homebrew de Nintendo Switch (borealis + mpv).
 * Fase 2: login + navegação/grade de conteúdo, com sessão salva localmente.
 */
int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) {
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        }
    }

    // Convenção do hbloader: argv[0] é o caminho absoluto do próprio .nro
    // em execução (ex.: "sdmc:/switch/switchiptv/switchiptv.nro") — é isso
    // que o atualizador (data/updater.cpp) sobrescreve ao instalar uma
    // versão nova. Em plataformas sem esse argumento (build desktop), fica
    // vazio e o botão de atualizar simplesmente reporta erro.
    if (argc > 0 && argv[0] != nullptr) {
        iptv::setExecutablePath(argv[0]);
    }

    // Precisa rodar na thread principal, antes de qualquer chamada de rede —
    // curl_easy_init() faz essa inicialização sozinho se ninguém chamar antes,
    // mas a primeira chamada de rede do app (login) roda em thread de fundo
    // via brls::async(), e curl_global_init() não é thread-safe.
    curl_global_init(CURL_GLOBAL_DEFAULT);

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("IPTV Player");
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(false);

    // Cores extraídas do próprio logo (resources/img/logo_switch.png): azul
    // e vermelho dos Joy-Con — troca o azul/verde-água genérico padrão da
    // borealis por algo com identidade de verdade. Só o tema escuro importa
    // (setThemeVariant acima sempre força DARK).
    {
        NVGcolor joyconBlue = nvgRGB(2, 177, 244);
        NVGcolor joyconRed = nvgRGB(251, 35, 36);
        brls::Theme& dark = brls::Theme::getDarkTheme();
        dark.addColor("brls/accent", joyconBlue);
        dark.addColor("brls/click_pulse", nvgRGBA(2, 177, 244, 38));
        dark.addColor("brls/highlight/color1", joyconBlue);
        dark.addColor("brls/highlight/color2", joyconRed);
        dark.addColor("brls/sidebar/active_item", joyconBlue);
        dark.addColor("brls/button/primary_enabled_background", joyconBlue);
        dark.addColor("brls/button/primary_enabled_text", nvgRGB(255, 255, 255));
        dark.addColor("brls/button/highlight_enabled_text", joyconBlue);
        dark.addColor("brls/button/highlight_disabled_text", joyconBlue);
        dark.addColor("brls/list/listItem_value_color", joyconBlue);
        dark.addColor("brls/slider/line_filled", joyconBlue);
    }

    iptv::StoredAuth restoredAuth;
    if (iptv::tryRestoreAuth(restoredAuth)) {
        brls::Application::pushActivity(new CatalogActivity(restoredAuth));
    } else {
        brls::Application::pushActivity(new LoginActivity());
    }

    while (brls::Application::mainLoop())
        ;

    curl_global_cleanup();

    return EXIT_SUCCESS;
}
