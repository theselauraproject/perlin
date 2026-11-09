#pragma once
#include "client/ui/HudState.hpp"

#include <algorithm>

namespace perlin::hand {

    inline int SwingTicks(int itemTicks, int fastAmplifier, int slowAmplifier) {
        if (fastAmplifier >= 0)
            return itemTicks - (fastAmplifier + 1);
        if (slowAmplifier >= 0)
            return itemTicks + 2 * slowAmplifier + 2;
        return itemTicks;
    }

    struct Swing {
        bool swinging = false;
        int time = 0;
        int duration = 6;
        float anim = 0, oAnim = 0;

        void Start() {
            if (swinging && time < duration / 2 && time >= 0)
                return;
            time = -1;
            swinging = true;
        }

        void Tick(int ticks) {
            duration = ticks;
            oAnim = anim;
            if (swinging && ++time >= duration) {
                swinging = false;
                time = 0;
            } else if (!swinging) {
                time = 0;
            }
            anim = swinging ? static_cast<float>(time) / static_cast<float>(duration) : 0.0f;
        }

        // EntityLivingBase.getSwingProgress: ungated, so the last tick still runs on to 1 instead of snapping
        float Progress(float partial) const {
            float d = anim - oAnim;
            if (d < 0.0f)
                d += 1.0f;
            return oAnim + d * partial;
        }
    };

    inline void TickEquip(ui::HudItem& shown, int& shownSlot, float& height, const ui::HudItem& item, int slot,
                          bool sameItem) {
        const bool slotChanged = slot != shownSlot;
        const bool lower = !(shown.Empty() && item.Empty()) && (slotChanged || !sameItem);
        if (!lower && sameItem)
            shown = item;
        height += std::clamp((lower ? 0.0f : 1.0f) - height, -0.4f, 0.4f);

        if (height <= 0.1f) {
            shown = item;
            shownSlot = slot;
        }
    }
}
