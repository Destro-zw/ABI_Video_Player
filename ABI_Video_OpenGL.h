#pragma once
#include "ABI_Video_Renderer.h"

#include <glad/gl.h>
typedef struct HGLRC__* HGLRC;
typedef struct HDC__* HDC;

// OpenGL video renderer implementation using GLAD2 for function loading.
class OpenGLVideoRenderer : public IVideoRenderer {
public:
    OpenGLVideoRenderer();
    ~OpenGLVideoRenderer() override;

    bool Init(HWND hwnd, int width, int height, bool ownsDevice, void* externalDevice) override;
    void Cleanup() override;
    bool CreateRenderTarget(int width, int height) override;
    void DestroyRenderTarget() override;
    void RenderFrame(VideoContext* ctx, bool canResetDevice) override;
    void* GetDevice() override;
    bool IsSurfaceValid() const override;
    void SetSurfaceValid(bool valid) override;

private:
    HWND  m_hwnd;       // Window handle (needed to ReleaseDC)
    HDC   m_hdc;        // Window device context (owned or borrowed)
    HGLRC m_hglrc;      // OpenGL rendering context (owned or borrowed)
    unsigned int m_texture;       // GL texture for the video frame
    unsigned int m_vao;           // Vertex array object
    unsigned int m_vbo;           // Vertex buffer object
    unsigned int m_ebo;           // Element buffer object
    unsigned int m_shaderProgram; // Shader program
    GLint m_uTextureLoc;        // Cached texture uniform location
    bool m_surfaceValid;        // Is the surface Valid
    bool m_ownsDevice;          // Does it own the device
    bool m_gladLoaded;  // GLAD2 initialized for this context

    bool InitGLContext(HWND hwnd, int width, int height);
    bool LoadGLAD();
    bool CreateShaderProgram();
};