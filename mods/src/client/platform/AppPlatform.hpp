#pragma once
#include <map>
#include <mutex>
#include <vector>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace perlin {
    class RenderContext;
    struct TouchState;
    struct TouchPlace;
}

struct NativeWindowInfo {
    void* window = nullptr;
    void* display = nullptr;
    bool wayland = false;
};

class AppPlatform {
public:
    virtual ~AppPlatform() = default;

    virtual bool Initialize() = 0;
    virtual void Run() = 0;
    virtual void Shutdown() = 0;

    virtual NativeWindowInfo GetNativeWindowInfo() = 0;

    virtual void GetFramebufferSize(int& width, int& height) {
        width = 1280;
        height = 720;
    }

    virtual void SetCursorCaptured(bool captured) { (void)captured; }
    // borderless fullscreen on the window's current display; no-op where the window is always fullscreen
    virtual void SetFullscreen(bool fullscreen) { (void)fullscreen; }
    virtual bool IsFullscreen() const { return false; }

    virtual void PumpFrame() {}

    virtual void SetTitle(const std::string& title) { (void)title; }

    virtual bool HasProgressWindow() const { return false; }
    virtual void SetProgressWindow(bool visible, const std::string& title, const std::string& line1,
                                   const std::string& line2, float fraction) {
        (void)visible;
        (void)title;
        (void)line1;
        (void)line2;
        (void)fraction;
    }
    virtual bool TakeProgressCancel() { return false; }

    virtual bool ConfirmDialog(const std::string& title, const std::string& message) {
        (void)title;
        (void)message;
        return false;
    }

    virtual void ShowSignInCode(const std::string& url, const std::string& code) {
        (void)url;
        (void)code;
    }

    virtual void SetClipboard(const std::string& text) { (void)text; }
    virtual std::string GetClipboard() { return {}; }
    virtual double DisplayRefreshHz() { return 0.0; }
    virtual bool WindowFocused() const { return true; }
    virtual void Vibrate(int milliseconds) { (void)milliseconds; }
    virtual bool WindowMinimized() const { return false; }
    virtual void OpenUrl(const std::string& url) { (void)url; }
    virtual bool TakeSurfaceChanged() { return false; }
    // a fresh native surface for the next graphics backend: windows and metal tie a window to the api that first presented to it,
    // so after a backend switch the old surface can keep showing its last frame; call with the old swapchain already destroyed
    virtual void RenewRenderSurface() {}
    virtual bool PickFile(const std::string& kind) {
        (void)kind;
        return false;
    }

    virtual void SetTouchState(const perlin::TouchState& state) { (void)state; }
    virtual bool TakeTouchLayout(std::map<std::string, perlin::TouchPlace>& out) {
        (void)out;
        return false;
    }
    virtual void DrawTouchOverlay(perlin::RenderContext& renderer) { (void)renderer; }
    // the pointer a gamepad or TV remote moves through menus where the system draws none (TV, phones, tablets)
    virtual void DrawPointerOverlay(perlin::RenderContext& renderer) { (void)renderer; }

    virtual std::unique_ptr<perlin::RenderContext> CreateRenderContext() = 0;

    void PushDropped(const std::string& path) {
        std::lock_guard lock(dropMutex);
        droppedFiles.push_back(path);
    }
    std::vector<std::string> TakeDropped() {
        std::lock_guard lock(dropMutex);
        std::vector<std::string> out;
        out.swap(droppedFiles);
        return out;
    }

    std::atomic<bool> isRunning{true};

private:
    std::mutex dropMutex;
    std::vector<std::string> droppedFiles;
};
