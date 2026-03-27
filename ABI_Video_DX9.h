#pragma once
#include "ABI_Video_Renderer.h"
#include <d3d9.h>

// DirectX 9 video renderer implementation.
class DX9VideoRenderer : public IVideoRenderer {
public:
    DX9VideoRenderer();
    ~DX9VideoRenderer() override;

    bool Init(HWND hwnd, int width, int height, bool ownsDevice, void* externalDevice) override;
    void Cleanup() override;
    bool CreateRenderTarget(int width, int height) override;
    void DestroyRenderTarget() override;
    void RenderFrame(VideoContext* ctx, bool canResetDevice) override;
    void* GetDevice() override;
    bool IsSurfaceValid() const override;
    void SetSurfaceValid(bool valid) override;

private:
    IDirect3D9* m_d3d;
    IDirect3DDevice9* m_device;
    IDirect3DSurface9* m_surface;
    bool m_surfaceValid;
    bool m_ownsDevice;

    bool InitD3D(HWND hwnd, int width, int height);
    void CleanupD3D();
    void ResetDevice(HWND hwnd, int newW, int newH);
};