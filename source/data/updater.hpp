#pragma once

#include <string>

namespace iptv {

// Chamado uma vez em main(), com argv[0] — no Switch, o hbloader passa aí o
// caminho absoluto do próprio .nro em execução (ex.:
// "sdmc:/switch/switchiptv/switchiptv.nro"). É o alvo que o instalador
// sobrescreve — sem isso setado, downloadAndInstall() falha (não dá pra
// supor um caminho fixo: o usuário pode ter instalado em qualquer pasta
// dentro de /switch/).
void setExecutablePath(const std::string& path);

struct UpdateCheckResult {
    bool ok = false;
    bool updateAvailable = false;
    std::string latestVersion;  // "v1.0.3", como veio da tag do GitHub
    std::string downloadUrl;    // asset .nro da release
    std::string error;
};

// Consulta a última release pública em github.com/gomprime/switchiptv e
// compara a tag com APP_VERSION (definido no CMakeLists). Síncrono — chamar
// de dentro de brls::async(), igual todo outro acesso de rede do app.
UpdateCheckResult checkForUpdate();

struct UpdateInstallResult {
    bool ok = false;
    std::string error;
};

// Baixa o .nro da release pra um arquivo temporário e só então substitui o
// binário em execução (rename, nunca sobrescrita direta) — assim uma queda
// de rede no meio do download nunca deixa a instalação atual corrompida.
// Síncrono — chamar de dentro de brls::async().
UpdateInstallResult downloadAndInstall(const std::string& downloadUrl);

}  // namespace iptv
