#pragma once

namespace perlin {

    struct NativeWindowInfo {
        void* window = nullptr;
        void* display = nullptr;
        bool wayland = false;
    };

    class RenderSurface {
    public:
        virtual ~RenderSurface() = default;

        virtual NativeWindowInfo GetNativeWindowInfo() = 0;

        virtual void GetFramebufferSize(int& width, int& height) {
            width = 1280;
            height = 720;
        }

        virtual bool WindowMinimized() const { return false; }
        virtual bool TakeSurfaceChanged() { return false; }
        virtual void RenewRenderSurface() {}
    };

}
