#include <algorithm>
#include <chrono>
#include <cmath>
#include "client/entity/EntityTracker.hpp"

namespace perlin::entity {
    namespace {
        float SignedPitch(float degrees) {
            if (!std::isfinite(degrees))
                return 0.0f;
            degrees = std::fmod(degrees + 180.0f, 360.0f);
            if (degrees < 0.0f)
                degrees += 360.0f;
            return std::clamp(degrees - 180.0f, -90.0f, 90.0f);
        }
    }

    void EntityTracker::Clear() {
        std::lock_guard lock(mutex);
        entities.clear();
        byUnique.clear();
    }

    void EntityTracker::AttachSkinLocked(Entity& e) {
        if (!e.isPlayer)
            return;
        const auto it = skins.find(e.uuid);
        if (it != skins.end())
            e.skin = it->second;
    }

    void EntityTracker::AddPlayer(uint64_t rid, int64_t uid, const std::string& uuid, const std::string& name,
                                  glm::dvec3 feetPos, float pitch, float yaw, float headYaw, uint64_t flags,
                                  const std::string& mainHand) {
        std::lock_guard lock(mutex);
        Entity& e = entities[rid];
        const bool fresh = e.runtimeId == 0;
        e.runtimeId = rid;
        e.uniqueId = uid;
        e.type = "minecraft:player";
        e.isPlayer = true;
        e.uuid = uuid;
        e.name = name;
        e.pos = feetPos;
        e.pitch = SignedPitch(pitch);
        e.yaw = yaw;
        e.headYaw = headYaw;
        e.prevPos = e.pos;
        e.prevPitch = e.pitch;
        e.prevYaw = e.yaw;
        e.prevHeadYaw = e.headYaw;
        e.lerp.steps = 0;
        e.hasQueued = false;
        e.haveNet = false;
        e.flags = flags;
        e.mainHand = mainHand;
        e.isSelf = rid == selfRuntime && rid != 0;
        if (fresh)
            e.moveSerial = 1;
        byUnique[uid] = rid;
        AttachSkinLocked(e);
    }

    void EntityTracker::AddEntity(uint64_t rid, int64_t uid, const std::string& type, glm::dvec3 feetPos,
                                  float pitch, float yaw, float headYaw, uint64_t flags, float scale,
                                  int variant, int markVariant, int color) {
        std::lock_guard lock(mutex);
        Entity& e = entities[rid];
        e.runtimeId = rid;
        e.uniqueId = uid;
        e.type = type;
        e.isPlayer = type == "minecraft:player";
        e.pos = feetPos;
        e.pitch = SignedPitch(pitch);
        e.yaw = yaw;
        e.headYaw = headYaw;
        e.prevPos = e.pos;
        e.prevPitch = e.pitch;
        e.prevYaw = e.yaw;
        e.prevHeadYaw = e.headYaw;
        e.lerp.steps = 0;
        e.hasQueued = false;
        e.haveNet = false;
        e.flags = flags;
        e.scale = scale;
        e.variant = variant;
        e.markVariant = markVariant;
        e.color = color;
        e.moveSerial = 1;
        byUnique[uid] = rid;
    }

    void EntityTracker::AddItem(uint64_t rid, int64_t uid, glm::dvec3 pos, const std::string& name, int count,
                                int aux, uint32_t blockRuntimeId, bool enchanted) {
        std::lock_guard lock(mutex);
        Entity& e = entities[rid];
        e.runtimeId = rid;
        e.uniqueId = uid;
        e.type = "minecraft:item";
        e.isItem = true;
        e.pos = pos;
        e.itemName = name;
        e.itemCount = count;
        e.itemAux = aux;
        e.itemBlock = blockRuntimeId;
        e.itemEnchanted = enchanted;
        e.moveSerial = 1;
        byUnique[uid] = rid;
    }

    void EntityTracker::RemoveRuntime(uint64_t rid) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return;
        byUnique.erase(it->second.uniqueId);
        entities.erase(it);
    }

    void EntityTracker::Remove(int64_t uid) {
        std::lock_guard lock(mutex);
        const auto it = byUnique.find(uid);
        if (it == byUnique.end())
            return;
        entities.erase(it->second);
        byUnique.erase(it);
    }

    namespace {
        void ItemServerUpdate(Entity& e, const glm::dvec3& target, bool onGround) {
            const auto now = std::chrono::steady_clock::now();
            if (e.haveServerPos) {
                const double dt = std::chrono::duration<double>(now - e.lastServerAt).count();
                if (dt > 0.001 && dt < 0.6)
                    e.vel = (target - e.lastServerPos) / std::max(1.0, dt * 20.0);
            }
            if (onGround)
                e.vel = glm::dvec3(0.0);
            e.lastServerPos = target;
            e.haveServerPos = true;
            e.lastServerAt = now;
            e.pos = target;
            e.onGround = onGround;
            e.moveSerial++;
        }
    }

    namespace {
        float Wrap(float degrees) {
            degrees = std::fmod(degrees, 360.0f);
            if (degrees >= 180.0f)
                degrees -= 360.0f;
            if (degrees < -180.0f)
                degrees += 360.0f;
            return degrees;
        }

        void PlaceNow(Entity& e, const Entity::Lerp& to) {
            e.pos = e.prevPos = to.pos;
            e.pitch = e.prevPitch = to.pitch;
            e.yaw = e.prevYaw = to.yaw;
            e.headYaw = e.prevHeadYaw = to.headYaw;
            e.lerp.steps = 0;
            e.hasQueued = false;
        }

        // Actor::lerpTo 0x142C74350: a running interpolation started with forceCompletion keeps going and the new
        // target waits in QueuedMovementInterpolationComponent; anything else replaces the target
        void LerpTo(Entity& e, const Entity::Lerp& to) {
            if (!to.forceCompletion && e.lerp.forceCompletion && e.lerp.steps != 0) {
                e.queued = to;
                e.hasQueued = true;
                return;
            }
            e.lerp = to;
        }

        // move_entity / move_entity_delta handler 0x1422C88B0 -> 0x1422C8D20: steps = ticks when above 3, else 3;
        // a teleport of 0.5 block or more is placed at once
        void Receive(Entity& e, const glm::dvec3& next, float pitch, float yaw, float headYaw,
                     EntityTracker::MoveKind kind, uint64_t ticks, bool forceCompletion) {
            e.haveNet = true;
            e.netPos = next;
            e.netPitch = pitch;
            e.netYaw = yaw;
            e.netHeadYaw = headYaw;
            const Entity::Lerp to{next, pitch, yaw, headYaw, ticks >= 4 ? static_cast<int>(std::min<uint64_t>(ticks, 1000)) : 3,
                                  forceCompletion};
            const glm::dvec3 jump = next - e.pos;
            const bool farTeleport =
                kind == EntityTracker::MoveKind::Teleport && glm::dot(jump, jump) >= 0.25;
            // TODO: the game also places at once when the destination block is not loaded
            if (kind == EntityTracker::MoveKind::Place || farTeleport || !e.ticked)
                PlaceNow(e, to);
            else
                LerpTo(e, to);
        }
    }

    void EntityTracker::Move(uint64_t rid, glm::dvec3 pos, float pitch, float yaw, float headYaw, bool onGround,
                             MoveKind kind, uint64_t ticks, bool forceCompletion) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return;
        Entity& e = it->second;
        if (e.isItem && e.simulated) {
            ItemServerUpdate(e, pos, onGround);
            return;
        }
        const glm::dvec3 next = e.isPlayer ? glm::dvec3(pos.x, pos.y - kPlayerEye, pos.z) : pos;
        if (e.dying && e.isPlayer && glm::dot(next - e.pos, next - e.pos) > 2.25)
            e.dying = false;
        Receive(e, next, SignedPitch(pitch), yaw, headYaw, kind, ticks, forceCompletion);
        e.onGround = onGround;
        e.moveSerial++;
    }

    void EntityTracker::MoveDelta(uint64_t rid, const double* x, const double* y, const double* z,
                                  const float* pitch, const float* yaw, const float* headYaw, bool onGround,
                                  bool teleport, uint64_t ticks, bool forceCompletion) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return;
        Entity& e = it->second;
        if (e.isItem && e.simulated) {
            ItemServerUpdate(e, glm::dvec3(x ? *x : e.pos.x, y ? *y : e.pos.y, z ? *z : e.pos.z), onGround);
            return;
        }
        if (!e.haveNet) {
            e.netPos = e.pos;
            e.netPitch = e.pitch;
            e.netYaw = e.yaw;
            e.netHeadYaw = e.headYaw;
        }
        const glm::dvec3 next(x ? *x : e.netPos.x, y ? (e.isPlayer ? *y - kPlayerEye : *y) : e.netPos.y,
                              z ? *z : e.netPos.z);
        Receive(e, next, pitch ? SignedPitch(*pitch) : e.netPitch, yaw ? *yaw : e.netYaw,
                headYaw ? *headYaw : e.netHeadYaw, teleport ? MoveKind::Teleport : MoveKind::Lerp, ticks,
                forceCompletion);
        e.onGround = onGround;
        e.moveSerial++;
    }

    void EntityTracker::TickInterpolation() {
        std::lock_guard lock(mutex);
        for (auto& [rid, e] : entities) {
            e.prevPos = e.pos;
            e.prevPitch = e.pitch;
            e.prevYaw = e.yaw;
            e.prevHeadYaw = e.headYaw;
            e.ticked = true;
            if (e.isSelf || (e.isItem && e.simulated) || pickups.contains(rid))
                continue;
            // MovementInterpolatorSystem::handleQueuedMovementInterpolation 0x143634AE0
            if (e.hasQueued) {
                if (e.lerp.forceCompletion && e.lerp.steps != 0) {
                    e.queued.steps = std::max(e.queued.steps, 2) - 1;
                } else {
                    e.lerp = e.queued;
                    e.lerp.forceCompletion = false;
                    e.hasQueued = false;
                }
            }
            // MovementInterpolatorSystem::tickClient 0x143630D70: 1/steps of the way, the last step lands on it
            if (e.lerp.steps > 0) {
                if (e.lerp.steps == 1) {
                    e.pos = e.lerp.pos;
                    e.pitch = e.lerp.pitch;
                    e.yaw = e.lerp.yaw;
                    e.headYaw = e.lerp.headYaw;
                } else {
                    const double f = 1.0 / e.lerp.steps;
                    e.pos += (e.lerp.pos - e.pos) * f;
                    e.pitch += Wrap(e.lerp.pitch - e.pitch) * static_cast<float>(f);
                    e.yaw += Wrap(e.lerp.yaw - e.yaw) * static_cast<float>(f);
                    e.headYaw += Wrap(e.lerp.headYaw - e.headYaw) * static_cast<float>(f);
                }
                e.lerp.steps--;
            }
        }
    }

    void EntityTracker::SetVehicle(int64_t riderUid, int64_t vehicleUid) {
        std::lock_guard lock(mutex);
        const auto u = byUnique.find(riderUid);
        if (u == byUnique.end())
            return;
        const auto it = entities.find(u->second);
        if (it != entities.end())
            it->second.vehicleUid = vehicleUid;
    }

#define PERLIN_ENTITY_SET(name, body)                                                                        \
    void EntityTracker::name {                                                                               \
        std::lock_guard lock(mutex);                                                                         \
        const auto it = entities.find(rid);                                                                  \
        if (it == entities.end())                                                                            \
            return;                                                                                          \
        Entity& e = it->second;                                                                              \
        body;                                                                                                \
    }
    PERLIN_ENTITY_SET(SetFlags(uint64_t rid, uint64_t flags), e.flags = flags)
    PERLIN_ENTITY_SET(SetFlagsExtended(uint64_t rid, uint64_t flags), e.flagsExtended = flags)
    PERLIN_ENTITY_SET(SetCrawling(uint64_t rid, bool crawling), e.crawling = crawling)
    PERLIN_ENTITY_SET(SetScale(uint64_t rid, float scale), e.scale = scale)
    // server hitbox: draw/collide with what it simulates
    // one lane may come alone: the other keeps what was there
    PERLIN_ENTITY_SET(SetBoundingBox(uint64_t rid, float width, float height),
                      if (width >= 0.0f) e.boxWidth = width;
                      if (height >= 0.0f) e.boxHeight = height; e.boxSent = true)
    PERLIN_ENTITY_SET(SetNameTag(uint64_t rid, const std::string& nameTag), e.nameTag = nameTag)
    PERLIN_ENTITY_SET(SetVariant(uint64_t rid, std::optional<int> variant, std::optional<int> markVariant,
                                 std::optional<int> color),
                      if (variant) e.variant = *variant;
                      if (markVariant) e.markVariant = *markVariant; if (color) e.color = *color)
    PERLIN_ENTITY_SET(SetHealth(uint64_t rid, float health), e.health = health;
                      if (health <= 0.0f && !e.isItem) e.dying = true;
                      else if (health > 0.0f) e.dying = false)
    PERLIN_ENTITY_SET(SetAttribute(uint64_t rid, const std::string& name, float min, float value, float max),
                      if (e.attributes.size() < 64 ||
                          e.attributes.count(name))(e.attributes[name] = Entity::Attribute{min, value, max}))
    PERLIN_ENTITY_SET(SetTarget(uint64_t rid, int64_t targetUid), e.targetUid = targetUid)
    PERLIN_ENTITY_SET(SetOwner(uint64_t rid, int64_t ownerUid), e.ownerUid = ownerUid)
    PERLIN_ENTITY_SET(Die(uint64_t rid), e.dying = true)
    PERLIN_ENTITY_SET(SetHeld(uint64_t rid, const std::string& item), e.mainHand = item)
    PERLIN_ENTITY_SET(SetOffHand(uint64_t rid, const std::string& item), e.offHand = item)
    PERLIN_ENTITY_SET(SetArmor(uint64_t rid, int slot, const std::string& item),
                      if (slot >= 0 && slot < 4) e.armor[slot] = item)
    PERLIN_ENTITY_SET(Hurt(uint64_t rid), e.hurtSerial++)
    PERLIN_ENTITY_SET(Swing(uint64_t rid), e.swingSerial++)
    PERLIN_ENTITY_SET(SetProperty(uint64_t rid, const std::string& key, float value), e.props[key] = value)
    PERLIN_ENTITY_SET(SetPropertyText(uint64_t rid, const std::string& key, const std::string& text),
                      e.propText[key] = text)
#undef PERLIN_ENTITY_SET

    void EntityTracker::SetSkin(const std::string& uuid, std::shared_ptr<const SkinData> skin, bool listed) {
        std::lock_guard lock(mutex);
        if (listed || skins.contains(uuid))
            skins[uuid] = skin;
        for (auto& [rid, e] : entities)
            if (e.isPlayer && e.uuid == uuid)
                e.skin = skin;
    }

    void EntityTracker::RemoveSkin(const std::string& uuid) {
        std::lock_guard lock(mutex);
        skins.erase(uuid);
    }

    bool EntityTracker::Find(uint64_t rid, Entity& out) const {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return false;
        out = it->second;
        return true;
    }

    bool EntityTracker::FindUnique(int64_t uid, Entity& out) const {
        std::lock_guard lock(mutex);
        const auto u = byUnique.find(uid);
        if (u == byUnique.end())
            return false;
        const auto it = entities.find(u->second);
        if (it == entities.end())
            return false;
        out = it->second;
        return true;
    }

    void EntityTracker::SetSelf(uint64_t rid, const std::string& uuid) {
        std::lock_guard lock(mutex);
        selfRuntime = rid;
        selfUuid = uuid;
        const auto it = entities.find(rid);
        if (it != entities.end())
            it->second.isSelf = true;
    }

    namespace {
        double NowMs() {
            return std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }
        constexpr double kPickupMs = 160.0;
    }

    void EntityTracker::SetViewer(uint64_t rid, glm::dvec3 feet) {
        std::lock_guard lock(mutex);
        viewerRuntime = rid;
        viewerFeet = feet;
    }

    void EntityTracker::Pickup(uint64_t rid, uint64_t collector) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return;
        if (!it->second.isItem || pickups.contains(rid)) {
            if (!it->second.isItem) {
                byUnique.erase(it->second.uniqueId);
                entities.erase(it);
            }
            return;
        }
        pickups[rid] = {collector, NowMs(), it->second.pos};
    }

    void EntityTracker::StepPickupsLocked(std::vector<uint64_t>* finished) const {
        if (pickups.empty())
            return;
        const double now = NowMs();
        for (auto it = pickups.begin(); it != pickups.end();) {
            const auto item = entities.find(it->first);
            if (item == entities.end()) {
                it = pickups.erase(it);
                continue;
            }
            const double t = (now - it->second.startMs) / kPickupMs;
            if (t >= 1.0) {
                byUnique.erase(item->second.uniqueId);
                entities.erase(item);
                it = pickups.erase(it);
                continue;
            }
            glm::dvec3 target = it->second.from;
            const auto collector = entities.find(it->second.collector);
            if (viewerRuntime != 0 && it->second.collector == viewerRuntime)
                target = viewerFeet + glm::dvec3(0.0, 0.5, 0.0);
            else if (collector != entities.end())
                target = collector->second.pos + glm::dvec3(0.0, 0.5, 0.0);
            const double e = t * t;
            item->second.pos = it->second.from + (target - it->second.from) * e;
            ++it;
        }
        (void)finished;
    }

    void EntityTracker::Snapshot(std::vector<Entity>& out) const {
        std::lock_guard lock(mutex);
        StepPickupsLocked(nullptr);
        out.resize(entities.size());
        size_t i = 0;
        for (const auto& [rid, e] : entities)
            out[i++] = e;
    }

    std::vector<Entity> EntityTracker::Snapshot() const {
        std::vector<Entity> out;
        Snapshot(out);
        return out;
    }

    size_t EntityTracker::Count() const {
        std::lock_guard lock(mutex);
        return entities.size();
    }
}

namespace perlin::entity {
    void EntityTracker::SetMotion(uint64_t rid, glm::dvec3 velocity) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return;
        // kept for every actor (a projectile points along its spawn velocity); only items are simulated from it
        it->second.vel = velocity;
        if (it->second.isItem)
            it->second.simulated = true;
    }

    void EntityTracker::StepItems(const std::function<bool(double, double, double, double, double, double)>& hit) {
        std::lock_guard lock(mutex);
        constexpr double kHalf = 0.125, kHeight = 0.25;
        const auto blocked = [&](double x, double y, double z) {
            return hit(x - kHalf, y, z - kHalf, x + kHalf, y + kHeight, z + kHalf);
        };
        for (auto& [rid, e] : entities) {
            if (!e.isItem || !e.simulated)
                continue;
            if (e.haveServerPos &&
                std::chrono::steady_clock::now() - e.lastServerAt < std::chrono::milliseconds(70))
                continue;
            e.vel.y -= 0.04;
            glm::dvec3 p = e.pos;
            bool grounded = false;
            const double ny = p.y + e.vel.y;
            if (blocked(p.x, ny, p.z)) {
                if (e.vel.y < 0.0) {
                    grounded = true;
                    // come to rest on the surface itself (slabs, carpets and custom shapes are lower than a cell)
                    double lo = ny, hi = p.y;
                    if (!blocked(p.x, hi, p.z)) {
                        for (int i = 0; i < 10; i++) {
                            const double mid = (lo + hi) * 0.5;
                            (blocked(p.x, mid, p.z) ? lo : hi) = mid;
                        }
                        p.y = hi;
                    }
                }
                e.vel.y = 0.0;
            } else {
                p.y = ny;
            }
            const double nx = p.x + e.vel.x;
            if (blocked(nx, p.y, p.z))
                e.vel.x = 0.0;
            else
                p.x = nx;
            const double nz = p.z + e.vel.z;
            if (blocked(p.x, p.y, nz))
                e.vel.z = 0.0;
            else
                p.z = nz;
            const double friction = grounded ? 0.98 * 0.6 : 0.98;
            e.vel.x *= friction;
            e.vel.z *= friction;
            e.vel.y *= 0.98;
            if (grounded && std::fabs(e.vel.x) < 1e-3 && std::fabs(e.vel.z) < 1e-3)
                e.vel.x = e.vel.z = 0.0;
            if (glm::dot(p - e.pos, p - e.pos) > 1e-10) {
                e.pos = p;
                e.moveSerial++;
            }
            e.onGround = grounded;
        }
    }
}

namespace perlin::entity {
    void EntityTracker::SetSleeping(uint64_t rid, bool sleeping, bool playerFlag) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return;
        (playerFlag ? it->second.sleepPlayer : it->second.sleepExt) = sleeping;
        it->second.sleeping = it->second.sleepExt || it->second.sleepPlayer;
    }
}
