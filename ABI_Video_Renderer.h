#pragma once
#include <windows.h>

class IVideoRenderer;

// Common video context shared between the player and renderer.
struct VideoContext {
    IVideoRenderer* renderer;       // Renderer instance
    CRITICAL_SECTION cs;            // Protects pixelBuffer and renderer state
    int width;                      // Video frame width
    int height;                     // Video frame height
    unsigned char* pixelBuffer;     // RGBA pixel buffer from VLC
    HWND hwnd;                      // Target window
    bool ownsDevice;                // True if we created the device
    bool needsReset;                // Device needs reset (set by WM_SIZE)
    int aspectW;                    // Aspect ratio width (0 = stretch)
    int aspectH;                    // Aspect ratio height (0 = stretch)
    LONG frameReady;                // Frame ready signal (external device)
    LONG resizing;                  // Window is being resized (accessed via Interlocked*)
};

class IVideoRenderer {
public:
    virtual ~IVideoRenderer() = default;

    // Initialize the renderer.
    // hwnd: target window
    // width, height: video frame dimensions
    // ownsDevice: true if we create and own the device, false for external
    // externalDevice: renderer-specific device pointer (nullptr if ownsDevice)
    virtual bool Init(HWND hwnd, int width, int height, bool ownsDevice, void* externalDevice) = 0;

    // Release all renderer resources.
    virtual void Cleanup() = 0;

    // Create the render target (offscreen surface / texture).
    virtual bool CreateRenderTarget(int width, int height) = 0;

    // Destroy the render target.
    virtual void DestroyRenderTarget() = 0;

    // Render a frame: copy pixel buffer to render target.
    virtual void RenderFrame(VideoContext* ctx, bool canResetDevice) = 0;

    // Get the underlying device pointer.
    virtual void* GetDevice() = 0;

    // Surface / render target validity.
    virtual bool IsSurfaceValid() const = 0;
    virtual void SetSurfaceValid(bool valid) = 0;
};