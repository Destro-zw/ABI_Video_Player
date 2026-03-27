// OpenGL video renderer implementation using GLAD2.

#include "ABI_Video_OpenGL.h"

#include <glad/gl.h>
#include <glad/wgl.h>

#include <stdio.h>
#include <string.h>

static GLADapiproc glad_wgl_get_proc(const char* name)
{
    return (GLADapiproc)wglGetProcAddress(name);
}

static const char* kVertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* kFragmentShaderSrc = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D uTexture;
void main() {
    FragColor = texture(uTexture, vTexCoord);
}
)";

static GLuint CompileShader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        char msg[1280];
        snprintf(msg, sizeof(msg), "OpenGL shader compile error: %s\n", log);
        OutputDebugStringA(msg);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

OpenGLVideoRenderer::OpenGLVideoRenderer()
    : m_hwnd(nullptr), m_hdc(nullptr), m_hglrc(nullptr),
      m_texture(0), m_vao(0), m_vbo(0), m_ebo(0), m_shaderProgram(0),
      m_surfaceValid(false), m_ownsDevice(false), m_gladLoaded(false),
      m_uTextureLoc(-1)
{
}

OpenGLVideoRenderer::~OpenGLVideoRenderer()
{
    Cleanup();
}


bool OpenGLVideoRenderer::LoadGLAD()
{
    if (m_gladLoaded)
        return true;

    int version = gladLoadGL(glad_wgl_get_proc);
    if (version == 0) {
        OutputDebugStringA("Failed to load OpenGL functions via GLAD\n");
        return false;
    }

    m_gladLoaded = true;
    return true;
}

bool OpenGLVideoRenderer::CreateShaderProgram()
{
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
    if (!vs) return false;

    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    m_shaderProgram = glCreateProgram();
    glAttachShader(m_shaderProgram, vs);
    glAttachShader(m_shaderProgram, fs);
    glLinkProgram(m_shaderProgram);

    GLint success = 0;
    glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(m_shaderProgram, sizeof(log), nullptr, log);
        char msg[1280];
        snprintf(msg, sizeof(msg), "OpenGL program link error: %s\n", log);
        OutputDebugStringA(msg);
        glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!m_shaderProgram) {
        OutputDebugStringA("Failed to create shader program\n");
        return false;
    }

    m_uTextureLoc = glGetUniformLocation(m_shaderProgram, "uTexture");
    if (m_uTextureLoc < 0) {
        OutputDebugStringA("Warning: uTexture uniform not found (may be optimized out)\n");
    }

    return true;
}

bool OpenGLVideoRenderer::InitGLContext(HWND hwnd, int width, int height)
{
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    m_hdc = GetDC(hwnd);
    if (!m_hdc) {
        OutputDebugStringA("Failed to get window DC for OpenGL\n");
        return false;
    }

    int pixelFormat = ChoosePixelFormat(m_hdc, &pfd);
    if (!pixelFormat) {
        OutputDebugStringA("Failed to choose pixel format for OpenGL\n");
        ReleaseDC(hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    if (!SetPixelFormat(m_hdc, pixelFormat, &pfd)) {
        OutputDebugStringA("Failed to set pixel format for OpenGL\n");
        ReleaseDC(hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    HGLRC tempCtx = wglCreateContext(m_hdc);
    if (!tempCtx) {
        OutputDebugStringA("Failed to create temporary OpenGL context\n");
        ReleaseDC(hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    wglMakeCurrent(m_hdc, tempCtx);

    if (gladLoadWGL(m_hdc, glad_wgl_get_proc) == 0) {
        OutputDebugStringA("Failed to load WGL extensions via GLAD\n");
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tempCtx);
        ReleaseDC(hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    // Create a 4.3 Core profile context (falls back to 3.2, then legacy)
    if (GLAD_WGL_ARB_create_context) {
        int attribs43[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };
        m_hglrc = wglCreateContextAttribsARB(m_hdc, nullptr, attribs43);

        if (!m_hglrc) {
            // Fall back to 3.2 Core
            int attribs32[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
                WGL_CONTEXT_MINOR_VERSION_ARB, 2,
                WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0
            };
            m_hglrc = wglCreateContextAttribsARB(m_hdc, nullptr, attribs32);
        }
    }

    if (!m_hglrc) {
        OutputDebugStringA("Failed to create OpenGL core context; falling back to legacy\n");
        m_hglrc = wglCreateContext(m_hdc);
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempCtx);

    if (!m_hglrc) {
        OutputDebugStringA("Failed to create OpenGL context\n");
        ReleaseDC(hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    if (!wglMakeCurrent(m_hdc, m_hglrc)) {
        OutputDebugStringA("Failed to make OpenGL context current\n");
        wglDeleteContext(m_hglrc);
        m_hglrc = nullptr;
        ReleaseDC(hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    return true;
}

bool OpenGLVideoRenderer::Init(HWND hwnd, int width, int height, bool ownsDevice, void* externalDevice)
{
    m_hwnd = hwnd;
    m_ownsDevice = ownsDevice;

    if (ownsDevice) {
        if (!InitGLContext(hwnd, width, height))
            return false;
    } else {
        m_hglrc = (HGLRC)externalDevice;
        m_hdc = GetDC(hwnd);
        if (!m_hdc || !m_hglrc) {
            OutputDebugStringA("Invalid external OpenGL context or DC\n");
            return false;
        }
        if (!wglMakeCurrent(m_hdc, m_hglrc)) {
            OutputDebugStringA("Failed to make external OpenGL context current\n");
            ReleaseDC(hwnd, m_hdc);
            m_hdc = nullptr;
            return false;
        }
    }

    if (!LoadGLAD()) {
        Cleanup();
        return false;
    }

    if (!CreateShaderProgram()) {
        Cleanup();
        return false;
    }

    float vertices[] = {
        // pos          // texCoord
        -1.0f, -1.0f,   0.0f, 0.0f,
         1.0f, -1.0f,   1.0f, 0.0f,
         1.0f,  1.0f,   1.0f, 1.0f,
        -1.0f,  1.0f,   0.0f, 1.0f,
    };
    unsigned int indices[] = {
        0, 1, 2,
        2, 3, 0,
    };

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    wglMakeCurrent(nullptr, nullptr);

    return true;
}

void OpenGLVideoRenderer::Cleanup()
{
    if (m_hdc && m_hglrc) {
        wglMakeCurrent(m_hdc, m_hglrc);
    }

    DestroyRenderTarget();

    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_shaderProgram) { glDeleteProgram(m_shaderProgram); m_shaderProgram = 0; }

    if (m_hglrc) {
        wglMakeCurrent(nullptr, nullptr);
        if (m_ownsDevice) {
            wglDeleteContext(m_hglrc);
        }
        m_hglrc = nullptr;
    }
    if (m_hdc && m_hwnd) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
    }
    m_hwnd = nullptr;
    m_gladLoaded = false;
    m_uTextureLoc = -1;
}

bool OpenGLVideoRenderer::CreateRenderTarget(int width, int height)
{
    if (!m_gladLoaded)
        return false;

    if (!wglMakeCurrent(m_hdc, m_hglrc)) {
        OutputDebugStringA("CreateRenderTarget: Failed to make OpenGL context current\n");
        return false;
    }

    DestroyRenderTarget();

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_surfaceValid = true;

    wglMakeCurrent(nullptr, nullptr);

    return true;
}

void OpenGLVideoRenderer::DestroyRenderTarget()
{
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    m_surfaceValid = false;
}

void* OpenGLVideoRenderer::GetDevice()
{
    return (void*)m_hglrc;
}

bool OpenGLVideoRenderer::IsSurfaceValid() const
{
    return m_surfaceValid;
}

void OpenGLVideoRenderer::SetSurfaceValid(bool valid)
{
    m_surfaceValid = valid;
}

void OpenGLVideoRenderer::RenderFrame(VideoContext* ctx, bool canResetDevice)
{
    if (!m_hglrc || !m_gladLoaded)
        return;

    if (!wglMakeCurrent(m_hdc, m_hglrc)) {
        OutputDebugStringA("RenderFrame: Failed to make OpenGL context current\n");
        return;
    }

    if (!m_surfaceValid || !m_texture) {
        if (m_texture) { glDeleteTextures(1, &m_texture); m_texture = 0; }
        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ctx->width, ctx->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_surfaceValid = true;
    }


    RECT clientRect;
    GetClientRect(ctx->hwnd, &clientRect);
    int windowW = clientRect.right - clientRect.left;
    int windowH = clientRect.bottom - clientRect.top;
    if (windowW < 1) windowW = 1;
    if (windowH < 1) windowH = 1;

    glViewport(0, 0, windowW, windowH);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ctx->width, ctx->height,
                    GL_RGBA, GL_UNSIGNED_BYTE, ctx->pixelBuffer);

    float left = -1.0f, right = 1.0f, bottom = -1.0f, top = 1.0f;

    if (ctx->aspectW > 0 && ctx->aspectH > 0 && windowW > 0 && windowH > 0) {
        float targetAspect = (float)ctx->aspectW / (float)ctx->aspectH;
        float windowAspect = (float)windowW / (float)windowH;

        if (windowAspect > targetAspect) {
            // Window is wider than target - pillarbox
            float scale = targetAspect / windowAspect;
            left  = -scale;
            right =  scale;
        } else {
            // Window is taller than target - letterbox
            float scale = windowAspect / targetAspect;
            bottom = -scale;
            top    =  scale;
        }
    }

    float vertices[] = {
        left,  bottom,   0.0f, 0.0f,
        right, bottom,   1.0f, 0.0f,
        right, top,      1.0f, 1.0f,
        left,  top,      0.0f, 1.0f,
    };

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

    glUseProgram(m_shaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    if (m_uTextureLoc >= 0) {
        glUniform1i(m_uTextureLoc, 0);
    }
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    SwapBuffers(m_hdc);

    wglMakeCurrent(nullptr, nullptr);
}