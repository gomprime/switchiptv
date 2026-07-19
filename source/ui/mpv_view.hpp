#pragma once

#include <borealis.hpp>
#include <functional>

#include "player/mpv_player.hpp"

// View da borealis que hospeda a saída de vídeo do mpv — renderiza cada
// frame numa FBO simples (só textura de cor, sem stencil/depth — não
// precisamos disso pra um blit de vídeo) e compõe essa textura como uma
// imagem normal do nanovg no meio do desenho da UI. Único lugar do app que
// mexe com GL bruto; o resto do player (`player_activity`) só enxerga isso
// como mais uma View.
class MpvView : public brls::View {
  public:
    MpvView();
    ~MpvView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    iptv::MpvPlayer* player() { return &mpvPlayer; }

    // Chamado a cada ~5s com posição/duração atuais — porte do throttle de
    // `updateProgress` em `PlayerView.tsx`. Não é responsabilidade desta
    // view salvar nada (ela não sabe o que é um "item"), só avisa.
    void setOnProgress(std::function<void(double position, double duration)> callback) {
        onProgress = std::move(callback);
    }

    // Chamado quando o mpv termina um arquivo com erro (ex.: servidor
    // recusou a conexão, limite de streams simultâneos do provedor) — sem
    // isso o erro ficava só em `MpvPlayer::lastError`, nunca lido por
    // ninguém, e a tela ficava preta/travada sem nenhuma explicação.
    void setOnError(std::function<void(const std::string& message)> callback) {
        onError = std::move(callback);
    }

    // Antes de `loadUrl` ser chamado (ex.: enquanto o diálogo de "continuar
    // de onde parou" ainda está na tela), `render()` do mpv já mexe em
    // estado bruto de OpenGL mesmo sem nada carregado — isso "suja" o
    // estado a tempo de atrapalhar o desenho de outra Activity por cima no
    // mesmo frame (o diálogo aparecia atrás do vídeo). Sem nada pra mostrar
    // mesmo, é mais seguro não tocar em GL nenhum até ter um vídeo de
    // verdade pra renderizar.
    void setReadyToRender(bool ready) { readyToRender = ready; }

  private:
    bool readyToRender = false;
    iptv::MpvPlayer mpvPlayer;
    unsigned int fbo = 0;
    unsigned int texture = 0;
    int nvgImage = 0;
    int fbWidth = 0;
    int fbHeight = 0;
    std::function<void(double, double)> onProgress;
    std::function<void(const std::string&)> onError;
    double lastProgressReportTime = 0;

    void ensureFramebuffer(int width, int height);
    void destroyFramebuffer();
};
