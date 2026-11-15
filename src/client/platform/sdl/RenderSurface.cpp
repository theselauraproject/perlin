#include "client/platform/sdl/AppPlatform_SDL.hpp"
#include "util/Log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

#if defined(SDL_PLATFORM_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
    LRESULT CALLBACK RenderSurfaceProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        HWND parent = GetParent(hwnd);
        switch (message) {
        case WM_NCHITTEST:

            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_SETFOCUS:
            if (parent)
                SetFocus(parent);
            return 0;
        case WM_SETCURSOR:
            if (parent)
                return SendMessageW(parent, message, reinterpret_cast<WPARAM>(parent), lParam);
            break;
        default:

            if (parent && ((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
                           (message >= WM_KEYFIRST && message <= WM_KEYLAST) || message == WM_MOUSEHWHEEL))
                return SendMessageW(parent, message, wParam, lParam);
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

void AppPlatform_SDL::RenewRenderSurface() {
    if (!window)
        return;
    HWND parent = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!parent)
        return;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    static ATOM surfaceClass = 0;
    if (!surfaceClass) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = RenderSurfaceProc;
        wc.hInstance = instance;
        wc.lpszClassName = L"PerlinRenderSurface";
        surfaceClass = RegisterClassExW(&wc);
    }
    if (renderSurface) {
        DestroyWindow(static_cast<HWND>(renderSurface));
        renderSurface = nullptr;
    }
    RECT rect{};
    GetClientRect(parent, &rect);
    HWND child = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_NOPARENTNOTIFY, L"PerlinRenderSurface", L"",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, std::max<LONG>(1, rect.right),
                                 std::max<LONG>(1, rect.bottom), parent, nullptr, instance, nullptr);
    if (!child) {
        perlin::Log("sdl", "could not create a render surface ({}); keeping the game window", GetLastError());
        return;
    }
    SetWindowPos(child, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    renderSurface = child;
    native.window = child;

    if (GetFocus() != parent && GetForegroundWindow() == parent)
        SetFocus(parent);
    perlin::Log("sdl", "new render surface for the next backend");
}

void AppPlatform_SDL::ResizeRenderSurface() {
    if (!renderSurface || !window)
        return;
    HWND parent = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    RECT rect{};
    if (parent && GetClientRect(parent, &rect))
        SetWindowPos(static_cast<HWND>(renderSurface), nullptr, 0, 0, std::max<LONG>(1, rect.right),
                     std::max<LONG>(1, rect.bottom), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}
#elif defined(SDL_PLATFORM_APPLE)
void AppPlatform_SDL::RenewRenderSurface() {
    if (!window)
        return;
    if (metalView)
        SDL_Metal_DestroyView(static_cast<SDL_MetalView>(metalView));
    metalView = SDL_Metal_CreateView(window);
    native.window = metalView ? SDL_Metal_GetLayer(static_cast<SDL_MetalView>(metalView)) : nullptr;
}

void AppPlatform_SDL::ResizeRenderSurface() {}
#else
void AppPlatform_SDL::RenewRenderSurface() {}
void AppPlatform_SDL::ResizeRenderSurface() {}
#endif
