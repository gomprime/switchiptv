#pragma once

#include <borealis.hpp>

#include "data/storage.hpp"

// Fase 1 — tela de login (Xtream/M3U). Ver `data/auth_repository.hpp` pro
// fluxo de fallback auto -> Xtream -> M3U (porte de `AuthContext.tsx`).
class LoginActivity : public brls::Activity {
  public:
    // `prefill` pré-preenche os campos (ex.: ao deslogar, sem PIN pra
    // recuperar as credenciais depois, digitar tudo de novo do zero seria
    // bem chato) — sem credenciais salvas, os campos começam vazios.
    explicit LoginActivity(std::optional<iptv::StoredAuth> prefill = std::nullopt);

    CONTENT_FROM_XML_RES("activity/login.xml");

    void onContentAvailable() override;

  private:
    BRLS_BIND(brls::Label, errorLabel, "errorLabel");
    BRLS_BIND(brls::SelectorCell, formModeCell, "formMode");
    BRLS_BIND(brls::InputCell, serverUrlCell, "serverUrl");
    BRLS_BIND(brls::InputCell, usernameCell, "username");
    BRLS_BIND(brls::InputCell, passwordCell, "password");
    BRLS_BIND(brls::Button, loginButton, "loginButton");

    int formModeIndex = 0;
    std::string serverUrl;
    std::string username;
    std::string password;
    bool busy = false;

    void submitLogin();
    void showError(const std::string& message);
    void setBusy(bool nextBusy);
    void updateFieldsVisibility();
};
