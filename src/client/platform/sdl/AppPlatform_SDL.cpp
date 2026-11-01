#include <algorithm>
#include "client/platform/sdl/AppPlatform_SDL.hpp"
#include "client/renderer/gpu/RenderContext_rhi.hpp"
#include "client/input/Keys.hpp"
#include "client/input/KeyMapping.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "util/Paths.hpp"
#include "util/Resources.hpp"
#include "client/resources/TextureLoader.hpp"
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <memory>
#include <iostream>
#include <thread>
#include "util/Log.hpp"
#include "util/Platform.hpp"
#ifdef PERLIN_IOS
#include "client/platform/ios/DocumentPicker.hpp"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <miniz.h>
#endif

namespace {
    int KeyFromScancode(SDL_Scancode sc) {
        namespace K = perlin::Key;
        if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
            return 'A' + (sc - SDL_SCANCODE_A);
        if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
            return '1' + (sc - SDL_SCANCODE_1);
        if (sc == SDL_SCANCODE_0)
            return '0';
        if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
            return K::F1 + (sc - SDL_SCANCODE_F1);
        switch (sc) {
        case SDL_SCANCODE_SPACE:
            return K::Space;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:
            return K::Shift;
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
            return K::Control;
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:
            return K::Alt;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_AC_BACK:
            return K::Escape;
        case SDL_SCANCODE_RETURN:
            return K::Enter;
        case SDL_SCANCODE_TAB:
            return K::Tab;
        case SDL_SCANCODE_BACKSPACE:
            return K::Backspace;
        case SDL_SCANCODE_PAGEUP:
            return K::PageUp;
        case SDL_SCANCODE_PAGEDOWN:
            return K::PageDown;
        case SDL_SCANCODE_HOME:
            return K::Home;
        case SDL_SCANCODE_END:
            return K::End;
        case SDL_SCANCODE_DELETE:
            return K::Delete;
        case SDL_SCANCODE_SLASH:
            return K::Slash;
        case SDL_SCANCODE_LEFT:
            return K::Left;
        case SDL_SCANCODE_UP:
            return K::Up;
        case SDL_SCANCODE_RIGHT:
            return K::Right;
        case SDL_SCANCODE_DOWN:
            return K::Down;
        default:
            return 0;
        }
    }

    int ButtonIndex(Uint8 button) {
        switch (button) {
        case SDL_BUTTON_LEFT:
            return 0;
        case SDL_BUTTON_RIGHT:
            return 1;
        case SDL_BUTTON_MIDDLE:
            return 2;
        default:
            return -1;
        }
    }

    void ApplyWindowIcon(SDL_Window* window) {
#if !defined(PERLIN_MOBILE) && !defined(__EMSCRIPTEN__)
        std::string png;
        perlin::Texture icon;
        if (!window || !perlin::resources::Read("perlin_icon.png", png) ||
            !perlin::LoadTextureMemory(reinterpret_cast<const unsigned char*>(png.data()), png.size(),
                                       icon) ||
            icon.width <= 0 || icon.height <= 0)
            return;
        if (SDL_Surface* surface = SDL_CreateSurfaceFrom(icon.width, icon.height, SDL_PIXELFORMAT_RGBA32,
                                                         icon.pixels.data(), icon.width * 4)) {
            SDL_SetWindowIcon(window, surface);
            SDL_DestroySurface(surface);
        }
#else
        (void)window;
#endif
    }

#if defined(SDL_PLATFORM_LINUX) && !defined(SDL_PLATFORM_ANDROID)
    void RegisterDesktopEntry() {
        std::error_code ec;
        const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (ec || exe.empty())
            return;
        std::filesystem::path data;
        if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
            data = xdg;
        else if (const char* home = std::getenv("HOME"); home && *home)
            data = std::filesystem::path(home) / ".local" / "share";
        else
            return;
        const auto writeIfChanged = [&](const std::filesystem::path& path, const std::string& content) {
            std::string current;
            if (std::ifstream in(path, std::ios::binary); in)
                current.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            if (current == content)
                return;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
        };
        std::string png;
        if (perlin::resources::Read("perlin_icon.png", png))
            writeIfChanged(data / "icons" / "hicolor" / "256x256" / "apps" / "io.perlin.client.png", png);
        std::string command = "\"";
        for (const char c : exe.string()) {
            if (c == '"' || c == '`' || c == '$' || c == '\\')
                command += "\\\\";
            if (c == '%')
                command += '%';
            command += c;
        }
        command += "\"";
        writeIfChanged(data / "applications" / "io.perlin.client.desktop",
                       "[Desktop Entry]\nType=Application\nName=Perlin\nExec=" + command +
                           "\nIcon=io.perlin.client\nTerminal=false\nCategories=Game;\nStartupWMClass=io."
                           "perlin.client\n");
    }
#endif
}

#ifdef __EMSCRIPTEN__
// clang-format off
EM_JS(int, perlin_js_confirm, (const char* title, const char* message), {
    return confirm(UTF8ToString(title) + "\n\n" + UTF8ToString(message)) ? 1 : 0;
});
// clang-format on
#endif

std::unique_ptr<perlin::RenderContext> AppPlatform_SDL::CreateRenderContext() {
    return std::make_unique<perlin::RenderContext_rhi>();
}

NativeWindowInfo AppPlatform_SDL::GetNativeWindowInfo() {
    return native;
}

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
            // clicks, moves and the cursor all belong to the game window underneath
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
            // should a driver or an odd window order route input here anyway, hand it straight to the game window
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
    // creating a window must not move keyboard focus off the game window
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

void AppPlatform_SDL::GetFramebufferSize(int& width, int& height) {
    width = framebufferWidth;
    height = framebufferHeight;
}

void AppPlatform_SDL::SetTitle(const std::string& title) {
    std::lock_guard lock(titleMutex);
    pendingTitle = title;
    titleDirty = true;
}

void AppPlatform_SDL::SetProgressWindow(bool visible, const std::string& title, const std::string& line1,
                                        const std::string& line2, float fraction) {
    std::lock_guard lock(progMutex);
    progWant = visible;
    progTitle = title;
    progLine1 = line1;
    progLine2 = line2;
    progFraction = fraction;
    progDirty = true;
}

void AppPlatform_SDL::UpdateProgressWindow() {
#if !defined(__EMSCRIPTEN__) && !defined(PERLIN_MOBILE)
    bool want, dirty;
    std::string title, l1, l2;
    float frac;
    {
        std::lock_guard lock(progMutex);
        want = progWant;
        dirty = progDirty;
        title = progTitle;
        l1 = progLine1;
        l2 = progLine2;
        frac = progFraction;
        progDirty = false;
    }
    if (!want) {
        if (progRenderer) {
            SDL_DestroyRenderer(progRenderer);
            progRenderer = nullptr;
        }
        if (progWindow) {
            SDL_DestroyWindow(progWindow);
            progWindow = nullptr;
            progWindowId = 0;
        }
        progCancel = false;
        return;
    }
    if (!dirty && progWindow)
        return;
    if (!progWindow) {
        progWindow = SDL_CreateWindow(title.c_str(), 380, 118, SDL_WINDOW_ALWAYS_ON_TOP);
        if (progWindow)
            progRenderer = SDL_CreateRenderer(progWindow, "software");
        if (!progWindow || !progRenderer) {
            perlin::Log("sdl", "could not create the export progress window: {}", SDL_GetError());
            if (progRenderer)
                SDL_DestroyRenderer(progRenderer);
            if (progWindow)
                SDL_DestroyWindow(progWindow);
            progRenderer = nullptr;
            progWindow = nullptr;
            progFailed = true;
            return;
        }
        progWindowId = SDL_GetWindowID(progWindow);
    }
    SDL_SetWindowTitle(progWindow, title.c_str());
    SDL_Renderer* r = progRenderer;
    const auto fill = [&](float x, float y, float w, float h, Uint8 cr, Uint8 cg, Uint8 cb) {
        SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
        SDL_FRect rect{x, y, w, h};
        SDL_RenderFillRect(r, &rect);
    };
    SDL_SetRenderDrawColor(r, 28, 28, 28, 255);
    SDL_RenderClear(r);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDebugText(r, 14, 12, l1.c_str());
    fill(14, 34, 352, 20, 0, 0, 0);
    fill(15, 35, 350, 18, 85, 85, 85);
    fill(15, 35, 350.0f * std::clamp(frac, 0.0f, 1.0f), 18, 198, 198, 198);
    SDL_SetRenderDrawColor(r, 170, 170, 170, 255);
    SDL_RenderDebugText(r, 14, 62, l2.c_str());
    fill(250, 84, 116, 24, 0, 0, 0);
    fill(251, 85, 114, 20, 170, 170, 170);
    fill(252, 86, 112, 19, 111, 111, 111);
    fill(251, 103, 114, 4, 86, 86, 86);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDebugText(r, 280, 92, "Cancel");
    SDL_RenderPresent(r);
#endif
}

void AppPlatform_SDL::ShowSignInCode(const std::string& url, const std::string& code) {
    struct Job {
        std::string url, code;
    };
    SDL_RunOnMainThread(
        [](void* data) {
            std::unique_ptr<Job> job(static_cast<Job*>(data));
            SDL_SetClipboardText(job->code.c_str());
            SDL_OpenURL(job->url.c_str());
        },
        new Job{url + (!url.contains('?') ? "?otc=" : "&otc=") + code, code}, false);
}

void AppPlatform_SDL::SetClipboard(const std::string& text) {
    SDL_RunOnMainThread(
        [](void* data) {
            std::unique_ptr<std::string> t(static_cast<std::string*>(data));
            SDL_SetClipboardText(t->c_str());
        },
        new std::string(text), false);
}

std::string AppPlatform_SDL::GetClipboard() {
    std::string text;
    SDL_RunOnMainThread(
        [](void* data) {
            if (char* clip = SDL_GetClipboardText()) {
                *static_cast<std::string*>(data) = clip;
                SDL_free(clip);
            }
        },
        &text, true);
    return text;
}

void AppPlatform_SDL::Vibrate(int milliseconds) {
    if (gamepad)
        SDL_RumbleGamepad(gamepad, 0xFFFF, 0xFFFF, static_cast<Uint32>(milliseconds));
}

bool AppPlatform_SDL::WindowFocused() const {
    return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

bool AppPlatform_SDL::WindowMinimized() const {
    return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
}

double AppPlatform_SDL::DisplayRefreshHz() {
    static double cached = 0.0;
    static auto checked = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - checked < std::chrono::seconds(2))
        return cached;
    checked = now;
    struct Query {
        SDL_Window* window;
        double hz;
    } query{window, 0.0};
    SDL_RunOnMainThread(
        [](void* data) {
            auto* q = static_cast<Query*>(data);
            const SDL_DisplayID display =
                q->window ? SDL_GetDisplayForWindow(q->window) : SDL_GetPrimaryDisplay();
            if (const SDL_DisplayMode* mode = display ? SDL_GetCurrentDisplayMode(display) : nullptr)
                q->hz = static_cast<double>(mode->refresh_rate);
        },
        &query, true);
    cached = query.hz;
    return cached;
}

#ifdef __EMSCRIPTEN__
namespace {
    // "open the folder" in a browser: the file, or the whole folder as a zip, is handed to the browser as a download
    void OfferDownload(const std::string& name, const void* data, size_t size) {
        // clang-format off
        MAIN_THREAD_EM_ASM({
            var bytes = HEAPU8.slice($1, $1 + $2);
            var link = document.createElement('a');
            link.href = URL.createObjectURL(new Blob([bytes]));
            link.download = UTF8ToString($0);
            document.body.appendChild(link);
            link.click();
            setTimeout(function () { URL.revokeObjectURL(link.href); link.remove(); }, 10000);
        }, name.c_str(), data, size);
        // clang-format on
    }

    bool DownloadLocalPath(std::string path) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(path, ec))
            return false;
        if (fs::is_regular_file(path, ec)) {
            std::ifstream in(path, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            OfferDownload(fs::path(path).filename().string(), bytes.data(), bytes.size());
            return true;
        }
        mz_zip_archive zip{};
        if (!mz_zip_writer_init_heap(&zip, 0, 1 << 20))
            return false;
        size_t files = 0;
        for (const fs::directory_entry& e : fs::recursive_directory_iterator(path, ec)) {
            if (!e.is_regular_file(ec))
                continue;
            const std::string rel = fs::relative(e.path(), path, ec).generic_string();
            std::ifstream in(e.path(), std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (mz_zip_writer_add_mem(&zip, rel.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION))
                files++;
        }
        void* out = nullptr;
        size_t size = 0;
        const bool ok = files > 0 && mz_zip_writer_finalize_heap_archive(&zip, &out, &size);
        if (ok)
            OfferDownload(fs::path(path).filename().string() + ".zip", out, size);
        mz_zip_writer_end(&zip);
        return ok;
    }
}
#endif

void AppPlatform_SDL::OpenUrl(const std::string& url) {
#ifdef __EMSCRIPTEN__
    if (url.starts_with("file://")) {
        std::string path = url.substr(7);
        while (path.starts_with("//"))
            path.erase(0, 1);
        if (!path.starts_with("/"))
            path.insert(0, "/");
        DownloadLocalPath(path);
        return;
    }
#endif
    SDL_RunOnMainThread(
        [](void* data) {
            std::unique_ptr<std::string> u(static_cast<std::string*>(data));
            SDL_OpenURL(u->c_str());
        },
        new std::string(url), false);
}

namespace {
    // a picked file is copied into <data>/imports/ and handed to the game as if it had been dropped on the window
    std::string ImportName(const std::string& kind, const unsigned char* b, size_t size, std::string ext) {
        if (ext.empty()) {
            ext = ".png";
            if (size >= 4 && b[0] == 'P' && b[1] == 'K')
                ext = ".zip";
            else if (size >= 4 && b[0] == 0 && b[1] == 'a' && b[2] == 's' && b[3] == 'm')
                ext = ".wasm";
        }
        const std::string prefix = kind == "cape" ? "cape_" : kind == "skin" ? "skin_" : "import_";
        return prefix + std::to_string(SDL_GetTicks()) + ext;
    }

    std::string ImportDir() {
        const std::string dir = perlin::paths::DataDir() + "/imports";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

#ifdef __EMSCRIPTEN__
    AppPlatform_SDL* gWebPicker = nullptr;
#endif
}

#ifdef __EMSCRIPTEN__
// the browser's file chooser: the chosen file is written straight into the virtual file system, then reported back
// clang-format off
EM_JS(void, perlin_js_pick_file, (const char* accept, const char* dir, const char* prefix), {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = UTF8ToString(accept);
    const folder = UTF8ToString(dir), pre = UTF8ToString(prefix);
    input.onchange = async () => {
        const file = input.files && input.files[0];
        if (!file) return;
        const data = new Uint8Array(await file.arrayBuffer());
        const dot = file.name.lastIndexOf('.');
        const path = folder + '/' + pre + Date.now() + (dot >= 0 ? file.name.substring(dot).toLowerCase() : '');
        FS.writeFile(path, data);
        const size = lengthBytesUTF8(path) + 1, ptr = _malloc(size);
        stringToUTF8(path, ptr, size);
        _perlin_web_file_picked(ptr);
        _free(ptr);
    };
    input.click();
});
// clang-format on

extern "C" EMSCRIPTEN_KEEPALIVE void perlin_web_file_picked(const char* path) {
    if (gWebPicker && path)
        gWebPicker->PushDropped(path);
}
#endif

bool AppPlatform_SDL::PickFile(const std::string& kind) {
    const bool image = kind == "skin" || kind == "cape";
#if defined(PERLIN_IOS)
    return perlin::ios::PickDocument(kind, [this, kind](std::vector<uint8_t> bytes, std::string ext) {
        const std::string path =
            ImportDir() + "/" + ImportName(kind, bytes.data(), bytes.size(), std::move(ext));
        if (FILE* out = std::fopen(path.c_str(), "wb")) {
            std::fwrite(bytes.data(), 1, bytes.size(), out);
            std::fclose(out);
            PushDropped(path);
        }
    });
#elif defined(__EMSCRIPTEN__)
    gWebPicker = this;
    const std::string prefix = kind == "cape" ? "cape_" : kind == "skin" ? "skin_" : "import_";
    perlin_js_pick_file(image ? "image/png" : ".zip,.mcpack,.mcaddon,.mcworld,.mctemplate,.wasm,.png",
                        ImportDir().c_str(), prefix.c_str());
    return true;
#elif defined(SDL_PLATFORM_ANDROID)
    struct Request {
        AppPlatform_SDL* self;
        std::string kind;
        bool image;
    };
    SDL_RunOnMainThread(
        [](void* data) {
            std::unique_ptr<Request> request(static_cast<Request*>(data));
            static const SDL_DialogFileFilter kImages[] = {{"PNG image", "png"}};
            SDL_PropertiesID props = SDL_CreateProperties();
            if (request->image) {
                SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER,
                                       const_cast<SDL_DialogFileFilter*>(kImages));
                SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
            }
            SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
            SDL_ShowFileDialogWithProperties(
                SDL_FILEDIALOG_OPENFILE,
                [](void* userdata, const char* const* files, int) {
                    std::unique_ptr<Request> req(static_cast<Request*>(userdata));
                    if (!files)
                        return;
                    for (const char* const* f = files; *f; f++) {
                        size_t size = 0;
                        void* bytes = SDL_LoadFile(*f, &size);
                        if (!bytes)
                            continue;
                        const auto* b = static_cast<const unsigned char*>(bytes);
                        const std::string path = ImportDir() + "/" + ImportName(req->kind, b, size, {});
                        if (FILE* out = std::fopen(path.c_str(), "wb")) {
                            std::fwrite(bytes, 1, size, out);
                            std::fclose(out);
                            req->self->PushDropped(path);
                        }
                        SDL_free(bytes);
                    }
                },
                new Request(*request), props);
            SDL_DestroyProperties(props);
        },
        new Request{this, kind, image}, false);
    return true;
#else
    // desktop: the caller opens the folder in the file manager instead, where files can simply be dropped
    (void)image;
    return false;
#endif
}

bool AppPlatform_SDL::ConfirmDialog(const std::string& title, const std::string& message) {
#ifdef __EMSCRIPTEN__
    return perlin_js_confirm(title.c_str(), message.c_str()) == 1;
#else
    SDL_MessageBoxButtonData buttons[2] = {};
    buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    buttons[0].buttonID = 1;
    buttons[0].text = "Yes";
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    buttons[1].buttonID = 0;
    buttons[1].text = "No";
    SDL_MessageBoxData data = {};
    data.flags = SDL_MESSAGEBOX_INFORMATION;
    data.window = window;
    data.title = title.c_str();
    data.message = message.c_str();
    data.numbuttons = 2;
    data.buttons = buttons;
    int pressed = 0;
    if (!SDL_ShowMessageBox(&data, &pressed))
        return false;
    return pressed == 1;
#endif
}

void AppPlatform_SDL::SetTouchState(const perlin::TouchState& state) {
    if (touchMode || state.editing || touch.Editing())
        touch.SetState(state);
}

bool AppPlatform_SDL::TakeTouchLayout(std::map<std::string, perlin::TouchPlace>& out) {
    return touch.TakeEditResult(out);
}

void AppPlatform_SDL::SetTouchMode(bool on) {
    if (on == touchMode)
        return;
    if (game)
        touch.Cancel(game->GetInput());
    touchMode = on;
    if (on) {
        if (cursorCaptured) {
            SDL_SetWindowRelativeMouseMode(window, false);
            cursorCaptured = false;
        }
        if (game)
            game->GetInput().SetCaptured(true);
    } else {
        if (game)
            game->GetInput().SetCaptured(false);
        keyboardShown = false;
        SDL_StopTextInput(window);
    }
}

void AppPlatform_SDL::DrawTouchOverlay(perlin::RenderContext& renderer) {
    if ((!touchMode && !touch.Editing()) || !game)
        return;
    touch.SetViewport(framebufferWidth, framebufferHeight);
    touch.Draw(renderer, touch.Gameplay(game->GetInput()));
}

bool AppPlatform_SDL::Initialize() {
#ifdef __EMSCRIPTEN__
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_ASYNCIFY, "0");
#endif
#ifdef PERLIN_MOBILE
    touchMode = true;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
#ifdef SDL_PLATFORM_ANDROID
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
#ifdef PERLIN_IOS
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
#endif
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (const char* t = std::getenv("PERLIN_TOUCH"))
        touchMode = touchMode || std::atoi(t) != 0;
    SDL_SetAppMetadata("Perlin", nullptr, "io.perlin.client");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        perlin::Log("sdl", "SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Perlin");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 1280);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 720);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, args.hidden);
#ifndef __EMSCRIPTEN__
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
#endif
#ifdef PERLIN_MOBILE
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
#endif
#ifdef SDL_PLATFORM_APPLE
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN, true);
#endif
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!window) {
        perlin::Log("sdl", "window creation failed: {}", SDL_GetError());
        return false;
    }
    ApplyWindowIcon(window);
#if defined(SDL_PLATFORM_LINUX) && !defined(SDL_PLATFORM_ANDROID)
    RegisterDesktopEntry();
#endif

#ifdef __EMSCRIPTEN__
    SDL_SetWindowFillDocument(window, true);
#endif

    SDL_PropertiesID wp = SDL_GetWindowProperties(window);
    const char* driver = SDL_GetCurrentVideoDriver();
    std::string drv = driver ? driver : "";
#if defined(SDL_PLATFORM_EMSCRIPTEN)
    native.window = const_cast<char*>("#canvas");
#elif defined(SDL_PLATFORM_WIN32)
    native.window = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_APPLE)
    // metal and webgpu both draw into this layer (an NSView on macOS, a UIView on iOS)
    metalView = SDL_Metal_CreateView(window);
    native.window = metalView ? SDL_Metal_GetLayer(static_cast<SDL_MetalView>(metalView)) : nullptr;
#elif defined(SDL_PLATFORM_ANDROID)
    native.window = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
    lastNativeWindow = native.window;
#else
    if (drv == "wayland") {
        native.wayland = true;
        native.display = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        native.window = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        native.display = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        native.window = reinterpret_cast<void*>(
            (uintptr_t)SDL_GetNumberProperty(wp, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    }
#endif
    if (!native.window) {
        perlin::Log("sdl", "no native window handle for video driver '{}'", drv);
        return false;
    }
    perlin::Log("sdl", "video driver: {}", drv);

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    framebufferWidth = w > 0 ? w : 1280;
    framebufferHeight = h > 0 ? h : 720;

    game = std::make_unique<perlin::Perlin>(args);
    if (!game->Initialize(this))
        return false;
    if (touchMode)
        game->GetInput().SetCaptured(true);
    if (args.hidden && args.controlPort)
        SetCursorCaptured(true);
#ifndef __EMSCRIPTEN__
    if (!args.hidden)
        SDL_RaiseWindow(window);
    if (!args.hidden && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
        SetCursorCaptured(true);
#endif
    return true;
}

void AppPlatform_SDL::SetFullscreen(bool fullscreen) {
#ifdef PERLIN_MOBILE
    (void)fullscreen;
#else
    if (!window || args.hidden || touchMode)
        return;
    SDL_SetWindowFullscreen(window, fullscreen);
#endif
}

bool AppPlatform_SDL::IsFullscreen() const {
#ifdef PERLIN_MOBILE
    return false;
#else
    return window && !args.hidden && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
#endif
}

void AppPlatform_SDL::SetCursorCaptured(bool captured) {
    if (args.hidden) {
        cursorCaptured = captured && args.controlPort != 0;
        if (game)
            game->GetInput().SetCaptured(cursorCaptured);
        return;
    }
    if (touchMode) {
        if (game)
            game->GetInput().SetCaptured(true);
        return;
    }
    if (!window || captured == cursorCaptured)
        return;
    if (SDL_SetWindowRelativeMouseMode(window, captured)) {
        cursorCaptured = captured;
        if (game)
            game->GetInput().SetCaptured(captured);
    }
}

void AppPlatform_SDL::HandleEvent(const SDL_Event& e) {
    if (progWindowId != 0) {
        Uint32 wid = 0;
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)
            wid = e.key.windowID;
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
            wid = e.button.windowID;
        else if (e.type == SDL_EVENT_MOUSE_MOTION)
            wid = e.motion.windowID;
        else if (e.type == SDL_EVENT_MOUSE_WHEEL)
            wid = e.wheel.windowID;
        else if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST)
            wid = e.window.windowID;
        if (wid == progWindowId) {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)
                progCancel = true;
            else if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                progCancel = true;
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.x >= 250 && e.button.x <= 366 &&
                     e.button.y >= 84 && e.button.y <= 108)
                progCancel = true;
            return;
        }
    }
    perlin::Input* input = game ? &game->GetInput() : nullptr;
    switch (e.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        isRunning = false;
        break;
    case SDL_EVENT_DROP_FILE:
        if (e.drop.data)
            PushDropped(e.drop.data);
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (e.window.data1 > 0 && e.window.data2 > 0) {
            framebufferWidth = e.window.data1;
            framebufferHeight = e.window.data2;
            ResizeRenderSurface();
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_HIDDEN:
        if (args.hidden)
            break;
        if (input && (input->pauseOnLostFocus || e.type != SDL_EVENT_WINDOW_FOCUS_LOST))
            input->ReleaseAll();
        SetCursorCaptured(false);
        break;
    case SDL_EVENT_SCREEN_KEYBOARD_HIDDEN:
        keyboardShown = false;
        break;
#ifdef PERLIN_IOS
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        backgrounded = true;
        if (input)
            input->ReleaseAll();
        SetCursorCaptured(false);
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        backgrounded = false;
        surfaceChanged = true;
        break;
#endif
    case SDL_EVENT_GAMEPAD_ADDED:
        if (!gamepad && (gamepad = SDL_OpenGamepad(e.gdevice.which)) && input)
            input->SetPadConnected(true);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (gamepad && SDL_GetGamepadID(gamepad) == e.gdevice.which) {
            SDL_CloseGamepad(gamepad);
            gamepad = nullptr;
            if (input) {
                input->SetPadConnected(false);
                input->SetPadLook(0, 0);
                for (int c = 0; c < 64; c++)
                    if (padState[c]) {
                        padState[c] = false;
                        input->OnPadUp(c);
                    }
            }
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        if (!input || !gamepad)
            break;
        const bool pressed = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const int code = e.gbutton.button;
        if (code < 0 || code >= 15)
            break;
        if (input->TextMode() && !input->PadCapture()) {
            if (code == perlin::Pad::A) {
                if (pressed)
                    input->OnMouseDown(0);
                else
                    input->OnMouseUp(0);
            } else if ((code == perlin::Pad::B || code == perlin::Pad::Start) && pressed) {
                input->OnKeyDown(perlin::Key::Escape);
                input->OnKeyUp(perlin::Key::Escape);
            }
            break;
        }
        if (code == perlin::Pad::LeftShoulder && pressed)
            input->OnMouseWheel(1.0f);
        else if (code == perlin::Pad::RightShoulder && pressed)
            input->OnMouseWheel(-1.0f);
        if (padState[code] != pressed) {
            padState[code] = pressed;
            if (pressed)
                input->OnPadDown(code);
            else
                input->OnPadUp(code);
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN:
        if (input) {
            const int k = KeyFromScancode(e.key.scancode);
            if (k && !e.key.repeat)
                input->OnKeyDown(k);
            else if (k && e.key.repeat && input->TextMode())
                input->OnKeyRepeat(k);
        }
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (input && input->TextMode()) {
            const char* p = e.text.text;
            while (p && *p) {
                Uint32 cp = SDL_StepUTF8(&p, nullptr);
                if (cp == 0)
                    break;
                input->OnChar(static_cast<int>(cp));
            }
        }
        break;
    case SDL_EVENT_KEY_UP:
        if (input)
            if (int k = KeyFromScancode(e.key.scancode))
                input->OnKeyUp(k);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (e.motion.which == SDL_TOUCH_MOUSEID)
            break;
        if (!touchMode && touch.Editing() && input) {
            if (editMouseDown) {
                const float d = SDL_GetWindowPixelDensity(window);
                touch.FingerMove(*input, 9999, e.motion.x * d, e.motion.y * d, e.motion.xrel * d,
                                 e.motion.yrel * d);
            }
            break;
        }
        if (touchMode) {
#ifdef PERLIN_MOBILE
            break;
#else
            SetTouchMode(false);
#endif
        }
        if (input) {
            if (cursorCaptured)
                input->OnMouseDelta(e.motion.xrel, e.motion.yrel);
            input->OnMouseMove(e.motion.x * SDL_GetWindowPixelDensity(window),
                               e.motion.y * SDL_GetWindowPixelDensity(window));
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.which == SDL_TOUCH_MOUSEID)
            break;
        if (!touchMode && touch.Editing() && input) {
            const float d = SDL_GetWindowPixelDensity(window);
            touch.SetViewport(framebufferWidth, framebufferHeight);
            editMouseDown = true;
            touch.FingerDown(*input, 9999, e.button.x * d, e.button.y * d);
            break;
        }
        if (touchMode) {
#ifdef PERLIN_MOBILE
            break;
#else
            SetTouchMode(false);
#endif
        }
        if (!cursorCaptured && !(input && (input->TextMode() || input->FreeCursor()))) {
            SetCursorCaptured(true);
            break;
        }
        if (input)
            if (int b = ButtonIndex(e.button.button); b >= 0)
                input->OnMouseDown(b);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (input)
            input->OnMouseWheel(e.wheel.y);
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (e.tfinger.touchID == SDL_MOUSE_TOUCHID || e.tfinger.touchID == SDL_PEN_TOUCHID)
            break;
        if (input && !touchMode && e.type == SDL_EVENT_FINGER_DOWN)
            SetTouchMode(true);
        if (input && touchMode) {
            touch.SetViewport(framebufferWidth, framebufferHeight);
            const float w = static_cast<float>(framebufferWidth), h = static_cast<float>(framebufferHeight);
            const int64_t id = static_cast<int64_t>(e.tfinger.fingerID);
            const float x = e.tfinger.x * w, y = e.tfinger.y * h;
            if (e.type == SDL_EVENT_FINGER_DOWN) {
                if (input->Keyboard())
                    keyboardPending = 4;
                touch.FingerDown(*input, id, x, y);
            } else if (e.type == SDL_EVENT_FINGER_MOTION)
                touch.FingerMove(*input, id, x, y, e.tfinger.dx * w, e.tfinger.dy * h);
            else if (e.type == SDL_EVENT_FINGER_UP)
                touch.FingerUp(*input, id, x, y);
            else
                touch.Cancel(*input);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (editMouseDown && input) {
            const float d = SDL_GetWindowPixelDensity(window);
            editMouseDown = false;
            touch.FingerUp(*input, 9999, e.button.x * d, e.button.y * d);
            break;
        }
        if (input)
            if (int b = ButtonIndex(e.button.button); b >= 0)
                input->OnMouseUp(b);
        break;
    default:
        break;
    }
}

void AppPlatform_SDL::PumpGamepad() {
    if (!game)
        return;
    perlin::Input& in = game->GetInput();
    const auto now = std::chrono::steady_clock::now();
    const float dt = padLast.time_since_epoch().count() == 0
                         ? 0.0f
                         : std::min(0.1f, std::chrono::duration<float>(now - padLast).count());
    padLast = now;
    if (!gamepad) {
        in.SetPadLook(0, 0);
        return;
    }
    const auto axis = [&](SDL_GamepadAxis a) {
        return static_cast<float>(SDL_GetGamepadAxis(gamepad, a)) / 32767.0f;
    };
    const float lx = axis(SDL_GAMEPAD_AXIS_LEFTX), ly = axis(SDL_GAMEPAD_AXIS_LEFTY);
    const float rx = axis(SDL_GAMEPAD_AXIS_RIGHTX), ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
    const float lt = axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), rt = axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    const float dz = in.PadDeadzone();
    const auto shape = [&](float v) {
        const float m = std::fabs(v);
        return m <= dz ? 0.0f : std::copysign(std::min(1.0f, (m - dz) / (1.0f - dz)), v);
    };

    if (in.TextMode() && !in.PadCapture()) {
        for (int c = 0; c < 64; c++)
            if (padState[c]) {
                padState[c] = false;
                in.OnPadUp(c);
            }
        in.SetPadLook(0, 0);
        const float sx = shape(lx), sy = shape(ly);
        if (sx != 0.0f || sy != 0.0f) {
            float x = 0, y = 0;
            SDL_GetMouseState(&x, &y);
            const float speed = 700.0f * dt / std::max(1.0f, SDL_GetWindowPixelDensity(window));
            SDL_WarpMouseInWindow(window, x + sx * std::fabs(sx) * speed * 1.4f,
                                  y + sy * std::fabs(sy) * speed * 1.4f);
        }
        padScroll += -shape(ry) * dt * 9.0f;
        if (std::fabs(padScroll) >= 1.0f) {
            in.OnMouseWheel(std::trunc(padScroll));
            padScroll -= std::trunc(padScroll);
        }
        return;
    }

    in.SetPadLook(shape(rx), shape(ry));
    struct Virtual {
        int code;
        bool on;
    };
    const float hi = 0.5f;
    const Virtual virtuals[] = {
        {perlin::Pad::LeftTrigger, lt > 0.4f},   {perlin::Pad::RightTrigger, rt > 0.4f},
        {perlin::Pad::LeftStickUp, ly < -hi},    {perlin::Pad::LeftStickDown, ly > hi},
        {perlin::Pad::LeftStickLeft, lx < -hi},  {perlin::Pad::LeftStickRight, lx > hi},
        {perlin::Pad::RightStickUp, ry < -hi},   {perlin::Pad::RightStickDown, ry > hi},
        {perlin::Pad::RightStickLeft, rx < -hi}, {perlin::Pad::RightStickRight, rx > hi}};
    for (const Virtual& v : virtuals) {
        if (padState[v.code] == v.on)
            continue;
        padState[v.code] = v.on;
        if (v.on)
            in.OnPadDown(v.code);
        else
            in.OnPadUp(v.code);
    }
}

void AppPlatform_SDL::PumpFrame() {
    SDL_Event e;
    while (SDL_PollEvent(&e))
        HandleEvent(e);
#ifdef PERLIN_IOS
    // iOS kills apps that submit gpu work from the background: sleep on the event queue until it is in front again
    while (backgrounded && isRunning && SDL_WaitEvent(&e))
        HandleEvent(e);
#endif
    PumpGamepad();

#ifdef SDL_PLATFORM_ANDROID
    {
        void* nw = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                          SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
        if (!nw) {
            surfaceLost = true;
        } else if (nw != lastNativeWindow || surfaceLost) {
            const bool resumed = lastNativeWindow != nullptr;
            lastNativeWindow = nw;
            if (resumed || surfaceLost) {
                native.window = nw;
                surfaceChanged = true;
                perlin::Log("sdl", "window surface replaced, rebuilding the swap chain");
            }
            surfaceLost = false;
        }
    }
#endif

    {
        std::lock_guard lock(titleMutex);
        if (titleDirty) {
            SDL_SetWindowTitle(window, pendingTitle.c_str());
            titleDirty = false;
        }
    }

    UpdateProgressWindow();

    if (game)
        touch.Update(game->GetInput());

    if (game) {
        perlin::Input& in = game->GetInput();
        const bool text = in.TextMode();
        if (text != textActive) {
            textActive = text;
            if (text) {
                recaptureAfterText = cursorCaptured;
                SetCursorCaptured(false);
                if (!touchMode)
                    SDL_StartTextInput(window);
            } else {
                if (!touchMode)
                    SDL_StopTextInput(window);
                if (recaptureAfterText)
                    SetCursorCaptured(true);
                recaptureAfterText = false;
            }
        }
        // after the grab is released above: warping while captured does nothing
        if (in.TakeCursorCenter() && !cursorCaptured && !touchMode && !args.hidden) {
            int w = 0, h = 0;
            SDL_GetWindowSize(window, &w, &h);
            SDL_WarpMouseInWindow(window, static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.5f);
        }
        if (touchMode) {
            const bool keyboard = in.Keyboard();
            if (!keyboard) {
                keyboardPending = 0;
                if (keyboardShown) {
                    keyboardShown = false;
                    SDL_StopTextInput(window);
                }
            } else if (keyboardPending > 0 && --keyboardPending == 0) {
                keyboardShown = true;
                SDL_StartTextInput(window);
            }
        }
    }

    if (!args.hidden && cursorCaptured && !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
        SetCursorCaptured(false);
}

void AppPlatform_SDL::Run() {
    while (isRunning)
        PumpFrame();
}

void AppPlatform_SDL::Shutdown() {
    if (game) {
        game->Shutdown();
        game.reset();
    }
    if (metalView) {
        SDL_Metal_DestroyView(static_cast<SDL_MetalView>(metalView));
        metalView = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}
