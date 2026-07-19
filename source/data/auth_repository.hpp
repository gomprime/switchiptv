#pragma once

#include <string>

#include "data/storage.hpp"
#include "data/xtream_client.hpp"

namespace iptv {

// Porte do fluxo de `AuthContext.tsx::login()`. Síncrono/bloqueante de
// propósito — quem chama roda isso dentro de `brls::async()` (thread de
// background) e usa `brls::sync()` pra voltar pra thread principal com o
// resultado, evitando travar a UI enquanto espera resposta do servidor.
enum class LoginFormMode { Auto, Xtream, M3uDirect };

struct LoginInput {
    std::string serverUrl;
    std::string username;
    std::string password;
    LoginFormMode formMode = LoginFormMode::Auto;
};

struct LoginOutcome {
    bool ok = false;
    std::string error;
    StoredAuth auth;
};

// Tenta login conforme `formMode`: 'm3u-direct' só verifica a URL informada;
// 'xtream' exige sucesso na API Xtream; 'auto' tenta Xtream e cai pra M3U
// montada (get.php) se falhar. Em sucesso, já persiste via `saveAuth()`.
LoginOutcome performLogin(const LoginInput& input);

// Tenta recarregar uma sessão já salva (chamado na inicialização do app).
bool tryRestoreAuth(StoredAuth& outAuth);

}  // namespace iptv
