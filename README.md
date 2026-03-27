# ABI_Video_Player

An easy to use C++ video playback library for Windows that plays videos using
libVLC, either in a new fullscreen window or passed through to an existing
DirectX 9 or OpenGL window where it renders directly to that target. It can
also load media from .zip packages using PhysFS via ABI_PhysFS_Loader.

The core (`ABI_Video_Player`) handles VLC playback, window management and the
message loop, while rendering is delegated to a pluggable backend through the
`IVideoRenderer` interface (`ABI_Video_Renderer.h`). Two backends are
provided: DirectX 9 and OpenGL.

## Requirements

- Windows
- A C++17 compiler (tested with MSVC / Visual Studio)
- CMake 3.15+ to build
- Python 3 with the `glad2` package, only when the OpenGL renderer is enabled

libVLC and GLAD2 are fetched automatically at configure time, so no manual
download is required.

## Quick start

```cpp
#include <ABI_Video_Player.h>

// Optional
ABI_PhysFS_Load_Package("Videos.zip");

// Play in a new fullscreen window, stretched to fill it.
ABI_Video_Play("video.mp4");

// Play in a new fullscreen window, keeping a 16:9 aspect ratio.
ABI_Video_Play("video.mp4", nullptr, nullptr, 16, 9);

// Attach to an existing DirectX 9 window. (DX9 must be enabled in CMake)
ABI_Video_Play("video.mp4", hwnd, reinterpret_cast<void*>(d3dDevice));

// Attach to an existing OpenGL window (OpenGL must be enabled in CMake)
ABI_Video_Play("video.mp4", hwnd, reinterpret_cast<void*>(hglrc), 16, 9,
               VideoRendererType::OpenGL);
```

## Renderers

Select a backend with the `VideoRendererType` enum passed to `ABI_Video_Play`:

- `VideoRendererType::DX9` — DirectX 9 (default when both renderers are enabled)
- `VideoRendererType::OpenGL` — OpenGL via GLAD2

## CMake Options

Choose which renderers to compile in and how the library is built:

| Option | Default | Description |
|---|---|---|
| `ABI_VIDEO_RENDERER_DX9` | `ON` | Enable the DirectX 9 renderer |
| `ABI_VIDEO_RENDERER_OPENGL` | `OFF` | Enable the OpenGL renderer (requires Python + `glad2`) |
| `ABI_VIDEO_ZIP_PACKAGE` | `ON` | Enable PhysFS `.zip` package support |
| `ABI_VIDEO_AS_DLL` | `OFF` | Build as a shared library (DLL) instead of a static library |

At least one renderer must be enabled. When only one is enabled, it becomes the
default for `ABI_Video_Play`.

### Examples

**DX9** (no Python/GLAD needed):
```cmake
set(ABI_VIDEO_RENDERER_DX9 ON CACHE BOOL "" FORCE)
set(ABI_VIDEO_RENDERER_OPENGL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ABI_Video_Player)
```

**OpenGL** (no d3d9 linked):
```cmake
set(ABI_VIDEO_RENDERER_DX9 OFF CACHE BOOL "" FORCE)
set(ABI_VIDEO_RENDERER_OPENGL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ABI_Video_Player)
```

**Optional PhysFS Zip Loader** (ABI_PhysFS_Loader):
```cmake
set(ABI_VIDEO_ZIP_PACKAGE ON CACHE BOOL "" FORCE)
```

**Optional .dll build** (WIP):
```cmake
set(ABI_VIDEO_AS_DLL ON CACHE BOOL "" FORCE)
```

## Parameters

| Parameter | Description |
|---|---|
| `filename` | Path to the video file (filesystem or PhysFS). |
| `attachWindow` | Existing window to render into. Pass `nullptr` to create a new fullscreen window. |
| `externalDevice` | Existing device/context to render with (renderer-specific). `IDirect3DDevice9*` for DX9, `HGLRC` for OpenGL — cast to `void*`. Pass `nullptr` to create an internal device. |
| `aspectW` | Desired render aspect ratio width (e.g. `16`). Pass `0` to stretch and fill the entire window. |
| `aspectH` | Desired render aspect ratio height (e.g. `9`). Pass `0` to stretch and fill the entire window. |
| `rendererType` | Rendering backend (`VideoRendererType::DX9` by default, `VideoRendererType::OpenGL` also supported). |

> **Note:** `externalDevice` is passed as `void*` to keep the public API
> renderer-agnostic. For the **DX9** renderer, cast your `IDirect3DDevice9*`
> (e.g. `reinterpret_cast<void*>(d3dDevice)`); for the **OpenGL** renderer,
> cast your `HGLRC` (e.g. `reinterpret_cast<void*>(hglrc)`).

## Building

To add ABI_Video_Player to your project, simply add the following to your
CMakeLists.txt. No download is required as the CMake script will fetch
everything for you. You can still download/clone ABI_Video_Player and modify it
as needed, just make note on how to add a local folder path to the CMake code
below.

```cmake
include(FetchContent)

# Add local folder path inside "" if you manually downloaded or cloned
# ABI_Video_Player. Otherwise it will fetch the repository automatically from
# GitHub.
set(ABI_Video_Player_LOCAL_PATH "" CACHE PATH "Local ABI_Video_Player path")

if(ABI_Video_Player_LOCAL_PATH)
  FetchContent_Declare(ABI_Video_Player SOURCE_DIR ${ABI_Video_Player_LOCAL_PATH})
else()
  FetchContent_Declare(
    ABI_Video_Player
    GIT_REPOSITORY https://github.com/Destro-zw/ABI_Video_Player.git
    GIT_TAG        main
  )
endif()

# Set your build options.
set(ABI_VIDEO_RENDERER_DX9 ON CACHE BOOL "" FORCE)
set(ABI_VIDEO_RENDERER_OPENGL OFF CACHE BOOL "" FORCE)
set(ABI_VIDEO_ZIP_PACKAGE ON CACHE BOOL "" FORCE)
set(ABI_VIDEO_AS_DLL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(ABI_Video_Player)

target_link_libraries(your_app PRIVATE ABI_Video_Player)
```