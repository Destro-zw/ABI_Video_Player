cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
project(ABI_Video_Player LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# Consumers can override these with -D... or set(... CACHE ... FORCE) before
# calling FetchContent_MakeAvailable. At least one renderer must be enabled.
# ---------------------------------------------------------------------------
option(ABI_VIDEO_RENDERER_DX9    "Enable the DirectX 9 renderer"                          ON)
option(ABI_VIDEO_RENDERER_OPENGL "Enable the OpenGL renderer (requires Python + glad2)"  OFF)
option(ABI_VIDEO_ZIP_PACKAGE     "Enable PhysFS .zip package support"                     ON)
option(ABI_VIDEO_AS_DLL          "Build as a shared library (DLL) instead of static"      OFF)

if(NOT ABI_VIDEO_RENDERER_DX9 AND NOT ABI_VIDEO_RENDERER_OPENGL)
    message(FATAL_ERROR "At least one renderer must be enabled (ABI_VIDEO_RENDERER_DX9 or ABI_VIDEO_RENDERER_OPENGL)")
endif()

# GLAD2 generates C source files and needs a C toolchain.
if(ABI_VIDEO_RENDERER_OPENGL)
    enable_language(C)
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
set(ABI_VIDEO_PLAYER_SOURCES ABI_Video_Player.cpp ABI_Video_Player.h ABI_Video_Renderer.h)

if(ABI_VIDEO_RENDERER_DX9)
    list(APPEND ABI_VIDEO_PLAYER_SOURCES ABI_Video_DX9.cpp ABI_Video_DX9.h)
endif()
if(ABI_VIDEO_RENDERER_OPENGL)
    list(APPEND ABI_VIDEO_PLAYER_SOURCES ABI_Video_OpenGL.cpp ABI_Video_OpenGL.h)
endif()

if(ABI_VIDEO_AS_DLL)
    add_library(ABI_Video_Player SHARED ${ABI_VIDEO_PLAYER_SOURCES})
else()
    add_library(ABI_Video_Player STATIC ${ABI_VIDEO_PLAYER_SOURCES})
endif()

target_compile_features(ABI_Video_Player PUBLIC cxx_std_17)
target_compile_definitions(ABI_Video_Player PRIVATE _WIN32_WINNT=0x0602)
target_include_directories(ABI_Video_Player PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Expose feature macros so the source and consumers know what is compiled in.
set(_ABI_DEFINES)
if(ABI_VIDEO_RENDERER_DX9)
    list(APPEND _ABI_DEFINES ABI_VIDEO_RENDERER_DX9_ENABLED)
endif()
if(ABI_VIDEO_RENDERER_OPENGL)
    list(APPEND _ABI_DEFINES ABI_VIDEO_RENDERER_OPENGL_ENABLED)
endif()
if(ABI_VIDEO_ZIP_PACKAGE)
    list(APPEND _ABI_DEFINES ABI_VIDEO_ZIP_PACKAGE_ENABLED)
endif()
if(ABI_VIDEO_AS_DLL)
    list(APPEND _ABI_DEFINES ABI_VIDEO_AS_DLL_ENABLED)
endif()
target_compile_definitions(ABI_Video_Player PUBLIC ${_ABI_DEFINES})

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# GLAD2 - OpenGL loader (only required when the OpenGL renderer is enabled).
if(ABI_VIDEO_RENDERER_OPENGL AND NOT TARGET glad_gl_core_43)
    if(NOT Python_EXECUTABLE)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_EXECUTABLE)
            set(Python_EXECUTABLE "${Python3_EXECUTABLE}" CACHE FILEPATH "Path to the Python executable (used by glad2)" FORCE)
        endif()
    endif()

    FetchContent_Declare(
        glad
        GIT_REPOSITORY https://github.com/Dav1dde/glad.git
        GIT_TAG        v2.0.8
        SOURCE_SUBDIR  cmake
    )
    FetchContent_MakeAvailable(glad)
    glad_add_library(glad_gl_core_43 STATIC REPRODUCIBLE LOADER API gl:core=4.3 wgl=1)
    # glad_add_library does not link opengl32 (required by wgl.c).
    if(WIN32)
        target_link_libraries(glad_gl_core_43 PUBLIC opengl32)
    endif()
endif()

# Prebuilt libVLC binaries for Windows.
FetchContent_Declare(
    libvlc_bin
    URL https://download.videolan.org/pub/videolan/vlc/3.0.20/win32/vlc-3.0.20-win32.7z
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR ${CMAKE_BINARY_DIR}/libvlc_bin
)
FetchContent_MakeAvailable(libvlc_bin)

set(LIBVLC_INCLUDE_DIR ${CMAKE_BINARY_DIR}/libvlc_bin/sdk/include)
set(LIBVLC_LIBRARY     ${CMAKE_BINARY_DIR}/libvlc_bin/sdk/lib/libvlc.lib)
set(LIBVLCCORE_LIBRARY ${CMAKE_BINARY_DIR}/libvlc_bin/sdk/lib/libvlccore.lib)
target_include_directories(ABI_Video_Player PUBLIC ${LIBVLC_INCLUDE_DIR})

# PhysFS .zip package support (optional).
if(ABI_VIDEO_ZIP_PACKAGE)
    set(ABI_PhysFS_Loader_LOCAL_PATH "" CACHE PATH "Local path to ABI_PhysFS_Loader (leave empty to fetch from GitHub)")
    if(ABI_PhysFS_Loader_LOCAL_PATH)
        FetchContent_Declare(ABI_PhysFS_Loader SOURCE_DIR ${ABI_PhysFS_Loader_LOCAL_PATH})
    else()
        FetchContent_Declare(
            ABI_PhysFS_Loader
            GIT_REPOSITORY https://github.com/ActingBadly/ABI_PhysFS_Loader.git
            GIT_TAG        main
        )
    endif()
    FetchContent_MakeAvailable(ABI_PhysFS_Loader)
endif()

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
set(ABI_VIDEO_PLAYER_LIBS ${LIBVLC_LIBRARY} ${LIBVLCCORE_LIBRARY} comctl32 winmm)

if(ABI_VIDEO_RENDERER_DX9)
    list(APPEND ABI_VIDEO_PLAYER_LIBS d3d9)
endif()
if(ABI_VIDEO_RENDERER_OPENGL)
    list(APPEND ABI_VIDEO_PLAYER_LIBS glad_gl_core_43 opengl32 gdi32)
endif()
if(ABI_VIDEO_ZIP_PACKAGE)
    list(APPEND ABI_VIDEO_PLAYER_LIBS ABI_PhysFS_Loader)
endif()

target_link_libraries(ABI_Video_Player ${ABI_VIDEO_PLAYER_LIBS})

# ---------------------------------------------------------------------------
# Diagnostics
# ---------------------------------------------------------------------------
message(STATUS "ABI_Video_Player configuration:")
message(STATUS "  DX9 renderer:      ${ABI_VIDEO_RENDERER_DX9}")
message(STATUS "  OpenGL renderer:   ${ABI_VIDEO_RENDERER_OPENGL}")
message(STATUS "  ZIP package:       ${ABI_VIDEO_ZIP_PACKAGE}")
message(STATUS "  Build as DLL:      ${ABI_VIDEO_AS_DLL}")
