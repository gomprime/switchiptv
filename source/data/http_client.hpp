#pragma once

#include <functional>
#include <string>

namespace iptv {

// Porta mínima do que `getText()`/fetch cru já faz em todos os outros
// clientes — GET/POST simples via libcurl, síncrono (quem chama decide se
// roda em brls::async() ou não; não force threading aqui, mistura mal com
// chamadas que já rodam em background por fora).
struct HttpResponse {
    bool ok = false;
    long statusCode = 0;
    std::string body;
    std::string error;
};

HttpResponse httpGet(const std::string& url);
HttpResponse httpPostJson(const std::string& url, const std::string& jsonBody);

// Mesma coisa que `httpGet`, mas com timeout configurável — usado pra
// carregar pôsteres/logos: tudo roda numa única thread compartilhada
// (`brls::async`), então uma logo lenta/quebrada com o timeout padrão de 15s
// travava essa thread (e, com ela, qualquer outra coisa da tela esperando
// pra rodar) por tempo longo demais só por causa de uma imagem decorativa.
HttpResponse httpGetWithTimeout(const std::string& url, long timeoutSeconds);

// Baixa uma URL direto pra um arquivo (streaming via callback do libcurl),
// em vez de acumular tudo num std::string na memória — usado pelo
// atualizador (data/updater.cpp) pra baixar o .nro da release, que é bem
// maior (dezenas de MB) que qualquer outra resposta que este app já busca.
// `onProgress` (opcional) é chamado periodicamente na MESMA thread que
// chamou esta função — quem passa o callback decide se precisa de
// `brls::sync` pra atualizar UI.
bool httpDownloadToFile(const std::string& url, const std::string& destPath, std::string& error,
                         std::function<void(double downloadedBytes, double totalBytes)> onProgress = nullptr);

}  // namespace iptv
