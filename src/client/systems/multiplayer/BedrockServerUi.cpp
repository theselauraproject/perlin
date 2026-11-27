#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/i18n/Lang.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace perlin {
    using namespace proto;

    bool BedrockSession::TakeServerForm(ServerForm& out) {
        std::lock_guard<std::mutex> lk(serverUiMutex);
        if (pendingForms.empty())
            return false;
        out = std::move(pendingForms.front());
        pendingForms.pop_front();
        return true;
    }

    bool BedrockSession::SendFormResponse(uint32_t id, const std::string* data, bool busy) {
        if (id >= 0xF0000000u) {
            SignEditRequest request;
            bool known = false;
            {
                std::lock_guard<std::mutex> lk(serverUiMutex);
                const auto it = signForms.find(id);
                if (it != signForms.end()) {
                    request = it->second;
                    signForms.erase(it);
                    known = true;
                }
            }
            if (!known || !data)
                return known;
            json::Value answers;
            std::string error;
            if (!json::Value::Parse(*data, answers, error) || !answers.IsArray())
                return false;
            std::string text;
            for (size_t i = 0; i < 4; i++) {
                if (i > 0)
                    text += '\n';
                if (i < answers.array.size() && answers.array[i].IsString())
                    text += answers.array[i].string;
            }
            return SendSignText(request.x, request.y, request.z, request.front, text);
        }
        ModalFormResponsePacket p;
        p.formId = id;
        if (data) {
            p.hasResponseData = true;
            p.data = *data;
            p.hasCancelReason = false;
        } else {
            p.hasResponseData = false;
            p.hasCancelReason = true;
            p.cancelReason = busy ? ModalFormResponsePacketCancelReason::Busy
                                  : ModalFormResponsePacketCancelReason::Closed;
        }
        return Send(p);
    }

    bool BedrockSession::SendServerSettingsRequest() {
        ServerSettingsRequestPacket p;
        return Send(p);
    }

    std::vector<BedrockSession::BossBar> BedrockSession::BossBars() const {
        std::lock_guard<std::mutex> lk(serverUiMutex);
        return bossBars;
    }

    bool BedrockSession::TakeToast(ServerToast& out) {
        std::lock_guard<std::mutex> lk(serverUiMutex);
        if (toasts.empty())
            return false;
        out = std::move(toasts.front());
        toasts.pop_front();
        return true;
    }

    bool BedrockSession::TakePickItemNotice(GuiDataState::Notice& out) {
        std::lock_guard lock(serverUiMutex);
        return guiData.TakeNotice(out);
    }

    GuiDataState::Flash BedrockSession::PickItemFlash() const {
        std::lock_guard lock(serverUiMutex);
        return guiData.GetFlash();
    }

    void BedrockSession::TickServerGui() {
        std::lock_guard lock(serverUiMutex);
        guiData.Tick();
    }

    void BedrockSession::ResetServerGui() {
        std::lock_guard lock(serverUiMutex);
        guiData.Clear();
        inventoryUiOptions = {};
        furnaceUiOptions = {};
    }

    BedrockSession::InventoryUiOptions BedrockSession::GetInventoryUiOptions() const {
        std::lock_guard lock(serverUiMutex);
        return inventoryUiOptions;
    }

    BedrockSession::FurnaceUiOptions BedrockSession::GetFurnaceUiOptions(int type) const {
        std::lock_guard lock(serverUiMutex);
        return type >= 1 && type <= 3 ? furnaceUiOptions[static_cast<size_t>(type - 1)]
                                    : FurnaceUiOptions{};
    }

    BedrockSession::ScoreboardView BedrockSession::Scoreboard() const {
        ScoreboardView view;
        std::lock_guard<std::mutex> lk(serverUiMutex);
        if (sidebarObjective.empty())
            return view;
        const auto obj = objectives.find(sidebarObjective);
        if (obj == objectives.end())
            return view;
        view.visible = true;
        view.title = obj->second.display;
        view.descending = obj->second.sortOrder == 1;
        for (const auto& [key, value] : scoreValues) {
            if (key.objective != sidebarObjective)
                continue;
            ScoreRow row;
            row.name = value.name;
            row.score = value.score;
            row.order = key.id;
            view.rows.push_back(std::move(row));
        }
        std::ranges::stable_sort(view.rows, [&](const ScoreRow& a, const ScoreRow& b) {
            if (a.score != b.score)
                return view.descending ? a.score > b.score : a.score < b.score;
            return a.order < b.order;
        });
        if (view.rows.size() > 15)
            view.rows.resize(15);
        return view;
    }

    void BedrockSession::RegisterServerUiHandlers() {
        On<PlayerLocationPacket>([this](const PlayerLocationPacket& p) {
            std::optional<glm::vec3> position;
            if (p.type == PlayerLocationPacketType::Coordinates) {
                if (!p.position)
                    return;
                if (!std::isfinite(p.position->x) || !std::isfinite(p.position->y) ||
                    !std::isfinite(p.position->z)) {
                    lastError = "Invalid PlayerLocation coordinates";
                    SetDisconnectInfo(static_cast<int>(DisconnectFailReason::BadPacket), lastError, false);
                    state = State::Failed;
                    return;
                }
                position = glm::vec3{p.position->x, p.position->y, p.position->z};
            } else if (p.type != PlayerLocationPacketType::TypeHide) {
                return;
            }
            std::lock_guard lock(locatorMutex);
            trackedPlayerLocations[p.entityUniqueId] = position;
        });

        On<GuiDataPickItemPacket>([this](const GuiDataPickItemPacket& p) {
            const std::string name = i18n::Tr(p.itemName);
            std::lock_guard lock(serverUiMutex);
            guiData.Pick(name, p.itemEffects, p.hotbarSlot);
        });

        On<SetPlayerInventoryOptionsPacket>([this](const SetPlayerInventoryOptionsPacket& p) {
            if (GetRuntimeId() == 0)
                return;
            std::lock_guard lock(serverUiMutex);
            inventoryUiOptions = {static_cast<int32_t>(p.leftTab), static_cast<int32_t>(p.rightTab),
                                  p.filtering, static_cast<int32_t>(p.layout),
                                  static_cast<int32_t>(p.craftingLayout), inventoryUiOptions.serial + 1};
        });

        On<SetPlayerFurnaceOptionsPacket>([this](const SetPlayerFurnaceOptionsPacket& p) {
            if (GetRuntimeId() == 0)
                return;
            const int type = static_cast<int>(p.furnaceType);
            if (type < 1 || type > 3)
                return;
            std::lock_guard lock(serverUiMutex);
            auto& options = furnaceUiOptions[static_cast<size_t>(type - 1)];
            options = {p.furnaceOptions.leftFurnaceTab, p.furnaceOptions.filtering,
                       p.furnaceOptions.layout, options.serial + 1};
        });

        On<ModalFormRequestPacket>([this](const ModalFormRequestPacket& p) {
            std::lock_guard<std::mutex> lk(serverUiMutex);
            pendingForms.push_back({p.formId, p.data});
            if (pendingForms.size() > 16)
                pendingForms.pop_front();
        });

        On<ClientboundCloseFormPacket>([this](const ClientboundCloseFormPacket&) {
            {
                std::lock_guard<std::mutex> lk(serverUiMutex);
                pendingForms.clear();
            }
            formClosed = true;
        });

        On<ServerSettingsResponsePacket>([this](const ServerSettingsResponsePacket& p) {
            std::lock_guard<std::mutex> lk(serverUiMutex);
            pendingForms.push_back({p.formId, p.data});
        });

        On<BossEventPacket>([this](const BossEventPacket& p) {
            std::lock_guard<std::mutex> lk(serverUiMutex);
            auto it =
                std::ranges::find_if(bossBars, [&](const BossBar& b) { return b.id == p.targetEntityId; });
            switch (p.type) {
            case BossEventPacketType::ShowBar: {
                BossBar bar;
                bar.id = p.targetEntityId;
                bar.title = p.title;
                bar.progress = std::clamp(p.progress, 0.0f, 1.0f);
                bar.color = static_cast<uint8_t>(p.color);
                bar.overlay = static_cast<uint8_t>(p.overlay);
                if (it != bossBars.end())
                    *it = bar;
                else
                    bossBars.push_back(bar);
                break;
            }
            case BossEventPacketType::HideBar:
                if (it != bossBars.end())
                    bossBars.erase(it);
                break;
            case BossEventPacketType::SetBarProgress:
                if (it != bossBars.end())
                    it->progress = std::clamp(p.progress, 0.0f, 1.0f);
                break;
            case BossEventPacketType::SetBarTitle:
                if (it != bossBars.end())
                    it->title = p.title;
                break;
            case BossEventPacketType::UpdateProperties:
                if (it != bossBars.end()) {
                    it->progress = std::clamp(p.progress, 0.0f, 1.0f);
                    it->color = static_cast<uint8_t>(p.color);
                    it->overlay = static_cast<uint8_t>(p.overlay);
                }
                break;
            case BossEventPacketType::Texture:
                if (it != bossBars.end()) {
                    it->color = static_cast<uint8_t>(p.color);
                    it->overlay = static_cast<uint8_t>(p.overlay);
                }
                break;
            default:
                break;
            }
        });

        On<SetDisplayObjectivePacket>([this](const SetDisplayObjectivePacket& p) {
            std::lock_guard<std::mutex> lk(serverUiMutex);
            Objective& o = objectives[p.objectiveName];
            o.name = p.objectiveName;
            o.display = p.displayName;
            o.sortOrder = p.sortOrder;
            if (p.displaySlot == "sidebar")
                sidebarObjective = p.objectiveName;
        });

        On<RemoveObjectivePacket>([this](const RemoveObjectivePacket& p) {
            std::lock_guard<std::mutex> lk(serverUiMutex);
            objectives.erase(p.objectiveName);
            if (sidebarObjective == p.objectiveName)
                sidebarObjective.clear();
            for (auto it = scoreValues.begin(); it != scoreValues.end();)
                it = it->first.objective == p.objectiveName ? scoreValues.erase(it) : std::next(it);
        });

        On<SetScoreboardIdentityPacket>([this](const SetScoreboardIdentityPacket& p) {
            std::lock_guard<std::mutex> lk(serverUiMutex);
            for (const auto& e : p.entries) {
                if (p.action == SetScoreboardIdentityPacketAction::RegisterIdentity && e.entityUniqueId)
                    scoreIdentities[e.scoreboardId] = *e.entityUniqueId;
                else
                    scoreIdentities.erase(e.scoreboardId);
            }
        });

        On<SetScorePacket>([this](const SetScorePacket& p) {
            const std::vector<TabPlayer> players = PlayerList();
            std::lock_guard<std::mutex> lk(serverUiMutex);
            for (const auto& e : p.entries) {
                const std::string objective = e.objectiveName ? *e.objectiveName : e.objectiveName2;
                ScoreKey key{objective, e.scoreboardId};
                if (e.entryType == SetScorePacketEntriesEntryEntryType::Remove) {
                    scoreValues.erase(key);
                    continue;
                }
                ScoreValue v;
                v.score = e.score;
                v.entity = e.entityUniqueId;
                if (e.entryType == SetScorePacketEntriesEntryEntryType::FakePlayer) {
                    v.name = e.customName;
                } else {
                    int64_t uid = e.entityUniqueId;
                    if (const auto id = scoreIdentities.find(e.scoreboardId); id != scoreIdentities.end())
                        uid = id->second;
                    for (const TabPlayer& tp : players)
                        if (tp.uid == uid) {
                            v.name = tp.name;
                            break;
                        }
                    if (v.name.empty())
                        v.name = e.customName;
                }
                scoreValues[key] = std::move(v);
            }
        });

        On<ToastRequestPacket>([this](const ToastRequestPacket& p) {
            std::lock_guard<std::mutex> lk(serverUiMutex);
            toasts.push_back({p.title, p.message});
            if (toasts.size() > 8)
                toasts.pop_front();
        });

        On<UpdateClientInputLocksPacket>(
            [this](const UpdateClientInputLocksPacket& p) { inputLocks = p.locks; });

        On<SetHealthPacket>([this](const SetHealthPacket& p) {
            std::lock_guard<std::mutex> lk(attrMutex);
            ownAttributes["minecraft:health"] = static_cast<float>(p.health);
        });

        On<SetHudPacket>([this](const SetHudPacket& p) {
            uint32_t mask = 0;
            if (p.visibility == SetHudPacketVisibility::Hide)
                for (const auto& e : p.elements)
                    mask |= 1u << (static_cast<uint32_t>(e) & 31);
            hiddenHud = mask;
        });
    }
}
