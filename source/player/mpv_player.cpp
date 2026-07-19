#include "player/mpv_player.hpp"

#include <GLFW/glfw3.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace iptv {

namespace {

void* getProcAddress(void*, const char* name) {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

}  // namespace

MpvPlayer::MpvPlayer() = default;

MpvPlayer::~MpvPlayer() {
    if (renderContext) mpv_render_context_free(renderContext);
    if (handle) mpv_terminate_destroy(handle);
}

bool MpvPlayer::init() {
    handle = mpv_create();
    if (!handle) {
        lastError = "Falha ao criar instância do mpv";
        return false;
    }

    // Sem VO próprio — a Render API cuida disso (composto na FBO que a gente
    // fornece em render()).
    mpv_set_option_string(handle, "vo", "libmpv");
    // Desligado por enquanto: primeiro teste no hardware real mostrou áudio
    // tocando mas vídeo em preto — suspeita de que o decode por hardware não
    // está conseguindo escrever na textura OpenGL nesse caminho específico.
    // Se isso resolver, dá pra investigar reativar com mais configuração.
    mpv_set_option_string(handle, "hwdec", "no");
    // Sem interface OSD/terminal — a UI é toda nossa (borealis).
    mpv_set_option_string(handle, "osc", "no");
    mpv_set_option_string(handle, "terminal", "no");
    mpv_set_option_string(handle, "keep-open", "yes");

    if (mpv_initialize(handle) < 0) {
        lastError = "Falha ao inicializar o mpv";
        mpv_destroy(handle);
        handle = nullptr;
        return false;
    }

    mpv_opengl_init_params glInitParams{};
    glInitParams.get_proc_address = getProcAddress;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    if (mpv_render_context_create(&renderContext, handle, params) < 0) {
        lastError = "Falha ao criar contexto de render do mpv";
        mpv_terminate_destroy(handle);
        handle = nullptr;
        return false;
    }

    return true;
}

void MpvPlayer::loadUrl(const std::string& url, double startSeconds) {
    if (!handle) return;
    lastError.clear();
    paused = false;
    if (startSeconds > 0) {
        std::string options = "start=" + std::to_string(startSeconds);
        const char* cmd[] = {"loadfile", url.c_str(), "replace", options.c_str(), nullptr};
        mpv_command(handle, cmd);
    } else {
        const char* cmd[] = {"loadfile", url.c_str(), nullptr};
        mpv_command(handle, cmd);
    }
}

double MpvPlayer::getPositionSeconds() const {
    if (!handle) return -1;
    double value = -1;
    if (mpv_get_property(handle, "time-pos", MPV_FORMAT_DOUBLE, &value) < 0) return -1;
    return value;
}

double MpvPlayer::getDurationSeconds() const {
    if (!handle) return -1;
    double value = -1;
    if (mpv_get_property(handle, "duration", MPV_FORMAT_DOUBLE, &value) < 0) return -1;
    return value;
}

void MpvPlayer::setPaused(bool nextPaused) {
    if (!handle) return;
    paused = nextPaused;
    mpv_set_property_string(handle, "pause", paused ? "yes" : "no");
}

bool MpvPlayer::isPaused() const {
    return paused;
}

void MpvPlayer::stop() {
    if (!handle) return;
    mpv_command_string(handle, "stop");
}

void MpvPlayer::seek(double deltaSeconds) {
    if (!handle) return;
    std::string cmd = "seek " + std::to_string(deltaSeconds) + " relative";
    mpv_command_string(handle, cmd.c_str());
}

void MpvPlayer::render(int fbo, int width, int height) {
    if (!renderContext) return;

    mpv_opengl_fbo mpvFbo{};
    mpvFbo.fbo = fbo;
    mpvFbo.w = width;
    mpvFbo.h = height;

    // Vídeo apareceu de ponta cabeça com flipY=1 no teste real — invertido.
    int flipY = 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(renderContext, params);
}

void MpvPlayer::drainEvents() {
    if (!handle) return;
    while (true) {
        mpv_event* event = mpv_wait_event(handle, 0);
        if (!event || event->event_id == MPV_EVENT_NONE) break;

        if (event->event_id == MPV_EVENT_END_FILE) {
            auto* endFile = static_cast<mpv_event_end_file*>(event->data);
            if (endFile && endFile->reason == MPV_END_FILE_REASON_ERROR) {
                lastError = mpv_error_string(endFile->error);
            }
        }
    }
}

std::string MpvPlayer::takeError() {
    std::string error = lastError;
    lastError.clear();
    return error;
}

}  // namespace iptv
