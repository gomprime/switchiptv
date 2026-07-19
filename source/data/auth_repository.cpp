#include "data/auth_repository.hpp"

namespace iptv {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

}  // namespace

LoginOutcome performLogin(const LoginInput& input) {
    LoginOutcome outcome;

    Credentials credentials;
    credentials.serverUrl = trim(input.serverUrl);
    credentials.username = trim(input.username);
    credentials.password = trim(input.password);

    if (input.formMode == LoginFormMode::M3uDirect) {
        LoginResult result = verifyM3uUrl(credentials.serverUrl);
        if (!result.ok) {
            outcome.error = !result.error.empty() ? result.error : "Falha ao carregar playlist M3U";
            return outcome;
        }
        outcome.auth.mode = AuthMode::M3u;
        outcome.auth.credentials = credentials;
        outcome.auth.m3uUrl = credentials.serverUrl;
        saveAuth(outcome.auth);
        outcome.ok = true;
        return outcome;
    }

    if (input.formMode == LoginFormMode::Xtream || input.formMode == LoginFormMode::Auto) {
        LoginResult result = xtreamLogin(credentials);
        if (result.ok) {
            outcome.auth.mode = AuthMode::Xtream;
            outcome.auth.credentials = credentials;
            saveAuth(outcome.auth);
            outcome.ok = true;
            return outcome;
        }
        if (input.formMode == LoginFormMode::Xtream) {
            outcome.error = !result.error.empty() ? result.error : "Falha no login Xtream";
            return outcome;
        }
        // formMode == Auto: cai para a tentativa via M3U abaixo.
    }

    std::string m3uUrl = buildM3uUrl(credentials);
    LoginResult result = verifyM3uUrl(m3uUrl);
    if (!result.ok) {
        outcome.error = "Não foi possível autenticar: nem a API Xtream nem a playlist M3U responderam. Verifique URL, usuário e senha.";
        return outcome;
    }

    outcome.auth.mode = AuthMode::M3u;
    outcome.auth.credentials = credentials;
    outcome.auth.m3uUrl = m3uUrl;
    saveAuth(outcome.auth);
    outcome.ok = true;
    return outcome;
}

bool tryRestoreAuth(StoredAuth& outAuth) {
    auto stored = loadAuth();
    if (!stored) return false;
    outAuth = *stored;
    return true;
}

}  // namespace iptv
