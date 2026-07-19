#include "data/updater.hpp"

#include <cstdio>

#include <algorithm>
#include <sstream>
#include <vector>

#include "data/http_client.hpp"
#include "third_party/nlohmann/json.hpp"

namespace iptv {

// Marcador de atualização pendente: só o caminho (em texto puro) do .nro
// que deve ser substituído na próxima abertura. Mesma pasta usada por
// storage.cpp pros dados do app. Fora do namespace anônimo abaixo de
// propósito — precisa ser alcançável como `iptv::kPendingUpdateMarkerPath`
// pela ponte `extern "C"` no fim deste arquivo.
const char kPendingUpdateMarkerPath[] = "sdmc:/switch/iptv-player/pending_update.txt";

namespace {

std::string g_executablePath;

const char kLatestReleaseUrl[] = "https://api.github.com/repos/gomprime/switchiptv/releases/latest";

// "v1.0.3" -> {1, 0, 3}. Também aceita sem o 'v' — usado tanto pra tag do
// GitHub quanto pra APP_VERSION (definido no CMakeLists, sem 'v').
std::vector<int> parseVersion(const std::string& raw) {
    std::string s = raw;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s = s.substr(1);
    std::vector<int> parts;
    std::stringstream ss(s);
    std::string segment;
    while (std::getline(ss, segment, '.')) {
        try {
            parts.push_back(std::stoi(segment));
        } catch (...) {
            parts.push_back(0);
        }
    }
    return parts;
}

// >0 se `a` é mais nova que `b`, 0 se igual, <0 se mais antiga.
int compareVersions(const std::vector<int>& a, const std::vector<int>& b) {
    size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        int va = i < a.size() ? a[i] : 0;
        int vb = i < b.size() ? b[i] : 0;
        if (va != vb) return va - vb;
    }
    return 0;
}

}  // namespace

void setExecutablePath(const std::string& path) {
    g_executablePath = path;
}

UpdateCheckResult checkForUpdate() {
    UpdateCheckResult result;

    HttpResponse response = httpGetWithTimeout(kLatestReleaseUrl, 10L);
    if (!response.ok) {
        result.error = response.error.empty() ? "Falha ao consultar atualizações" : response.error;
        return result;
    }

    nlohmann::json data;
    try {
        data = nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::exception&) {
        result.error = "Resposta inválida do GitHub";
        return result;
    }

    std::string tagName = data.value("tag_name", "");
    if (tagName.empty()) {
        result.error = "Release sem versão";
        return result;
    }

    std::string downloadUrl;
    auto assetsField = data.find("assets");
    if (assetsField != data.end() && assetsField->is_array()) {
        for (const auto& asset : *assetsField) {
            std::string name = asset.value("name", "");
            if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".nro") == 0) {
                downloadUrl = asset.value("browser_download_url", "");
                break;
            }
        }
    }
    if (downloadUrl.empty()) {
        result.error = "Release sem arquivo .nro";
        return result;
    }

    result.ok = true;
    result.latestVersion = tagName;
    result.downloadUrl = downloadUrl;
    result.updateAvailable = compareVersions(parseVersion(tagName), parseVersion(APP_VERSION)) > 0;
    return result;
}

UpdateInstallResult downloadAndInstall(const std::string& downloadUrl,
                                        std::function<void(double, double)> onProgress) {
    UpdateInstallResult result;

    if (g_executablePath.empty()) {
        result.error = "Não foi possível localizar o arquivo instalado";
        return result;
    }

    std::string tempPath = g_executablePath + ".update";
    std::string downloadError;
    if (!httpDownloadToFile(downloadUrl, tempPath, downloadError, std::move(onProgress))) {
        remove(tempPath.c_str());
        result.error = downloadError.empty() ? "Falha ao baixar a atualização" : downloadError;
        return result;
    }

    // NÃO dá pra substituir o binário em execução agora: este app usa RomFS
    // embutido no próprio .nro (fontes/imagens da borealis), e o libnx
    // mantém um handle de arquivo aberto nesse .nro pra ler esses recursos
    // sob demanda durante toda a execução — o cartão SD (FAT) recusa
    // apagar/renomear um arquivo com handle aberto (confirmado no
    // aparelho: "Falha ao substituir o arquivo instalado"). Em vez disso,
    // só grava um marcador com o caminho a trocar — a troca de verdade
    // acontece em `userAppInit()` (patch em
    // library/borealis/library/lib/platforms/switch/switch_wrapper.c), que
    // roda ANTES do `romfsInit()` do próximo processo, quando o arquivo
    // ainda está livre.
    FILE* marker = fopen(kPendingUpdateMarkerPath, "wb");
    if (!marker) {
        remove(tempPath.c_str());
        result.error = "Não foi possível preparar a instalação";
        return result;
    }
    fwrite(g_executablePath.data(), 1, g_executablePath.size(), marker);
    fclose(marker);
    printf("[updater] marcador gravado: alvo=%s update=%s\n", g_executablePath.c_str(), tempPath.c_str());

    result.ok = true;
    return result;
}

}  // namespace iptv

// Chamado de `userAppInit()` (switch_wrapper.c, C puro) — precisa de
// linkagem C pra ser visível de lá sem incluir cabeçalho C++ nenhum. Prints
// aqui só aparecem no console do nxlink (build -DDEBUG) — ajudou a
// confirmar que o app estava sendo suspenso (botão HOME) em vez de
// realmente fechado, então esta função nunca chegava a rodar de novo.
extern "C" void iptv_apply_pending_update() {
    FILE* marker = fopen(iptv::kPendingUpdateMarkerPath, "rb");
    if (!marker) {
        printf("[updater] sem atualização pendente\n");
        return;
    }

    char targetPath[512] = {0};
    size_t n = fread(targetPath, 1, sizeof(targetPath) - 1, marker);
    fclose(marker);

    while (n > 0 && (targetPath[n - 1] == '\n' || targetPath[n - 1] == '\r')) {
        targetPath[--n] = '\0';
    }

    printf("[updater] atualização pendente encontrada, alvo=%s\n", targetPath);

    if (n > 0) {
        std::string target(targetPath);
        std::string updateFile = target + ".update";
        FILE* check = fopen(updateFile.c_str(), "rb");
        if (check) {
            fclose(check);
            int removeResult = remove(target.c_str());
            int renameResult = rename(updateFile.c_str(), target.c_str());
            printf("[updater] remove=%d rename=%d\n", removeResult, renameResult);
        } else {
            printf("[updater] ERRO: arquivo baixado não encontrado em %s\n", updateFile.c_str());
        }
    }

    remove(iptv::kPendingUpdateMarkerPath);
}
