#include "data/http_client.hpp"

#include <cstdio>

#include <curl/curl.h>

namespace iptv {

namespace {

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t writeToFileCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* file = static_cast<FILE*>(userdata);
    return fwrite(ptr, size, nmemb, file);
}

}  // namespace

HttpResponse httpGetWithTimeout(const std::string& url, long timeoutSeconds) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Falha ao iniciar libcurl";
        return response;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // A API do GitHub (usada pelo atualizador) responde 403 sem isso —
    // inofensivo pros outros usos (Xtream/M3U ignoram).
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IptvPlayerSwitch");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        response.statusCode = code;
        response.ok = code >= 200 && code < 300;
    }

    curl_easy_cleanup(curl);
    return response;
}

HttpResponse httpGet(const std::string& url) {
    return httpGetWithTimeout(url, 15L);
}

HttpResponse httpPostJson(const std::string& url, const std::string& jsonBody) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Falha ao iniciar libcurl";
        return response;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        response.statusCode = code;
        response.ok = code >= 200 && code < 300;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

namespace {
int xferInfoCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* callback = static_cast<std::function<void(double, double)>*>(clientp);
    if (callback && *callback && dltotal > 0) {
        (*callback)(static_cast<double>(dlnow), static_cast<double>(dltotal));
    }
    return 0;
}
}  // namespace

bool httpDownloadToFile(const std::string& url, const std::string& destPath, std::string& error,
                         std::function<void(double, double)> onProgress) {
    FILE* file = fopen(destPath.c_str(), "wb");
    if (!file) {
        error = "Não foi possível criar o arquivo temporário";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(file);
        error = "Falha ao iniciar libcurl";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // Bem maior que o timeout padrão — é um binário de dezenas de MB, não
    // uma resposta JSON/imagem pequena.
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IptvPlayerSwitch");
    if (onProgress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfoCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &onProgress);
    }

    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    fclose(file);

    if (res != CURLE_OK) {
        error = curl_easy_strerror(res);
        return false;
    }
    if (code < 200 || code >= 300) {
        error = "Servidor respondeu " + std::to_string(code);
        return false;
    }
    return true;
}

}  // namespace iptv
