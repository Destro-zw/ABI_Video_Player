#include "ABI_Video_DX9.h"

// Fills a D3D9 present-parameters struct for both device creation and reset.
static void FillPresentParams(D3DPRESENT_PARAMETERS& pp, HWND hwnd, int width, int height)
{
    pp.BackBufferWidth = width;
    pp.BackBufferHeight = height;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
}

DX9VideoRenderer::DX9VideoRenderer()
    : m_d3d(nullptr), m_device(nullptr), m_surface(nullptr),
      m_surfaceValid(false), m_ownsDevice(false)
{
}

DX9VideoRenderer::~DX9VideoRenderer()
{
    Cleanup();
}

bool DX9VideoRenderer::InitD3D(HWND hwnd, int width, int height)
{
    m_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!m_d3d) {
        OutputDebugStringA("Failed to create Direct3D9\n");
        return false;
    }

    D3DPRESENT_PARAMETERS pp = {};
    FillPresentParams(pp, hwnd, width, height);

    HRESULT hr = m_d3d->CreateDevice(0, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp, &m_device);
    if (FAILED(hr)) {
        char _buf[64]; 
        wsprintfA(_buf, "Failed to create D3D9 device: 0x%08X\n", (UINT)hr); 
        OutputDebugStringA(_buf);
        m_d3d->Release();
        m_d3d = nullptr;
        return false;
    }
    return true;
}

void DX9VideoRenderer::CleanupD3D()
{
    if (m_device) {
        m_device->Release();
        m_device = nullptr;
    }
    if (m_d3d) {
        m_d3d->Release();
        m_d3d = nullptr;
    }
}

void DX9VideoRenderer::ResetDevice(HWND hwnd, int newW, int newH)
{
    if (!m_device)
        return;

    D3DPRESENT_PARAMETERS pp = {};
    FillPresentParams(pp, hwnd, newW, newH);

    m_device->Reset(&pp);
}

bool DX9VideoRenderer::Init(HWND hwnd, int width, int height, bool ownsDevice, void* externalDevice)
{
    m_ownsDevice = ownsDevice;

    if (ownsDevice) {
        if (!InitD3D(hwnd, width, height))
            return false;
    } else {
        m_device = (IDirect3DDevice9*)externalDevice;
        D3DDEVICE_CREATION_PARAMETERS cp = {};
        if (SUCCEEDED(m_device->GetCreationParameters(&cp))) {
            if (!(cp.BehaviorFlags & D3DCREATE_MULTITHREADED)) {
                OutputDebugStringA("Warning: External D3D9 device was not created with D3DCREATE_MULTITHREADED. "
                                 "Rendering is deferred to the calling thread to avoid races.\n");
            }
        }
    }
    return true;
}

void DX9VideoRenderer::Cleanup()
{
    DestroyRenderTarget();
    if (m_ownsDevice) {
        CleanupD3D();
    } else {
        m_device = nullptr;
    }
}

bool DX9VideoRenderer::CreateRenderTarget(int width, int height)
{
    if (!m_device)
        return false;

    DestroyRenderTarget();

    HRESULT hr = m_device->CreateOffscreenPlainSurface(
        width, height, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
        &m_surface, nullptr);
    if (SUCCEEDED(hr)) {
        m_surfaceValid = true;
        return true;
    }
    char _buf[64]; 
    wsprintfA(_buf, "Failed to create D3D9 offscreen surface: 0x%08X\n", (UINT)hr); 
    OutputDebugStringA(_buf);
    return false;
}

void DX9VideoRenderer::DestroyRenderTarget()
{
    if (m_surface) {
        m_surface->Release();
        m_surface = nullptr;
    }
    m_surfaceValid = false;
}

void* DX9VideoRenderer::GetDevice()
{
    return (void*)m_device;
}

bool DX9VideoRenderer::IsSurfaceValid() const
{
    return m_surfaceValid;
}

void DX9VideoRenderer::SetSurfaceValid(bool valid)
{
    m_surfaceValid = valid;
}

void DX9VideoRenderer::RenderFrame(VideoContext* ctx, bool canResetDevice)
{
    if (!m_device)
        return;

    if (canResetDevice && ctx->needsReset) {
        RECT rc;
        GetClientRect(ctx->hwnd, &rc);
        int newW = rc.right - rc.left;
        int newH = rc.bottom - rc.top;
        if (newW > 0 && newH > 0) {
            DestroyRenderTarget();
            ResetDevice(ctx->hwnd, newW, newH);
            ctx->needsReset = false;
        }
    }

    HRESULT hr = m_device->TestCooperativeLevel();
    if (FAILED(hr)) {
        m_surfaceValid = false;
        if (canResetDevice && hr == D3DERR_DEVICENOTRESET) {
            RECT rc;
            GetClientRect(ctx->hwnd, &rc);
            int newW = rc.right - rc.left;
            int newH = rc.bottom - rc.top;
            if (newW > 0 && newH > 0) {
                DestroyRenderTarget();
                ResetDevice(ctx->hwnd, newW, newH);
            }
        }
        return;
    }

    if (!m_surfaceValid || !m_surface) {
        if (m_surface) { m_surface->Release(); m_surface = nullptr; }
        hr = m_device->CreateOffscreenPlainSurface(
            ctx->width, ctx->height, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
            &m_surface, nullptr);
        if (SUCCEEDED(hr)) {
            m_surfaceValid = true;
        } else {
            return;
        }
    }

    D3DLOCKED_RECT locked;
    hr = m_surface->LockRect(&locked, nullptr, D3DLOCK_DISCARD);
    if (SUCCEEDED(hr)) {
        const int pitch = locked.Pitch;
        const int rowBytes = ctx->width * 4;
        for (int y = 0; y < ctx->height; y++) {
            const unsigned char* src = ctx->pixelBuffer + y * rowBytes;
            unsigned char* dst = (unsigned char*)locked.pBits + y * pitch;
            // BGRA swap: VLC provides RGBA, D3D9 stores pixel data as BGRA
            for (int x = 0; x < ctx->width; x++) {
                dst[x * 4 + 0] = src[x * 4 + 2]; // B
                dst[x * 4 + 1] = src[x * 4 + 1]; // G
                dst[x * 4 + 2] = src[x * 4 + 0]; // R
                dst[x * 4 + 3] = 0xFF;           // A
            }
        }
        m_surface->UnlockRect();
    } else {
        return;
    }

    IDirect3DSurface9* backBuffer = nullptr;
    hr = m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    if (SUCCEEDED(hr) && backBuffer) {
        D3DSURFACE_DESC bbDesc;
        backBuffer->GetDesc(&bbDesc);
        int bbW = (int)bbDesc.Width;
        int bbH = (int)bbDesc.Height;

        if (bbW > 0 && bbH > 0) {
            RECT destRect;
            if (ctx->aspectW > 0 && ctx->aspectH > 0) {
                float targetAspect = (float)ctx->aspectW / (float)ctx->aspectH;
                RECT clientRect;
                GetClientRect(ctx->hwnd, &clientRect);
                int clientW = clientRect.right - clientRect.left;
                int clientH = clientRect.bottom - clientRect.top;
                if (clientW < 1) clientW = 1;
                if (clientH < 1) clientH = 1;
                float windowAspect = (float)clientW / (float)clientH;
                if (windowAspect > targetAspect) {
                    int scaledW = (int)(clientH * targetAspect + 0.5f);
                    int offsetX = (clientW - scaledW) / 2;
                    destRect.left   = offsetX * bbW / clientW;
                    destRect.right  = (offsetX + scaledW) * bbW / clientW;
                    destRect.top    = 0;
                    destRect.bottom = bbH;
                } else {
                    int scaledH = (int)(clientW / targetAspect + 0.5f);
                    int offsetY = (clientH - scaledH) / 2;
                    destRect.left   = 0;
                    destRect.right  = bbW;
                    destRect.top    = offsetY * bbH / clientH;
                    destRect.bottom = (offsetY + scaledH) * bbH / clientH;
                }
            } else {
                destRect.left = 0;
                destRect.top = 0;
                destRect.right = bbW;
                destRect.bottom = bbH;
            }

            m_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
            m_device->StretchRect(m_surface, nullptr, backBuffer, &destRect, D3DTEXF_LINEAR);
        }
        backBuffer->Release();
    }

    hr = m_device->Present(nullptr, nullptr, nullptr, nullptr);
    if (FAILED(hr)) {
        m_surfaceValid = false;
    }
}