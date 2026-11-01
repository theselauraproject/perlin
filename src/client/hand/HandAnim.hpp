#pragma once
#include "client/ui/HudState.hpp"

#include <algorithm>

namespace perlin::hand {
    // Mob::getCurrentSwingDuration 0x142c24e10: haste or conduit power take amplifier+1 ticks off,
    // otherwise mining fatigue adds 2 per level; -1 when the effect is absent
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

        // Mob::swing 0x142c31210: a running swing restarts only from its second half
        void Start() {
            if (swinging && time < duration / 2 && time >= 0)
                return;
            time = -1;
            swinging = true;
        }

        // Mob::baseTick 0x142c241c0 keeps the old value, Mob::updateAttackAnim 0x142c2d060 steps;
        // a duration of 0 or less ends the swing on its first tick
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

        // Mob::getAttackAnim 0x146e8b2d0: 0 as soon as the swing ends, no blend back to rest
        float Progress(float partial) const {
            if (!swinging)
                return 0.0f;
            float d = anim - oAnim;
            if (d < 0.0f)
                d += 1.0f;
            return oAnim + d * partial;
        }
    };

    // ItemInHandRenderer::tick 0x145315cc0 with the item's re-equip check 0x141a587f0:
    // a hotbar slot change lowers even onto the same item; a count change (placing) keeps the hand up
    inline void TickEquip(ui::HudItem& shown, int& shownSlot, float& height, const ui::HudItem& item, int slot,
                          bool sameItem) {
        const bool slotChanged = slot != shownSlot;
        const bool lower = !(shown.Empty() && item.Empty()) && (slotChanged || !sameItem);
        if (!lower && sameItem)
            shown = item;
        height += std::clamp((lower ? 0.0f : 1.0f) - height, -0.4f, 0.4f);
        // inclusive: the game swaps at exactly 0.1 too
        if (height <= 0.1f) {
            shown = item;
            shownSlot = slot;
        }
    }
}
