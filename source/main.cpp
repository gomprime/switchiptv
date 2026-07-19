#include <borealis.hpp>
#include <curl/curl.h>

#include "activity/catalog_activity.hpp"
#include "activity/login_activity.hpp"
#include "data/auth_repository.hpp"

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
