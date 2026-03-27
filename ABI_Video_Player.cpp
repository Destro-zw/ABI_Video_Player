#include "ABI_Video_Player.h"

#include <commctrl.h>
#include <mmsystem.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

static void DebugPrint(const char* format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

static const wchar_t* VIDEO_WND_CLASS = L"ABI_Video_Player";
static bool wndClassRegistered = false;

// ---------------------------------------------------------------------------
// Window procedures
// ---------------------------------------------------------------------------

static LRESULT CALLBACK VideoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    VideoContext* ctx = (VideoContext*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (ctx) {
        if (msg == WM_SIZE) {
            EnterCriticalSection(&ctx->cs);
            ctx->needsReset = true;
            LeaveCriticalSection(&ctx->cs);
            return 0;
        }
        if (msg == WM_ENTERSIZEMOVE) {
            InterlockedExchange(&ctx->resizing, 1);
        } else if (msg == WM_EXITSIZEMOVE) {
            InterlockedExchange(&ctx->resizing, 0);
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK ExternalSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    VideoContext* ctx = (VideoContext*)dwRefData;

    if (msg == WM_CLOSE) {
        // Ignore close messages while a video is playing in an external window.
        return 0;
    }

    if (msg == WM_SIZE) {
        EnterCriticalSection(&ctx->cs);
        if (ctx->renderer)
            ctx->renderer->SetSurfaceValid(false);
        LeaveCriticalSection(&ctx->cs);
    } else if (msg == WM_ENTERSIZEMOVE) {
        InterlockedExchange(&ctx->resizing, 1);
    } else if (msg == WM_EXITSIZEMOVE) {
        InterlockedExchange(&ctx->resizing, 0);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// libVLC frame callbacks
// ---------------------------------------------------------------------------

static void* lock(void* data, void** p_pixels)
{
    VideoContext* ctx = (VideoContext*)data;
    EnterCriticalSection(&ctx->cs);
    *p_pixels = ctx->pixelBuffer;
    return nullptr;
}

static void unlock(void* data, void* id, void* const* p_pixels)
{
    (void)id;
    (void)p_pixels;
    VideoContext* ctx = (VideoContext*)data;
    LeaveCriticalSection(&ctx->cs);
}

static void display(void* data, void* id)
{
    (void)id;
    VideoContext* ctx = (VideoContext*)data;

    if (!ctx->renderer)
        return;

    if (ctx->ownsDevice) {
        EnterCriticalSection(&ctx->cs);

        // Skip rendering while the user is dragging the window.
        if (InterlockedCompareExchange(&ctx->resizing, 0, 0)) {
            LeaveCriticalSection(&ctx->cs);
            return;
        }

        ctx->renderer->RenderFrame(ctx, true);
        LeaveCriticalSection(&ctx->cs);
    } else {
        InterlockedExchange(&ctx->frameReady, 1);
    }
}

static void RenderDeferredFrame(VideoContext* ctx)
{
    if (!ctx->renderer)
        return;

    // Skip while the user is actively dragging the window border/title.
    if (InterlockedCompareExchange(&ctx->resizing, 0, 0))
        return;

    EnterCriticalSection(&ctx->cs);

    // External devices are owned by the host, so they are never reset here.
    ctx->renderer->RenderFrame(ctx, false);
    LeaveCriticalSection(&ctx->cs);
}

// ---------------------------------------------------------------------------
// VLC_VIDEO
// ---------------------------------------------------------------------------

VLC_VIDEO::VLC_VIDEO()
    : window(nullptr), vlcdone(false), libvlc(nullptr), m(nullptr), mp(nullptr)
{
    static const char* vlc_argv[] = { "--play-and-stop" };
    const int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);
    libvlc = libvlc_new(vlc_argc, vlc_argv);

    if (!libvlc)
        DebugPrint("LibVLC initialization failure.\n");
}

VLC_VIDEO::~VLC_VIDEO()
{
    if (libvlc) {
        libvlc_release(libvlc);
        libvlc = nullptr;
    }
}

// ---------------------------------------------------------------------------
// PhysFS media callbacks (compiled only with .zip package support)
// ---------------------------------------------------------------------------

#if defined(ABI_VIDEO_ZIP_PACKAGE_ENABLED)
struct VLCMediaBuffer {
    unsigned char* data;
    size_t size;
    size_t pos;
};

static int vlc_media_open(void* opaque, void** datap, UINT64* sizep)
{
    VLCMediaBuffer* buf = (VLCMediaBuffer*)opaque;
    buf->pos = 0;
    *datap = buf;
    *sizep = (UINT64)buf->size;
    return 0;
}

static ssize_t vlc_media_read(void* opaque, unsigned char* buf_out, size_t len)
{
    VLCMediaBuffer* buf = (VLCMediaBuffer*)opaque;
    size_t remaining = buf->size - buf->pos;
    if (remaining == 0)
        return 0;

    size_t to_read = len < remaining ? len : remaining;
    CopyMemory(buf_out, buf->data + buf->pos, to_read);
    buf->pos += to_read;
    return (ssize_t)to_read;
}

static int vlc_media_seek(void* opaque, UINT64 offset)
{
    VLCMediaBuffer* buf = (VLCMediaBuffer*)opaque;
    if (offset > (uint64_t)buf->size)
        return -1;
    buf->pos = (size_t)offset;
    return 0;
}

static void vlc_media_close(void* opaque)
{
    VLCMediaBuffer* buf = (VLCMediaBuffer*)opaque;
    free(buf->data);
    free(buf);
}
#endif

// ---------------------------------------------------------------------------
// Fullscreen window creation & renderer factory
// ---------------------------------------------------------------------------

HWND VLC_VIDEO::CreateVideoWindow(int& outWidth, int& outHeight)
{
    if (!wndClassRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = VideoWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = VIDEO_WND_CLASS;
        RegisterClassExW(&wc);
        wndClassRegistered = true;
    }

    outWidth = GetSystemMetrics(SM_CXSCREEN);
    outHeight = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST,
        VIDEO_WND_CLASS,
        L"VLC VIDEO PLAYER",
        WS_POPUP,
        0, 0, outWidth, outHeight,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        ShowCursor(TRUE);
    }

    return hwnd;
}

static IVideoRenderer* CreateRenderer(VideoRendererType type)
{
    switch (type) {
#if defined(ABI_VIDEO_RENDERER_DX9_ENABLED)
        case VideoRendererType::DX9:
            return new DX9VideoRenderer();
#endif
#if defined(ABI_VIDEO_RENDERER_OPENGL_ENABLED)
        case VideoRendererType::OpenGL:
            return new OpenGLVideoRenderer();
#endif
        default:
            return nullptr;
    }
}

int VLC_VIDEO::Play(const char* filename, HWND attachWindow, void* externalDevice,
                    int aspectW, int aspectH, VideoRendererType rendererType)
{
    if (!libvlc) {
        DebugPrint("LibVLC not initialized, skipping video: %s\n", filename);
        return EXIT_FAILURE;
    }

    vlcdone = false;

    const bool ownsWindow = (attachWindow == nullptr);
    const bool ownsDevice = (externalDevice == nullptr);

    int wndW = 0, wndH = 0;

    if (ownsWindow) {
        window = CreateVideoWindow(wndW, wndH);
        if (!window) {
            DebugPrint("Couldn't create video window\n");
            return EXIT_FAILURE;
        }
        attachWindow = window;
    } else {
        RECT clientRect;
        GetClientRect(attachWindow, &clientRect);
        wndW = clientRect.right - clientRect.left;
        wndH = clientRect.bottom - clientRect.top;
    }

    // Guard against zero-size windows (minimized).
    if (wndW <= 0) wndW = 1;
    if (wndH <= 0) wndH = 1;

    // Video frame dimensions.
    int videoWidth, videoHeight;
    if (aspectW > 0 && aspectH > 0) {
        // Keep the buffer height within a sane range and derive the width from
        // the aspect ratio so the frame has the correct proportions.
        int baseH = (wndH < 720) ? wndH : 720;
        if (baseH < 1) baseH = 1;
        if (baseH > 1080) baseH = 1080;
        videoWidth = baseH * aspectW / aspectH;
        videoHeight = baseH;
    } else {
        videoWidth = wndW;
        videoHeight = wndH;
    }

    IVideoRenderer* renderer = CreateRenderer(rendererType);
    if (!renderer) {
        DebugPrint("Failed to create renderer (renderer type not compiled in?)\n");
        if (ownsWindow && window) { DestroyWindow(window); window = nullptr; }
        return EXIT_FAILURE;
    }

    if (!renderer->Init(attachWindow, wndW, wndH, ownsDevice, externalDevice)) {
        delete renderer;
        if (ownsWindow && window) { DestroyWindow(window); window = nullptr; }
        return EXIT_FAILURE;
    }

    VideoContext context = {};
    context.renderer = renderer;
    context.width = videoWidth;
    context.height = videoHeight;
    context.pixelBuffer = new unsigned char[videoWidth * videoHeight * 4]();
    context.hwnd = attachWindow;
    context.ownsDevice = ownsDevice;
    context.needsReset = false;
    context.aspectW = aspectW;
    context.aspectH = aspectH;
    context.frameReady = 0;
    context.resizing = 0;
    InitializeCriticalSection(&context.cs);

    if (ownsWindow) {
        SetWindowLongPtrW(attachWindow, GWLP_USERDATA, (LONG_PTR)&context);
    } else {
        SetWindowSubclass(attachWindow, ExternalSubclassProc, 1, (DWORD_PTR)&context);
        // Disable the external window's close button while a video is playing
        // so the host window can't be closed from under the player.
        HMENU sysMenu = GetSystemMenu(attachWindow, FALSE);
        if (sysMenu)
            EnableMenuItem(sysMenu, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
    }

    auto cleanup = [&]() {
        if (!ownsWindow) {
            if (IsWindow(attachWindow)) {
                RemoveWindowSubclass(attachWindow, ExternalSubclassProc, 1);
                HMENU sysMenu = GetSystemMenu(attachWindow, FALSE);
                if (sysMenu)
                    EnableMenuItem(sysMenu, SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);
            }
        } else {
            SetWindowLongPtrW(attachWindow, GWLP_USERDATA, 0);
        }

        renderer->Cleanup();
        delete renderer;
        delete[] context.pixelBuffer;
        DeleteCriticalSection(&context.cs);
        if (ownsWindow && window) { DestroyWindow(window); window = nullptr; }
        ShowCursor(TRUE);
    };

    if (!renderer->CreateRenderTarget(videoWidth, videoHeight)) {
        cleanup();
        return EXIT_FAILURE;
    }

    // Create the VLC media source: local file, PhysFS package, or fail.
    DWORD fileAttrs = GetFileAttributesA(filename);
    if (fileAttrs != INVALID_FILE_ATTRIBUTES && !(fileAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
        m = libvlc_media_new_path(libvlc, filename);
        if (!m) {
            DebugPrint("Failed to create VLC media for: %s\n", filename);
            cleanup();
            return EXIT_FAILURE;
        }
    }
#if defined(ABI_VIDEO_ZIP_PACKAGE_ENABLED)
    else if (ABI_PhysFS_Check_File_Exists(filename)) {
        PHYSFS_sint64 fileSize = 0;
        void* fileData = ABI_PhysFS_Get_File_Contents(filename, &fileSize);
        if (!fileData || fileSize <= 0 || fileSize > (PHYSFS_sint64)(size_t)-1) {
            DebugPrint("Failed to load video from PhysFS: %s\n", filename);
            if (fileData) free(fileData);
            cleanup();
            return EXIT_FAILURE;
        }

        VLCMediaBuffer* buf = (VLCMediaBuffer*)malloc(sizeof(VLCMediaBuffer));
        if (!buf) {
            free(fileData);
            cleanup();
            return EXIT_FAILURE;
        }
        buf->data = (unsigned char*)fileData;
        buf->size = (size_t)fileSize;
        buf->pos = 0;
        m = libvlc_media_new_callbacks(libvlc,
            vlc_media_open, vlc_media_read, vlc_media_seek, vlc_media_close, buf);
        if (!m) {
            DebugPrint("Failed to create VLC media callbacks for: %s\n", filename);
            free(fileData);
            free(buf);
            cleanup();
            return EXIT_FAILURE;
        }
    }
#endif
    else {
        DebugPrint("Video file not found locally or in PhysFS: %s\n", filename);
        cleanup();
        return EXIT_FAILURE;
    }

    mp = libvlc_media_player_new_from_media(m);
    libvlc_media_release(m);
    m = nullptr;
    if (!mp) {
        DebugPrint("Failed to create VLC media player\n");
        cleanup();
        return EXIT_FAILURE;
    }

    libvlc_video_set_callbacks(mp, lock, unlock, display, &context);
    libvlc_video_set_format(mp, "RGBA", videoWidth, videoHeight, videoWidth * 4);
    libvlc_media_player_play(mp);

    // Previous joystick button state, used to detect a button-1 press.
    DWORD prevJoyButtons = 0;
    {
        JOYINFOEX joyInit = {};
        joyInit.dwSize = sizeof(joyInit);
        joyInit.dwFlags = JOY_RETURNBUTTONS;
        if (joyGetPosEx(JOYSTICKID1, &joyInit) == JOYERR_NOERROR)
            prevJoyButtons = joyInit.dwButtons;
    }

    MSG msg;
    while (!vlcdone) {
        if (libvlc_media_player_get_state(mp) == libvlc_Ended)
            vlcdone = true;

        // Skip the video when joystick button 1 is pressed.
        {
            JOYINFOEX joy = {};
            joy.dwSize = sizeof(joy);
            joy.dwFlags = JOY_RETURNBUTTONS;
            if (joyGetPosEx(JOYSTICKID1, &joy) == JOYERR_NOERROR) {
                DWORD curButtons = joy.dwButtons;
                if ((curButtons & JOY_BUTTON1) && !(prevJoyButtons & JOY_BUTTON1))
                    vlcdone = true;
                prevJoyButtons = curButtons;
            }
        }

        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                vlcdone = true;
                break;
            }
            if (msg.message == WM_KEYDOWN &&
                (msg.wParam == VK_ESCAPE || msg.wParam == VK_SPACE || msg.wParam == VK_RETURN)) {
                vlcdone = true;
                break;
            }
            if (!ownsWindow && msg.hwnd == attachWindow) {
                if ((msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST)
                    || msg.message >= WM_APP)
                    continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!ownsDevice && InterlockedExchange(&context.frameReady, 0)) {
            RenderDeferredFrame(&context);
        }

        Sleep(ownsDevice ? 100 : 1);
    }

    libvlc_media_player_stop(mp);
    libvlc_media_player_release(mp);
    mp = nullptr;
    cleanup();
    return EXIT_SUCCESS;
}

void VLC_VIDEO::Stop()
{
    vlcdone = true;
}

VLC_VIDEO& VLC_VideoInstance()
{
    static VLC_VIDEO instance;
    return instance;
}

int ABI_Video_Play(const char* filename, HWND attachWindow, void* externalDevice,
                   int aspectW, int aspectH, VideoRendererType rendererType)
{
    return VLC_VideoInstance().Play(filename, attachWindow, externalDevice,
                                    aspectW, aspectH, rendererType);
}
