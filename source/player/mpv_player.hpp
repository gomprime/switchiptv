#pragma once

#include <string>

struct mpv_handle;
struct mpv_render_context;

namespace iptv {

// Encapsula libmpv (handle + contexto de render OpenGL) — sem nenhuma
// dependência de nanovg/borealis aqui, só o motor de reprodução em si. Quem
// possui o FBO/textura (ver `ui/mpv_view.hpp`) chama `render()` passando o
// FBO já criado.
class MpvPlayer {
  public:
    MpvPlayer();
    ~MpvPlayer();

    MpvPlayer(const MpvPlayer&) = delete;
    MpvPlayer& operator=(const MpvPlayer&) = delete;

    bool init();
    // `startSeconds > 0` retoma de um ponto específico (porte da retomada
    // de progresso do `storage.ts`) — usa a opção `start=` do próprio mpv,
    // aplicada só a esse loadfile, em vez de carregar e dar seek depois.
    void loadUrl(const std::string& url, double startSeconds = 0);
    void setPaused(bool paused);
    bool isPaused() const;
    void stop();
    // Avança/volta em relação à posição atual (segundos negativos voltam).
    void seek(double deltaSeconds);

    // -1 quando ainda não disponível (arquivo não carregado o suficiente).
    double getPositionSeconds() const;
    double getDurationSeconds() const;

    // Deve ser chamado com um contexto OpenGL corrente (mesma thread da UI).
    void render(int fbo, int width, int height);

    // Processa a fila de eventos internos do mpv — chamar todo frame.
    void drainEvents();

    bool hasError() const { return !lastError.empty(); }
    std::string takeError();

  private:
    mpv_handle* handle = nullptr;
    mpv_render_context* renderContext = nullptr;
    bool paused = false;
    std::string lastError;
};

}  // namespace iptv
