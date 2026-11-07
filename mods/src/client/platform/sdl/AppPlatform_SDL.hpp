#pragma once
#include "client/Perlin.hpp"
#include "client/input/TouchControls.hpp"
#include "client/platform/AppPlatform.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

struct SDL_Window;
union SDL_Event;

class AppPlatform_SDL : public AppPlatform {
public:
    explicit AppPlatform_SDL(perlin::LaunchArgs args) : args(std::move(args)) {}

    bool Initialize() override;
    void Run() override;
    void Shutdown() override;
    void PumpFrame() override;

    NativeWindowInfo GetNativeWindowInfo() override;
    void GetFramebufferSize(int& width, int& height) override;
    void RenewRenderSurface() override;
    void SetTitle(const std::string& title) override;
    bool HasProgressWindow() const override { return !progFailed; }
    void SetProgressWindow(bool visible, const std::string& title, const std::string& line1,
                           const std::string& line2, float fraction) override;
    bool TakeProgressCancel() override { return progCancel.exchange(false); }
    void ShowSignInCode(const std::string& url, const std::string& code) override;
    bool ConfirmDialog(const std::string& title, const std::string& message) override;
    std::unique_ptr<perlin::RenderContext> CreateRenderContext() override;
    void SetTouchState(const perlin::TouchState& state) override;
    bool TakeTouchLayout(std::map<std::string, perlin::TouchPlace>& out) override;
    void DrawTouchOverlay(perlin::RenderContext& renderer) override;
    void DrawPointerOverlay(perlin::RenderContext& renderer) override;

private:
    void HandleEvent(const SDL_Event& event);
    void SetTouchMode(bool on);
    void PumpGamepad();
    struct SDL_Gamepad* gamepad = nullptr;
    bool padState[64] = {};
    std::chrono::steady_clock::time_point padLast;
    float padScroll = 0.0f;
    void SetClipboard(const std::string& text) override;
    std::string GetClipboard() override;
    double DisplayRefreshHz() override;
    bool WindowFocused() const override;
    void Vibrate(int milliseconds) override;
    bool WindowMinimized() const override;
    void OpenUrl(const std::string& url) override;
    bool PickFile(const std::string& kind) override;
    bool TakeSurfaceChanged() override { return surfaceChanged.exchange(false); }
    void SetCursorCaptured(bool captured) override;
    void SetFullscreen(bool fullscreen) override;
    bool IsFullscreen() const override;
    bool cursorCaptured = false;
    bool textActive = false;
    bool keyboardShown = false;
    int keyboardPending = 0;
    bool editMouseDown = false;
    std::atomic<bool> surfaceChanged{false};
    void* lastNativeWindow = nullptr;
    bool surfaceLost = false;
    bool backgrounded = false; // iOS: no gpu work allowed until the app is in front again
    bool recaptureAfterText = false;
    bool touchMode = false;
    // a mouse, keyboard or controller may take over from touch (desktop, Chromebooks, TVs, DeX); phones stay touch
    bool adaptiveInput = true;
    // Android TV / Fire TV: lean-back, remote or controller, nothing to touch
    bool tvMode = false;
    // menus with a controller or remote where the cursor cannot be warped: a pointer drawn by the game
    bool virtualPointer = false;
    bool pointerShown = false;
    float pointerX = 0.0f, pointerY = 0.0f;
    uint32_t pointerTexture = 0;
    void MovePointer(float dx, float dy);
    perlin::TouchControls touch;

    SDL_Window* window = nullptr;
    void* metalView = nullptr;
    void* renderSurface =
        nullptr; // windows: child window the swapchain lives on once a backend switch has happened
    void ResizeRenderSurface();
    NativeWindowInfo native;
    std::unique_ptr<perlin::Perlin> game;
    std::atomic<int> framebufferWidth{1280};
    std::atomic<int> framebufferHeight{720};
    std::mutex titleMutex;
    std::string pendingTitle;
    bool titleDirty = false;
    std::mutex progMutex;
    bool progWant = false, progDirty = false;
    std::string progTitle, progLine1, progLine2;
    float progFraction = 0.0f;
    struct SDL_Window* progWindow = nullptr;
    struct SDL_Renderer* progRenderer = nullptr;
    unsigned progWindowId = 0;
    std::atomic<bool> progFailed{false};
    std::atomic<bool> progCancel{false};
    void UpdateProgressWindow();
    perlin::LaunchArgs args;
};
