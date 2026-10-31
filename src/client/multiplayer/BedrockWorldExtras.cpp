#include "client/multiplayer/BedrockSession.hpp"
#include "client/multiplayer/NbtJson.hpp"

#include <algorithm>
#include <iostream>

namespace perlin {
    using namespace proto;

    namespace {
        void PutVarint(std::vector<uint8_t>& o, uint32_t v) {
            while (v >= 0x80) {
                o.push_back(static_cast<uint8_t>(v | 0x80));
                v >>= 7;
            }
            o.push_back(static_cast<uint8_t>(v));
        }

        void PutName(std::vector<uint8_t>& o, uint8_t type, const std::string& name) {
            o.push_back(type);
            PutVarint(o, static_cast<uint32_t>(name.size()));
            o.insert(o.end(), name.begin(), name.end());
        }

        void PutString(std::vector<uint8_t>& o, const std::string& k, const std::string& v) {
            PutName(o, 8, k);
            PutVarint(o, static_cast<uint32_t>(v.size()));
            o.insert(o.end(), v.begin(), v.end());
        }

        void PutInt(std::vector<uint8_t>& o, const std::string& k, int32_t v) {
            PutName(o, 3, k);
            PutVarint(o, (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31));
        }

        void PutByte(std::vector<uint8_t>& o, const std::string& k, uint8_t v) {
            PutName(o, 1, k);
            o.push_back(v);
        }

        std::vector<uint8_t> SignNbt(int x, int y, int z, bool front, const std::string& text, const BlockEntity* old) {
            std::vector<uint8_t> o;
            PutName(o, 10, "");
            PutString(o, "id", "Sign");
            PutInt(o, "x", x);
            PutInt(o, "y", y);
            PutInt(o, "z", z);
            PutByte(o, "isMovable", 1);
            PutByte(o, "IsWaxed", 0);
            auto face = [&](const char* key, const std::string& t, uint32_t color, bool glow) {
                PutName(o, 10, key);
                PutString(o, "Text", t);
                PutInt(o, "SignTextColor", static_cast<int32_t>(color));
                PutByte(o, "IgnoreLighting", glow ? 1 : 0);
                PutByte(o, "HideGlowOutline", 0);
                PutString(o, "TextOwner", "");
                PutByte(o, "PersistFormatting", 1);
                o.push_back(0);
            };
            const SignFace empty;
            const SignFace& f = old ? old->front : empty;
            const SignFace& b = old ? old->back : empty;
            if (front) {
                face("FrontText", text, f.color, f.glow);
                face("BackText", b.text, b.color, b.glow);
            } else {
                face("FrontText", f.text, f.color, f.glow);
                face("BackText", text, b.color, b.glow);
            }
            o.push_back(0);
            return o;
        }
    }

    bool BedrockSession::TakeSignRequest(SignEditRequest& out) {
        std::lock_guard<std::mutex> lk(worldExtraMutex);
        if (signRequests.empty())
            return false;
        out = signRequests.front();
        signRequests.pop_front();
        return true;
    }

    bool BedrockSession::SendSignText(int x, int y, int z, bool front, const std::string& text) {
        const BlockEntity* old = nullptr;
        BlockEntity copy;
        if (blockEntities.Get(x, y, z, copy)) {
            old = &copy;
        }
        BlockEntityDataPacket p;
        p.position = {x, y, z};
        p.nbt.bytes = SignNbt(x, y, z, front, text, old);
        return Send(p);
    }

    int BedrockSession::WindowProperty(int window, int property, int fallback) const {
        std::lock_guard<std::mutex> lk(worldExtraMutex);
        auto w = windowData.find(window);
        if (w == windowData.end())
            return fallback;
        auto v = w->second.find(property);
        return v == w->second.end() ? fallback : v->second;
    }

    bool BedrockSession::TakeNpcDialogue(NpcDialogueView& out) {
        std::lock_guard<std::mutex> lk(worldExtraMutex);
        if (npcDialogues.empty())
            return false;
        out = std::move(npcDialogues.front());
        npcDialogues.pop_front();
        return true;
    }

    bool BedrockSession::TakeTrade(TradeView& out) {
        std::lock_guard<std::mutex> lk(worldExtraMutex);
        if (!pendingTrade)
            return false;
        out = std::move(*pendingTrade);
        pendingTrade.reset();
        return true;
    }

    std::optional<BedrockSession::MapView> BedrockSession::Map(int64_t id) const {
        std::lock_guard<std::mutex> lk(worldExtraMutex);
        auto it = maps.find(id);
        if (it == maps.end())
            return std::nullopt;
        return it->second;
    }

    void BedrockSession::ParsePropertySchema(const std::vector<uint8_t>& bytes) {
        if (bytes.empty())
            return;
        ui::Json root;
        if (!NetworkNbtToJson(bytes, root))
            return;
        const ui::Json* type = root.Find("type");
        const ui::Json* list = root.Find("properties");
        if (!type || !type->IsString() || !list || !list->IsArray())
            return;
        std::vector<PropertyDef> defs;
        for (const ui::Json& entry : list->array) {
            PropertyDef d;
            if (const ui::Json* n = entry.Find("name"))
                d.name = n->StringOr("");
            if (const ui::Json* t = entry.Find("type"))
                d.type = static_cast<int>(t->NumberOr(0));
            if (const ui::Json* v = entry.Find("enum"); v && v->IsArray())
                for (const ui::Json& item : v->array)
                    d.values.push_back(item.StringOr(""));
            defs.push_back(std::move(d));
        }
        std::lock_guard<std::mutex> lk(propertyMutex);
        propertySchemas[type->string] = std::move(defs);
    }

    void BedrockSession::ApplyEntityProperties(uint64_t rid, const std::string& type, const EntityProperties& props) {
        if (props.ints.empty() && props.floats.empty())
            return;
        std::lock_guard<std::mutex> lk(propertyMutex);
        const auto schema = propertySchemas.find(type);
        if (schema == propertySchemas.end())
            return;
        const std::vector<PropertyDef>& defs = schema->second;
        for (const EntityPropertiesIntsEntry& i : props.ints) {
            if (i.index >= defs.size() || defs[i.index].name.empty())
                continue;
            const PropertyDef& d = defs[i.index];
            entities.SetProperty(rid, d.name, static_cast<float>(i.value));
            if (d.type == 3 && i.value >= 0 && static_cast<size_t>(i.value) < d.values.size())
                entities.SetPropertyText(rid, d.name, d.values[static_cast<size_t>(i.value)]);
        }
        for (const EntityPropertiesFloatsEntry& f : props.floats)
            if (f.index < defs.size() && !defs[f.index].name.empty())
                entities.SetProperty(rid, defs[f.index].name, f.value);
    }

    void BedrockSession::RegisterWorldExtraHandlers() {
        On<SyncEntityPropertyPacket>([this](const SyncEntityPropertyPacket& p) {
            ParsePropertySchema(p.nbt.bytes);
        });

        On<ChangeMobPropertyPacket>([this](const ChangeMobPropertyPacket& p) {
            entity::Entity e;
            if (p.property.empty() || !entities.FindUnique(p.entityUniqueId, e))
                return;
            if (!p.stringValue.empty()) {
                entities.SetPropertyText(e.runtimeId, p.property, p.stringValue);
                return;
            }
            entities.SetProperty(e.runtimeId, p.property, p.floatValue != 0.0f ? p.floatValue : p.boolValue ? 1.0f : static_cast<float>(p.intValue));
        });

        On<BlockEntityDataPacket>([this](const BlockEntityDataPacket& p) {
            ui::Json json;
            if (NetworkNbtToJson(p.nbt.bytes, json))
                blockEntities.Set(p.position.x, p.position.y, p.position.z, json);
        });

        On<OpenSignPacket>([this](const OpenSignPacket& p) {
            BlockEntity existing;
            std::string text;
            if (blockEntities.Get(p.position.x, p.position.y, p.position.z, existing))
                text = p.isFront ? existing.front.text : existing.back.text;
            std::vector<std::string> lines(4);
            size_t from = 0;
            for (size_t i = 0; i < 4 && from <= text.size(); i++) {
                const size_t end = text.find('\n', from);
                lines[i] = text.substr(from, end == std::string::npos ? std::string::npos : end - from);
                if (end == std::string::npos)
                    break;
                from = end + 1;
            }
            ui::Json form = ui::Json::MakeObject();
            form.Set("type", ui::Json("custom_form"));
            form.Set("title", ui::Json(p.isFront ? "Edit Sign" : "Edit Sign (Back)"));
            ui::Json content = ui::Json::MakeArray();
            for (size_t i = 0; i < 4; i++) {
                ui::Json input = ui::Json::MakeObject();
                input.Set("type", ui::Json("input"));
                input.Set("text", ui::Json("Line " + std::to_string(i + 1)));
                input.Set("placeholder", ui::Json(""));
                input.Set("default", ui::Json(lines[i]));
                content.Push(std::move(input));
            }
            form.Set("content", std::move(content));
            const uint32_t id = 0xF0000000u + signFormSerial++;
            std::lock_guard<std::mutex> lk(serverUiMutex);
            signForms[id] = {p.position.x, p.position.y, p.position.z, p.isFront};
            pendingForms.push_back({id, form.Dump()});
            if (pendingForms.size() > 16)
                pendingForms.pop_front();
        });

        On<TickSyncPacket>([this](const TickSyncPacket& p) {
            TickSyncPacket reply;
            reply.requestTime = p.requestTime;
            reply.responseTime = static_cast<int64_t>(serverTick);
            Send(reply);
        });

        On<ContainerSetDataPacket>([this](const ContainerSetDataPacket& p) {
            std::lock_guard<std::mutex> lk(worldExtraMutex);
            windowData[static_cast<int>(p.windowId)][p.property] = p.value;
        });

        On<ClientboundMapItemDataPacket>([this](const ClientboundMapItemDataPacket& p) {
            std::lock_guard<std::mutex> lk(worldExtraMutex);
            MapView& m = maps[p.mapId];
            m.locked = p.locked;
            if (p.width && p.height && p.pixels) {
                const int w = *p.width, h = *p.height;
                const int ox = p.xOffset ? *p.xOffset : 0, oy = p.yOffset ? *p.yOffset : 0;
                if (w > 0 && h > 0 && static_cast<size_t>(w) * h <= p.pixels->size()) {
                    if (m.pixels.empty())
                        m.pixels.assign(128 * 128, 0);
                    for (int y = 0; y < h; y++) {
                        for (int x = 0; x < w; x++) {
                            const int tx = ox + x, ty = oy + y;
                            if (tx < 0 || ty < 0 || tx >= m.width || ty >= m.height)
                                continue;
                            m.pixels[static_cast<size_t>(ty) * m.width + tx] = static_cast<uint32_t>((*p.pixels)[static_cast<size_t>(y) * w + x]);
                        }
                    }
                    m.revision++;
                }
            }
        });

        On<NpcDialoguePacket>([this](const NpcDialoguePacket& p) {
            std::lock_guard<std::mutex> lk(worldExtraMutex);
            NpcDialogueView v;
            v.entityId = p.entityId;
            v.dialogue = p.dialogue;
            v.screenName = p.screenName;
            v.npcName = p.npcName;
            v.actionJson = p.actionJson;
            v.open = static_cast<int>(p.actionType) == 0;
            npcDialogues.push_back(std::move(v));
        });

        On<UpdateTradePacket>([this](const UpdateTradePacket& p) {
            std::lock_guard<std::mutex> lk(worldExtraMutex);
            TradeView t;
            t.windowId = static_cast<int>(p.windowId);
            t.title = p.displayName;
            t.offers = p.offers.bytes;
            t.villagerUid = static_cast<int64_t>(p.villagerUniqueId);
            t.newUi = p.newTradingUi;
            pendingTrade = std::move(t);
        });

        On<CameraShakePacket>([this](const CameraShakePacket& p) {
            if (p.action == CameraShakePacketAction::Stop)
                cameraEffects.StopShake();
            else
                cameraEffects.Shake(p.intensity, p.duration, p.type);
        });

        On<PlayerFogPacket>([this](const PlayerFogPacket& p) {
            cameraEffects.SetFog(p.stack);
        });

        On<CameraPresetsPacket>([this](const CameraPresetsPacket& p) {
            std::lock_guard<std::mutex> lk(worldExtraMutex);
            cameraPresets.clear();
            for (const auto& c : p.presets) {
                CameraPreset r;
                r.name = c.name;
                r.parent = c.parent;
                if (c.position.x && c.position.y && c.position.z)
                    r.position = glm::vec3(*c.position.x, *c.position.y, *c.position.z);
                if (c.rotation.x && c.rotation.y)
                    r.rotation = glm::vec2(*c.rotation.x, *c.rotation.y);
                if (c.offset)
                    r.offset = glm::vec2(c.offset->x, c.offset->z);
                if (c.entityOffset)
                    r.entityOffset = glm::vec3(c.entityOffset->x, c.entityOffset->y, c.entityOffset->z);
                cameraPresets.push_back(std::move(r));
            }
        });

        On<CameraInstructionPacket>([this](const CameraInstructionPacket& p) {
            if (p.clear && *p.clear)
                cameraEffects.Clear();
            if (p.fade && p.fade->timeData) {
                const auto& t = *p.fade->timeData;
                const Vec3F c = p.fade->colorRgb ? *p.fade->colorRgb : Vec3F{};
                cameraEffects.Fade(t.fadeInDuration, t.waitDuration, t.fadeOutDuration, {c.x, c.y, c.z});
            }
            if (p.fov) {
                static const char* const kEases[] = {
                    "linear", "spring", "in_quad", "out_quad", "in_out_quad", "in_cubic", "out_cubic", "in_out_cubic",
                    "in_quart", "out_quart", "in_out_quart", "in_quint", "out_quint", "in_out_quint", "in_sine",
                    "out_sine", "in_out_sine", "in_expo", "out_expo", "in_out_expo", "in_circ", "out_circ", "in_out_circ",
                    "in_bounce", "out_bounce", "in_out_bounce", "in_back", "out_back", "in_out_back", "in_elastic",
                    "out_elastic", "in_out_elastic", "inverse_lerp"};
                int ease = 0;
                for (int i = 0; i < static_cast<int>(sizeof(kEases) / sizeof(kEases[0])); i++)
                    if (p.fov->easeType == kEases[i])
                        ease = i;
                cameraEffects.SetFov(p.fov->fieldOfView, p.fov->easeTime, ease, p.fov->clear);
            }
            if (p.target) {
                std::optional<glm::vec3> off;
                if (p.target->offset)
                    off = glm::vec3(p.target->offset->x, p.target->offset->y, p.target->offset->z);
                cameraEffects.Target(p.target->entityUniqueId, off);
            }
            if (p.removeTarget && *p.removeTarget)
                cameraEffects.RemoveTarget();
            if (p.attachToEntity)
                cameraEffects.Attach(*p.attachToEntity);
            if (p.detachFromEntity && *p.detachFromEntity)
                cameraEffects.Detach();
            if (p.instructionSet) {
                const auto& s = *p.instructionSet;
                CameraSet set;
                {
                    std::lock_guard<std::mutex> lk(worldExtraMutex);
                    std::string name;
                    int guard = 0;
                    if (s.runtimeId >= 0 && static_cast<size_t>(s.runtimeId) < cameraPresets.size()) {
                        const CameraPreset* cur = &cameraPresets[static_cast<size_t>(s.runtimeId)];
                        while (cur && guard++ < 8) {
                            if (!set.position && cur->position)
                                set.position = cur->position;
                            if (!set.rotation && cur->rotation)
                                set.rotation = cur->rotation;
                            if (!set.offset && cur->offset)
                                set.offset = cur->offset;
                            if (!set.entityOffset && cur->entityOffset)
                                set.entityOffset = cur->entityOffset;
                            const CameraPreset* next = nullptr;
                            if (!cur->parent.empty()) {
                                for (const auto& c : cameraPresets) {
                                    if (c.name == cur->parent) {
                                        next = &c;
                                        break;
                                    }
                                }
                            }
                            cur = next;
                        }
                    }
                }
                if (s.position)
                    set.position = glm::vec3(s.position->x, s.position->y, s.position->z);
                if (s.rotation)
                    set.rotation = glm::vec2(s.rotation->x, s.rotation->z);
                if (s.facing)
                    set.facing = glm::vec3(s.facing->x, s.facing->y, s.facing->z);
                if (s.offset)
                    set.offset = glm::vec2(s.offset->x, s.offset->z);
                if (s.entityOffset)
                    set.entityOffset = glm::vec3(s.entityOffset->x, s.entityOffset->y, s.entityOffset->z);
                if (s.easeData) {
                    set.hasEase = true;
                    set.easeType = static_cast<int>(s.easeData->type);
                    set.easeDuration = s.easeData->duration;
                }
                cameraEffects.Set(set);
            }
        });
    }

    namespace {
        constexpr const char* kLegacySounds[] = {"ItemUseOn", "Hit", "Step", "Fly", "Jump", "Break", "Place", "HeavyStep", "Gallop", "Fall", "Ambient", "AmbientBaby", "AmbientInWater", "Breathe", "Death", "DeathInWater", "DeathToZombie", "Hurt", "HurtInWater", "Mad", "Boost", "Bow", "SquishBig", "SquishSmall", "FallBig", "FallSmall", "Splash", "Fizz", "Flap", "Swim", "Drink", "Eat", "Takeoff", "Shake", "Plop", "Land", "Saddle", "Armor", "ArmorStandPlace", "AddChest", "Throw", "Attack", "AttackNoDamage", "AttackStrong", "Warn", "Shear", "Milk", "Thunder", "Explode", "Fire", "Ignite", "Fuse", "Stare", "Spawn", "Shoot", "BreakBlock", "Launch", "Blast", "LargeBlast", "Twinkle", "Remedy", "Unfect", "LevelUp", "BowHit", "BulletHit", "ExtinguishFire", "ItemFizz", "ChestOpen", "ChestClosed", "ShulkerBoxOpen", "ShulkerBoxClosed", "EnderChestOpen", "EnderChestClosed", "PowerOn", "PowerOff", "Attach", "Detach", "Deny", "Tripod", "Pop", "DropSlot", "Note", "Thorns", "PistonIn", "PistonOut", "Portal", "Water", "LavaPop", "Lava", "Burp", "BucketFillWater", "BucketFillLava", "BucketEmptyWater", "BucketEmptyLava", "ArmorEquipChain", "ArmorEquipDiamond", "ArmorEquipGeneric", "ArmorEquipGold", "ArmorEquipIron", "ArmorEquipLeather", "ArmorEquipElytra", "Record13", "RecordCat", "RecordBlocks", "RecordChirp", "RecordFar", "RecordMall", "RecordMellohi", "RecordStal", "RecordStrad", "RecordWard", "Record11", "RecordWait", "StopRecord", "Flop", "GuardianCurse", "MobWarning", "MobWarningBaby", "Teleport", "ShulkerOpen", "ShulkerClose", "Haggle", "HaggleYes", "HaggleNo", "HaggleIdle", "ChorusGrow", "ChorusDeath", "Glass", "PotionBrewed", "CastSpell", "PrepareAttackSpell", "PrepareSummon", "PrepareWololo", "Fang", "Charge", "CameraTakePicture", "LeashKnotPlace", "LeashKnotBreak", "AmbientGrowl", "AmbientWhine", "AmbientPant", "AmbientPurr", "AmbientPurreow", "DeathMinVolume", "DeathMidVolume", "ImitateBlaze", "ImitateCaveSpider", "ImitateCreeper", "ImitateElderGuardian", "ImitateEnderDragon", "ImitateEnderman", "ImitateEndermite", "ImitateEvocationIllager", "ImitateGhast", "ImitateHusk", "ImitateIllusionIllager", "ImitateMagmaCube", "ImitatePolarBear", "ImitateShulker", "ImitateSilverfish", "ImitateSkeleton", "ImitateSlime", "ImitateSpider", "ImitateStray", "ImitateVex", "ImitateVindicationIllager", "ImitateWitch", "ImitateWither", "ImitateWitherSkeleton", "ImitateWolf", "ImitateZombie", "ImitateZombiePigman", "ImitateZombieVillager", "EnderEyePlaced", "EndPortalCreated", "AnvilUse", "BottleDragonBreath", "PortalTravel", "TridentHit", "TridentReturn", "TridentRiptide1", "TridentRiptide2", "TridentRiptide3", "TridentThrow", "TridentThunder", "TridentHitGround", "Default", "FletchingTableUse", "ElemConstructOpen", "IceBombHit", "BalloonPop", "LtReactionIceBomb", "LtReactionBleach", "LtReactionElephantToothpaste", "LtReactionElephantToothpaste2", "LtReactionGlowStick", "LtReactionGlowStick2", "LtReactionLuminol", "LtReactionSalt", "LtReactionFertilizer", "LtReactionFireball", "LtReactionMagnesiumSalt", "LtReactionMiscFire", "LtReactionFire", "LtReactionMiscExplosion", "LtReactionMiscMystical", "LtReactionMiscMystical2", "LtReactionProduct", "SparklerUse", "GlowStickUse", "SparklerActive", "ConvertToDrowned", "BucketFillFish", "BucketEmptyFish", "BubbleColumnUpwards", "BubbleColumnDownwards", "BubblePop", "BubbleUpInside", "BubbleDownInside", "HurtBaby", "DeathBaby", "StepBaby", "SpawnBaby", "Born", "TurtleEggBreak", "TurtleEggCrack", "TurtleEggHatched", "LayEgg", "TurtleEggAttacked", "BeaconActivate", "BeaconAmbient", "BeaconDeactivate", "BeaconPower", "ConduitActivate", "ConduitAmbient", "ConduitAttack", "ConduitDeactivate", "ConduitShort", "Swoop", "BambooSaplingPlace", "PreSneeze", "Sneeze", "AmbientTame", "Scared", "ScaffoldingClimb", "CrossbowLoadingStart", "CrossbowLoadingMiddle", "CrossbowLoadingEnd", "CrossbowShoot", "CrossbowQuickChargeStart", "CrossbowQuickChargeMiddle", "CrossbowQuickChargeEnd", "AmbientAggressive", "AmbientWorried", "CantBreed", "ShieldBlock", "LecternBookPlace", "GrindstoneUse", "Bell", "CampfireCrackle", "Roar", "Stun", "SweetBerryBushHurt", "SweetBerryBushPick", "CartographyTableUse", "StonecutterUse", "ComposterEmpty", "ComposterFill", "ComposterFillLayer", "ComposterReady", "BarrelOpen", "BarrelClose", "RaidHorn", "LoomUse", "AmbientInRaid", "UicartographyTableUse", "UistonecutterUse", "UiloomUse", "SmokerUse", "BlastFurnaceUse", "SmithingTableUse", "Screech", "Sleep", "FurnaceUse", "MooshroomConvert", "MilkSuspiciously", "Celebrate", "JumpPrevent", "AmbientPollinate", "BeehiveDrip", "BeehiveEnter", "BeehiveExit", "BeehiveWork", "BeehiveShear", "HoneybottleDrink", "AmbientCave", "Retreat", "ConvertToZombified", "Admire", "StepLava", "Tempt", "Panic", "Angry", "AmbientMoodWarpedForest", "AmbientMoodSoulsandValley", "AmbientMoodNetherWastes", "AmbientMoodBasaltDeltas", "AmbientMoodCrimsonForest", "RespawnAnchorCharge", "RespawnAnchorDeplete", "RespawnAnchorSetSpawn", "RespawnAnchorAmbient", "SoulEscapeQuiet", "SoulEscapeLoud", "RecordPigstep", "LinkCompassToLodestone", "UseSmithingTable", "EquipNetherite", "AmbientLoopWarpedForest", "AmbientLoopSoulsandValley", "AmbientLoopNetherWastes", "AmbientLoopBasaltDeltas", "AmbientLoopCrimsonForest", "AmbientAdditionWarpedForest", "AmbientAdditionSoulsandValley", "AmbientAdditionNetherWastes", "AmbientAdditionBasaltDeltas", "AmbientAdditionCrimsonForest", "SculkSensorPowerOn", "SculkSensorPowerOff", "BucketFillPowderSnow", "BucketEmptyPowderSnow", "PointedDripstoneCauldronDripWater", "PointedDripstoneCauldronDripLava", "PointedDripstoneDripWater", "PointedDripstoneDripLava", "CaveVinesPickBerries", "BigDripleafTiltDown", "BigDripleafTiltUp", "CopperWaxOn", "CopperWaxOff", "Scrape", "PlayerHurtDrown", "PlayerHurtOnFire", "PlayerHurtFreeze", "UseSpyglass", "StopUsingSpyglass", "AmethystBlockChime", "AmbientScreamer", "HurtScreamer", "DeathScreamer", "MilkScreamer", "JumpToBlock", "PreRam", "PreRamScreamer", "RamImpact", "RamImpactScreamer", "SquidInkSquirt", "GlowSquidInkSquirt", "ConvertToStray", "CakeAddCandle", "ExtinguishCandle", "AmbientCandle", "BlockClick", "BlockClickFail", "SculkCatalystBloom", "SculkShriekerShriek", "WardenNearbyClose", "WardenNearbyCloser", "WardenNearbyClosest", "WardenSlightlyAngry", "RecordOtherside", "Tongue", "CrackIronGolem", "RepairIronGolem", "Listening", "Heartbeat", "HornBreak", "_", "SculkSpread", "SculkCharge", "SculkSensorPlace", "SculkShriekerPlace", "GoatCall0", "GoatCall1", "GoatCall2", "GoatCall3", "GoatCall4", "GoatCall5", "GoatCall6", "GoatCall7", "GoatCall8", "GoatCall9", "GoatHarmony0", "GoatHarmony1", "GoatHarmony2", "GoatHarmony3", "GoatHarmony4", "GoatHarmony5", "GoatHarmony6", "GoatHarmony7", "GoatHarmony8", "GoatHarmony9", "GoatMelody0", "GoatMelody1", "GoatMelody2", "GoatMelody3", "GoatMelody4", "GoatMelody5", "GoatMelody6", "GoatMelody7", "GoatMelody8", "GoatMelody9", "GoatBass0", "GoatBass1", "GoatBass2", "GoatBass3", "GoatBass4", "GoatBass5", "GoatBass6", "GoatBass7", "GoatBass8", "GoatBass9", "_", "_", "_", "ImitateWarden", "ListeningAngry", "ItemGiven", "ItemTaken", "Disappeared", "Reappeared", "DrinkMilk", "FrogspawnHatched", "LaySpawn", "FrogspawnBreak", "SonicBoom", "SonicCharge", "SoundeventItemThrown", "Record5", "ConvertToFrog", "RecordPlaying", "EnchantingTableUse", "StepSand", "DashReady", "BundleDropContents", "BundleInsert", "BundleRemoveOne", "PressurePlateClickOff", "PressurePlateClickOn", "ButtonClickOff", "ButtonClickOn", "DoorOpen", "DoorClose", "TrapdoorOpen", "TrapdoorClose", "FenceGateOpen", "FenceGateClose", "Insert", "Pickup", "InsertEnchanted", "PickupEnchanted", "Brush", "BrushCompleted", "ShatterDecoratedPot", "BreakDecoratedPot", "SnifferEggCrack", "SnifferEggHatched", "WaxedSignInteractFail", "RecordRelic", "Bump", "PumpkinCarve", "ConvertHuskToZombie", "PigDeath", "HoglinZombified", "AmbientUnderwaterEnter", "AmbientUnderwaterExit", "BottleFill", "BottleEmpty", "CrafterCraft", "CrafterFail", "DecoratedPotInsert", "DecoratedPotInsertFail", "CrafterDisableSlot", "TrialSpawnerOpenShutter", "TrialSpawnerEjectItem", "TrialSpawnerDetectPlayer", "TrialSpawnerSpawnMob", "TrialSpawnerCloseShutter", "TrialSpawnerAmbient", "CopperBulbTurnOn", "CopperBulbTurnOff", "AmbientInAir", "BreezeWindChargeBurst", "ImitateBreeze", "ArmadilloBrush", "ArmadilloScuteDrop", "EquipWolf", "UnequipWolf", "Reflect", "VaultOpenShutter", "VaultCloseShutter", "VaultEjectItem", "VaultInsertItem", "VaultInsertItemFail", "VaultAmbient", "VaultActivate", "VaultDeactive", "HurtReduced", "WindChargeBurst", "ImitateBogged", "WolfArmourCrack", "WolfArmourBreak", "WolfArmourRepair", "MaceSmashAir", "MaceSmashGround", "TrialSpawnerChargeActivate", "TrialSpawnerAmbientOminous", "OminiousItemSpawnerSpawnItem", "OminousBottleEndUse", "MaceHeavySmashGround", "OminousItemSpawnerSpawnItemBegin", "_", "ApplyEffectBadOmen", "ApplyEffectRaidOmen", "ApplyEffectTrialOmen", "OminousItemSpawnerAboutToSpawnItem", "RecordCreator", "RecordCreatorMusicBox", "RecordPrecipice", "VaultRejectRewardedPlayer", "ImitateDrowned", "ImitateCreaking", "BundleInsertFailed", "SpongeAbsorb", "_", "BlockCreakingHeartTrail", "CreakingHeartSpawn", "Activate", "Deactivate", "Freeze", "Unfreeze", "Open", "OpenLong", "Close", "CloseLong", "ImitatePhantom", "ImitateZoglin", "ImitateGuardian", "ImitateRavager", "ImitatePillager", "PlaceInWater", "StateChange", "ImitateHappyGhast", "UniqueGeneric", "RecordTears", "TheEndLightFlash", "LeadLeash", "LeadUnleash", "LeadBreak", "Unsaddle", "EquipCopper", "RecordLavaChicken", "PlaceItem", "SingleItemSwap", "MultiItemSwap", "ItemEnchantLunge1", "ItemEnchantLunge2", "ItemEnchantLunge3", "AttackCritical", "ItemSpearAttackHit", "ItemSpearAttackMiss", "ItemWoodenSpearAttackHit", "ItemWoodenSpearAttackMiss", "ImitateParched", "ImitateCamelHusk", "ItemSpearUse", "ItemWoodenSpearUse", "SaddleInWater", "ItemStoneSpearAttackHit", "ItemIronSpearAttackHit", "ItemCopperSpearAttackHit", "ItemGoldenSpearAttackHit", "ItemDiamondSpearAttackHit", "ItemNetheriteSpearAttackHit", "ItemStoneSpearAttackMiss", "ItemIronSpearAttackMiss", "ItemCopperSpearAttackMiss", "ItemGoldenSpearAttackMiss", "ItemDiamondSpearAttackMiss", "ItemNetheriteSpearAttackMiss", "ItemStoneSpearUse", "ItemIronSpearUse", "ItemCopperSpearUse", "ItemGoldenSpearUse", "ItemDiamondSpearUse", "ItemNetheriteSpearUse", "PauseGrowth", "ResetGrowth", "PushedByPlayer", "Bounce", "SlimeLanding", "AbsorbBlock", "EjectBlock", "GeyserEruptionStart", "GeyserEruptionActive", "RecordBounce", "BucketFillLandAnimal", "BucketEmptyLandAnimal", "GeyserContinuousEruptionStart", "GeyserContinuousEruptionActive", "Mount", "Dismount", "StrawBedBreakLeave", "Undefined"};

        std::string LegacySoundName(uint32_t id) {
            return id < sizeof(kLegacySounds) / sizeof(kLegacySounds[0]) ? kLegacySounds[id] : std::string();
        }
    }

    void BedrockSession::RegisterProtocolExtras() {
        On<LevelSoundEventOldPacket>([this](const LevelSoundEventOldPacket& p) {
            const std::string name = LegacySoundName(p.soundId);
            if (name.empty())
                return;
            SoundRequest r;
            r.kind = SoundRequest::Kind::Level;
            r.name = name;
            r.x = p.position.x;
            r.y = p.position.y;
            r.z = p.position.z;
            r.extra = p.blockId;
            r.baby = p.isBabyMob;
            r.global = p.isGlobal;
            PushSound(std::move(r));
        });

        On<LevelSoundEventV2Packet>([this](const LevelSoundEventV2Packet& p) {
            const std::string name = LegacySoundName(p.soundId);
            if (name.empty())
                return;
            SoundRequest r;
            r.kind = SoundRequest::Kind::Level;
            r.name = name;
            r.x = p.position.x;
            r.y = p.position.y;
            r.z = p.position.z;
            r.extra = p.blockId;
            r.entityType = p.entityType;
            r.baby = p.isBabyMob;
            r.global = p.isGlobal;
            PushSound(std::move(r));
        });

        On<UpdateSoftEnumPacket>([this](const UpdateSoftEnumPacket& p) {
            std::lock_guard lock(commandMutex);
            for (ui::CommandInfo& command : commands)
                for (auto& overload : command.overloads)
                    for (ui::CommandParam& param : overload) {
                        if (param.softEnum != p.enumType)
                            continue;
                        if (p.actionType == UpdateSoftEnumPacketActionType::Update) {
                            param.values = p.options;
                        } else if (p.actionType == UpdateSoftEnumPacketActionType::Add) {
                            for (const std::string& v : p.options)
                                if (std::find(param.values.begin(), param.values.end(), v) == param.values.end())
                                    param.values.push_back(v);
                        } else {
                            for (const std::string& v : p.options)
                                param.values.erase(std::remove(param.values.begin(), param.values.end(), v), param.values.end());
                        }
                    }
            commandVersion++;
        });

        On<BlockEventPacket>([this](const BlockEventPacket& p) {
            if (p.type == BlockEventPacketType::ChangeState)
                blockEntities.SetLidOpen(p.position.x, p.position.y, p.position.z, p.data > 0);
        });
        On<MotionPredictionHintsPacket>([this](const MotionPredictionHintsPacket& p) {
            if (runtimeId == 0 || p.entityRuntimeId != runtimeId)
                return;
            // the game's handler (ClientNetworkHandler 0x146BD64E0) only looks the actor up: no velocity change
            NoteKnockback(p.velocity.x, p.velocity.z);
        });
    }

    void BedrockSession::RegisterIgnoredHandlers() {
        auto accept = [this]<class... T>() { (On<T>([](const T&) {}), ...); };
        accept.template operator()<AnimateEntityPacket,
                EmotePacket, HurtArmorPacket, PlayerArmorDamagePacket, LevelEventGenericPacket,
                PlayerLocationPacket, ServerPostMovePacket, SetLastHurtByPacket,
                SimulationTypePacket, SpawnExperienceOrbPacket, UpdateBlockPropertiesPacket, UpdateEquipmentPacket,
                ClientboundControlsSchemePacket, ClientboundTextureShiftPacket,
                ClientboundUpdateSoundDataPacket,
                SetMovementAuthorityPacket, ScriptMessagePacket, GraphicsOverrideParameterPacket,
                PrimitiveShapesPacket, ServerScriptDebugDrawerPacket, ClientboundAttributeLayerSyncPacket,
                CameraSplinePacket, CameraAimAssistPacket, CameraAimAssistPresetsPacket, CameraAimAssistActorPriorityPacket,
                ClientboundDataStorePacket, ClientboundDataDrivenUiShowScreenPacket, ClientboundDataDrivenUiCloseScreenPacket,
                ClientboundDataDrivenUiReloadPacket>();
    }
}
