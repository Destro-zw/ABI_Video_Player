#pragma once

#include <windows.h>
#include <atomic>

#include "ABI_Video_Renderer.h"

#if defined(ABI_VIDEO_RENDERER_DX9_ENABLED)
#include "ABI_Video_DX9.h"
#endif
#if defined(ABI_VIDEO_RENDERER_OPENGL_ENABLED)
#include "ABI_Video_OpenGL.h"
#endif
#if defined(ABI_VIDEO_ZIP_PACKAGE_ENABLED)
#include "ABI_PhysFS_Loader.h"
#endif

// libVLC's Windows SDK headers reference ssize_t, which MSVC does not define.
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;

#include "vlc/vlc.h"

// Rendering backend for ABI_Video_Play.
enum class VideoRendererType {
#if defined(ABI_VIDEO_RENDERER_DX9_ENABLED)
    DX9,
#endif
#if defined(ABI_VIDEO_RENDERER_OPENGL_ENABLED)
    OpenGL,
#endif
};

// Backend used when none is specified: the first renderer that was compiled in.
#if defined(ABI_VIDEO_RENDERER_DX9_ENABLED)
static constexpr VideoRendererType kDefaultRenderer = VideoRendererType::DX9;
#elif defined(ABI_VIDEO_RENDERER_OPENGL_ENABLED)
static constexpr VideoRendererType kDefaultRenderer = VideoRendererType::OpenGL;
#else
// This is here for internal consistency, but the build should fail if no renderer is selected.
static constexpr VideoRendererType kDefaultRenderer = static_cast<VideoRendererType>(0);
#endif

class VLC_VIDEO
{
public:
    VLC_VIDEO();
    ~VLC_VIDEO();

    int Play(const char* filename, HWND attachWindow = nullptr, void* externalDevice = nullptr,
             int aspectW = 0, int aspectH = 0,
             VideoRendererType rendererType = kDefaultRenderer);
    void Stop();

private:
    std::atomic<bool> vlcdone;
    libvlc_instance_t* libvlc;
    libvlc_media_t* m;
    libvlc_media_player_t* mp;
    HWND window;

    HWND CreateVideoWindow(int& outWidth, int& outHeight);
};

VLC_VIDEO& VLC_VideoInstance();

// Exported as a C symbol when built as a DLL.
#if defined(ABI_VIDEO_AS_DLL_ENABLED)
extern "C" __declspec(dllexport) int ABI_Video_Play(const char* filename, HWND attachWindow = nullptr,
                                                    void* externalDevice = nullptr,
                                                    int aspectW = 0, int aspectH = 0,
                                                    VideoRendererType rendererType = kDefaultRenderer);
#else
int ABI_Video_Play(const char* filename, HWND attachWindow = nullptr,
                   void* externalDevice = nullptr,
                   int aspectW = 0, int aspectH = 0,
                   VideoRendererType rendererType = kDefaultRenderer);
#endif
