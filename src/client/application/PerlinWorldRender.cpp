#include "client/ui/GuiViewport.hpp"
#include "client/screen/screens/ScreenTemplates.hpp"
#include "client/systems/sprites/ItemData.hpp"
#include "client/application/PerlinFrame.hpp"
#include "client/systems/renderer/CustomSky.hpp"
#include "client/systems/resources/AssetJobs.hpp"
#include "util/Log.hpp"
#include <algorithm>
#include <cstdio>
#include <unordered_set>

namespace perlin {
    void Perlin::UpdateHudAndScreens(FrameContext& f) {
        auto& live = f.live;
        hudScreen.EnsureUi(f);
        if (hud) {
            int fbW = 0, fbH = 0;
            renderer->FramebufferSize(fbW, fbH);
            frame::ProfileScope hudScope(frame::Profiler::Hud);
            hud->Update(hudState, fbW, fbH, options.guiScale);
            if (pause.optionsUi.touchEditing) {
                if (!pause.optionsUi.touchEditHidden) {
                    pause.optionsUi.touchEditHidden = true;
                    hud->Overlay().ClearOverlay();
                }
            } else {
                pause.optionsUi.touchEditHidden = false;
                modHost.UpdateModLayer(*hud);
                UpdateServerLayer(*hud, live, fbW, fbH);
                screen::Frame frame{*hud, live, fbW, fbH};
                screens.Update(frame);
            }
            const bool titleUp = !pause.optionsUi.touchEditing && screens.TookFrame(screen::Id::Title);
            {
                {
                    bool keyboard = chat.IsOpen();
                    if (titleUp)
                        keyboard = keyboard || title.page == ui::TitlePage::Edit ||
                                   title.page == ui::TitlePage::Direct ||
                                   title.page == ui::TitlePage::AddRealm ||
                                   title.page == ui::TitlePage::AccountName ||
                                   title.page == ui::TitlePage::ReplayRename;
                    if (form.IsOpen() && form.model.focus >= 0 &&
                        static_cast<size_t>(form.model.focus) < form.model.elements.size())
                        keyboard =
                            keyboard || form.model.elements[static_cast<size_t>(form.model.focus)].kind ==
                                            ui::FormElement::Kind::Input;
                    if (inventory.IsOpen() && inventory.creative.tab == ui::kCreativeSearchTab)
                        keyboard = true;
                    this->input.SetKeyboard(keyboard);
                }
                const bool pressed = this->input.IsMouseDown(0);
                std::string clickName;
                float clickVolume = 1.0f, clickPitch = 1.0f;
                if (pressed && !uiClickWasDown && !chat.IsOpen() &&
                    hud->Overlay().OverlayHoverClickSound(clickName, clickVolume, clickPitch))
                    sounds.PlayNamed(clickName, {camera.x, camera.y, camera.z}, clickVolume, clickPitch, false);
                uiClickWasDown = pressed;
            }
            hudScreen.Present(f, fbW, fbH);
        }
    }

    void Perlin::UpdateTouch(FrameContext& f) {
        auto& width = f.width;
        auto& height = f.height;
        auto& live = f.live;
        auto& preview = loop->preview;
        TouchState touchState;
        touchState.flying = player.Flying();
        touchState.inWater = player.InWater() && !player.State().onGround;
        touchState.riding = live && live->RidingUid() != 0;
        touchState.editing = pause.optionsUi.touchEditing;
        touchState.hudScale = hud ? hud->Scale() : 0;
        touchState.scale = options.touchScale;
        touchState.opacity = options.touchOpacity;
        touchState.sensitivity = options.touchSensitivity;
        touchState.layout = options.touchLayout;
        for (size_t a = 0; a < kActionCount; a++)
            touchState.bindings[a] = options.GetKey(static_cast<Action>(a));
        platform->SetTouchState(touchState);
        std::map<std::string, TouchPlace> edited;
        if (platform->TakeTouchLayout(edited)) {
            options.touchLayout = edited;
            options.Save();
            pause.optionsUi.touchEditing = false;
            pause.optionsUi.signature.clear();
        }
        const bool previewTouch = preview && PERLIN_ENV("PERLIN_PREVIEW_TOUCH");
        if (previewTouch) {
            static TouchControls previewControls;
            previewControls.SetViewport(width, height);
            previewControls.SetState(touchState);
            previewControls.Draw(*renderer, true);
        } else if (pause.optionsUi.touchEditing || (live && hud && !asset.IsOpen() && !death.IsOpen() && !pause.IsOpen() &&
                                              !inventory.IsOpen() && !WorldCovered())) {
            platform->DrawTouchOverlay(*renderer);
        }
        platform->DrawPointerOverlay(*renderer);
    }
}
