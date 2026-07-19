#include "ui/mpv_view.hpp"

#include <chrono>
#include <glad/glad.h>
#include <nanovg.h>

// Só precisamos das declarações (nvglCreateImageFromHandleGL3 etc.) — a
// implementação do backend GL3 já está compilada em glfw_video.cpp (via
// NANOVG_GL3_IMPLEMENTATION). Definir só o `NANOVG_GL3` puro (sem o sufixo
// _IMPLEMENTATION) evita recompilar o backend inteiro de novo aqui.
#define NANOVG_GL3 1
#include <nanovg_gl.h>

namespace {

// A chamada de render do mpv mexe em estado bruto do OpenGL (programa
// ativo, texturas, blend, etc.) sem avisar o nanovg, que cacheia esse estado
// internamente pra evitar chamadas repetidas — sem salvar/restaurar isso,
// os desenhos seguintes do nanovg podem ficar errados mesmo com o mpv tendo
// renderizado certinho na textura.
struct GLStateGuard {
    GLint program = 0;
    GLint activeTexture = 0;
    GLint texture2d = 0;
    GLint framebuffer = 0;
    GLint arrayBuffer = 0;
    GLint vertexArray = 0;
    GLboolean blendEnabled = GL_FALSE;
    GLint blendSrcRgb = 0, blendDstRgb = 0, blendSrcAlpha = 0, blendDstAlpha = 0;
    GLboolean depthTestEnabled = GL_FALSE;
    GLboolean cullFaceEnabled = GL_FALSE;
    GLboolean scissorEnabled = GL_FALSE;
    GLint scissorBox[4] = {0, 0, 0, 0};
    GLint viewport[4] = {0, 0, 0, 0};

    GLStateGuard() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2d);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
        blendEnabled = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
        depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
        scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
        glGetIntegerv(GL_VIEWPORT, viewport);
    }

    ~GLStateGuard() {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glUseProgram(program);
        glActiveTexture(static_cast<GLenum>(activeTexture));
        glBindTexture(GL_TEXTURE_2D, texture2d);
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glBindVertexArray(vertexArray);
        blendEnabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha);
        depthTestEnabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        cullFaceEnabled ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
        scissorEnabled ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
        glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
};

}  // namespace

MpvView::MpvView() {
    mpvPlayer.init();
}

MpvView::~MpvView() {
    destroyFramebuffer();
}

void MpvView::destroyFramebuffer() {
    if (nvgImage != 0) {
        nvgDeleteImage(brls::Application::getNVGContext(), nvgImage);
        nvgImage = 0;
    }
    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    if (texture != 0) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
}

void MpvView::ensureFramebuffer(int width, int height) {
    if (fbo != 0 && fbWidth == width && fbHeight == height) return;
    destroyFramebuffer();

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        brls::Logger::error("MpvView: framebuffer incompleta (status 0x{:x})", static_cast<unsigned>(status));
        destroyFramebuffer();
        return;
    }

    nvgImage = nvglCreateImageFromHandleGL3(brls::Application::getNVGContext(), texture, width, height, 0);

    fbWidth = width;
    fbHeight = height;
}

void MpvView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                    brls::FrameContext* ctx) {
    if (!readyToRender) return;

    int w = static_cast<int>(width);
    int h = static_cast<int>(height);
    if (w <= 0 || h <= 0) return;

    ensureFramebuffer(w, h);
    if (fbo == 0 || nvgImage == 0) return;

    mpvPlayer.drainEvents();
    if (mpvPlayer.hasError() && onError) onError(mpvPlayer.takeError());
    {
        GLStateGuard guard;
        mpvPlayer.render(static_cast<int>(fbo), w, h);
    }

    nvgSave(vg);
    NVGpaint paint = nvgImagePattern(vg, x, y, width, height, 0, nvgImage, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
    nvgRestore(vg);

    if (onProgress) {
        double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        // 1s (era 5s) — a barra de tempo do player precisa de atualização
        // mais frequente pra parecer "viva".
        if (now - lastProgressReportTime >= 1.0) {
            lastProgressReportTime = now;
            double position = mpvPlayer.getPositionSeconds();
            double duration = mpvPlayer.getDurationSeconds();
            if (position >= 0 && duration > 0) onProgress(position, duration);
        }
    }
}
