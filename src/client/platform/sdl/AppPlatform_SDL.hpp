#pragma once
#include "client/platform/Application.hpp"
#include "client/systems/input/TouchControls.hpp"
#include "client/systems/input/PlayDevice.hpp"
#include "client/platform/AppPlatform.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

struct SDL_Window;
union SDL_Event;
struct SDL_KeyboardEvent;
struct SDL_TextInputEvent;
struct SDL_MouseMotionEvent;
struct SDL_MouseButtonEvent;

class AppPlatform_SDL : public AppPlatform {
public:
    AppPlatform_SDL(perlin::LaunchArgs args, perlin::ApplicationFactory factory)
        : createApplication(std::move(factory)), args(std::move(args)) {}

    bool Initialize() override;
    void Run() override;
    void Shutdown() override;
    void PumpFrame() override;

    perlin::NativeWindowInfo GetNativeWindowInfo() override;
    void GetFramebufferSize(int& width, int& height) override;
    void RenewRenderSurface() override;
    void SetTitle(const std::string& title) override;
    void ShowSignInCode(const std::string& url, const std::string& code) override;
    void CloseSignIn() override;
    bool TouchEditing() const override { return touch.Editing(); }
    bool ConfirmDialog(const std::string& title, const std::string& message) override;
    std::unique_ptr<perlin::RenderContext> CreateRenderContext() override;
    void SetTouchState(const perlin::TouchState& state) override;
    bool TakeTouchLayout(std::map<std::string, perlin::TouchPlace>& out) override;
    void DrawTouchOverlay(perlin::RenderContext& renderer) override;
    void DrawPointerOverlay(perlin::RenderContext& renderer) override;

private:
    void HandleEvent(const SDL_Event& event);
    float PointerStep() const;
    bool LeaveTouchModeForMouse();
    bool IsTvPointerKey(const SDL_KeyboardEvent& key, perlin::Input* input) const;
    void OnFocusLost(const SDL_Event& event, perlin::Input* input);
    void OnGamepadAdded(const SDL_Event& event, perlin::Input* input);
    void OnGamepadRemoved(const SDL_Event& event, perlin::Input* input);
    void OnGamepadButton(const SDL_Event& event, perlin::Input* input);
    void OnKeyDown(const SDL_KeyboardEvent& key, perlin::Input* input);
    void OnKeyUp(const SDL_KeyboardEvent& key, perlin::Input* input);
    void OnTextInput(const SDL_TextInputEvent& text, perlin::Input* input);
    void OnMouseMotion(const SDL_MouseMotionEvent& motion, perlin::Input* input);
    void OnMouseButtonDown(const SDL_MouseButtonEvent& button, perlin::Input* input);
    void OnMouseButtonUp(const SDL_MouseButtonEvent& button, perlin::Input* input);
    void OnFinger(const SDL_Event& event, perlin::Input* input);
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
    std::atomic<bool> surfaceLost{false};
    std::atomic<bool> backgrounded{false};
    bool recaptureAfterText = false;
    bool touchMode = false;
    bool padPlaying = false;

    bool adaptiveInput = true;

    bool tvMode = false;

    bool virtualPointer = false;
    bool pointerShown = false;
    float pointerX = 0.0f, pointerY = 0.0f;
    uint32_t pointerTexture = 0;
    void MovePointer(float dx, float dy);
    perlin::TouchControls touch;

    SDL_Window* window = nullptr;
    void* metalView = nullptr;
    void* renderSurface =
        nullptr;
    void ResizeRenderSurface();
    perlin::NativeWindowInfo native;
    perlin::ApplicationFactory createApplication;
    std::unique_ptr<perlin::Application> game;
    std::atomic<int> framebufferWidth{1280};
    std::atomic<int> framebufferHeight{720};
    std::mutex titleMutex;
    std::string pendingTitle;
    bool titleDirty = false;
    perlin::LaunchArgs args;
};
