#include "data/updater.hpp"

#include <cstdio>

#include <algorithm>
#include <sstream>
#include <vector>

#include "data/http_client.hpp"
#include "third_party/nlohmann/json.hpp"

namespace iptv {

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

UpdateInstallResult downloadAndInstall(const std::string& downloadUrl) {
    UpdateInstallResult result;

    if (g_executablePath.empty()) {
        result.error = "Não foi possível localizar o arquivo instalado";
        return result;
    }

    std::string tempPath = g_executablePath + ".update";
    std::string downloadError;
    if (!httpDownloadToFile(downloadUrl, tempPath, downloadError)) {
        remove(tempPath.c_str());
        result.error = downloadError.empty() ? "Falha ao baixar a atualização" : downloadError;
        return result;
    }

    // Sobrescreve o binário em execução — seguro no Switch (mesmo padrão de
    // vários outros homebrews auto-atualizáveis): o hbloader já carregou o
    // .nro inteiro na RAM antes de começar a rodar o processo, então trocar
    // o arquivo no cartão SD não afeta a execução atual. `remove` antes do
    // `rename` porque nem toda implementação de FS do devkitPro sobrescreve
    // o destino sozinha (ao contrário do POSIX de verdade).
    remove(g_executablePath.c_str());
    if (rename(tempPath.c_str(), g_executablePath.c_str()) != 0) {
        result.error = "Falha ao substituir o arquivo instalado";
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace iptv
