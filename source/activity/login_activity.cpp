#include "activity/login_activity.hpp"

#include "activity/catalog_activity.hpp"
#include "data/auth_repository.hpp"

namespace {
const std::vector<std::string> kFormModeOptions = {"Automático", "Xtream", "M3U direto"};
const std::string kSuggestedM3uUrl = "https://iptv-org.github.io/iptv/languages/por.m3u";
}

LoginActivity::LoginActivity(std::optional<iptv::StoredAuth> prefill) {
    if (!prefill) return;
    if (prefill->mode == iptv::AuthMode::M3u) {
        formModeIndex = 2;
        // No modo M3U o campo de URL do servidor é o próprio link da
        // playlist (ver `updateFieldsVisibility`).
        serverUrl = !prefill->m3uUrl.empty() ? prefill->m3uUrl : prefill->credentials.serverUrl;
        return;
    }
    formModeIndex = 1;
    serverUrl = prefill->credentials.serverUrl;
    username = prefill->credentials.username;
    password = prefill->credentials.password;
}

void LoginActivity::onContentAvailable() {
    formModeCell->init(
        "Modo de login", kFormModeOptions, formModeIndex, [](int) {},
        [this](int selected) {
            formModeIndex = selected;
            updateFieldsVisibility();
        });
    updateFieldsVisibility();

    serverUrlCell->init(
        "URL do servidor", serverUrl, [this](std::string text) { serverUrl = text; }, "http://exemplo.com:8080",
        "Endereço do servidor Xtream ou link direto da playlist M3U",
        // Padrão da InputCell é 32 — curto demais pra URL: servidores Xtream
        // com subdomínio longo já estouram isso, e links M3U diretos (com
        // token/query string) costumam ser bem mais longos ainda. 200 cabe
        // folgado no buffer de 256 bytes do teclado do Switch
        // (`swkbdShow(&config, buffer, 0x100)`).
        200);

    usernameCell->init(
        "Usuário", username, [this](std::string text) { username = text; }, "usuário");

    passwordCell->init(
        "Senha", password, [this](std::string text) { password = text; }, "senha");

    loginButton->registerClickAction([this](brls::View* view) {
        submitLogin();
        return true;
    });
}

void LoginActivity::updateFieldsVisibility() {
    // "M3U direto" só precisa do link (reaproveita o campo de URL do
    // servidor) — usuário/senha não se aplicam nesse modo e ficam ocultos.
    bool isM3uDirect = formModeIndex == 2;
    usernameCell->setVisibility(isM3uDirect ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    passwordCell->setVisibility(isM3uDirect ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    serverUrlCell->setHint(isM3uDirect ? "Link direto da playlist M3U (.m3u/.m3u8)"
                                        : "Endereço do servidor Xtream ou link direto da playlist M3U");

    // Sugestão pronta pra quem só quer testar rápido (lista de canais em
    // português do iptv-org) — só preenche se o campo ainda estiver vazio,
    // nunca sobrescreve o que o usuário já digitou.
    if (isM3uDirect && serverUrl.empty()) {
        serverUrl = kSuggestedM3uUrl;
        serverUrlCell->setValue(serverUrl);
    }
}

void LoginActivity::showError(const std::string& message) {
    errorLabel->setText(message);
    errorLabel->setVisibility(message.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void LoginActivity::setBusy(bool nextBusy) {
    busy = nextBusy;
    loginButton->setState(nextBusy ? brls::ButtonState::DISABLED : brls::ButtonState::ENABLED);
}

void LoginActivity::submitLogin() {
    if (busy) return;
    showError("");
    setBusy(true);

    iptv::LoginInput input;
    input.serverUrl = serverUrl;
    input.username = username;
    input.password = password;
    switch (formModeIndex) {
        case 1:
            input.formMode = iptv::LoginFormMode::Xtream;
            break;
        case 2:
            input.formMode = iptv::LoginFormMode::M3uDirect;
            break;
        default:
            input.formMode = iptv::LoginFormMode::Auto;
            break;
    }

    brls::async([this, input]() {
        iptv::LoginOutcome outcome = iptv::performLogin(input);
        brls::sync([this, outcome]() {
            setBusy(false);
            if (!outcome.ok) {
                showError(outcome.error);
                return;
            }
            brls::Application::clear();
            brls::Application::pushActivity(new CatalogActivity(outcome.auth));
        });
    });
}
