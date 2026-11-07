#include "client/multiplayer/BlockEntities.hpp"

#include <algorithm>

namespace perlin {
    namespace {
        void ReadFace(const ui::Json* node, SignFace& face) {
            if (!node || !node->IsObject())
                return;
            if (const ui::Json* t = node->Find("Text"))
                face.text = t->StringOr("");
            if (const ui::Json* c = node->Find("SignTextColor"))
                face.color =
                    static_cast<uint32_t>(static_cast<int64_t>(c->NumberOr(-16777216.0))) | 0xFF000000u;
            if (const ui::Json* g = node->Find("IgnoreLighting"))
                face.glow = g->NumberOr(0) != 0 || g->BoolOr(false);
        }
    }

    void BlockEntityStore::Set(int x, int y, int z, const ui::Json& nbt) {
        if (!nbt.IsObject())
            return;
        BlockEntity be;
        if (const ui::Json* id = nbt.Find("id"))
            be.id = id->StringOr("");
        if (const ui::Json* c = nbt.Find("color"))
            be.color = static_cast<int>(c->NumberOr(-1));
        const ui::Json* front = nbt.Find("FrontText");
        if (front)
            ReadFace(front, be.front);
        else if (const ui::Json* t = nbt.Find("Text")) {
            be.front.text = t->StringOr("");
            if (const ui::Json* c = nbt.Find("SignTextColor"))
                be.front.color =
                    static_cast<uint32_t>(static_cast<int64_t>(c->NumberOr(-16777216.0))) | 0xFF000000u;
            if (const ui::Json* g = nbt.Find("IgnoreLighting"))
                be.front.glow = g->NumberOr(0) != 0 || g->BoolOr(false);
        }
        ReadFace(nbt.Find("BackText"), be.back);
        if (const ui::Json* r = nbt.Find("Rotation"))
            be.rotation = static_cast<float>(r->NumberOr(0.0));
        if (const ui::Json* b = nbt.Find("Base"))
            be.base = static_cast<int>(b->NumberOr(-1));
        if (const ui::Json* t = nbt.Find("Type"))
            be.bannerType = static_cast<int>(t->NumberOr(0));
        if (const ui::Json* list = nbt.Find("Patterns"); list && list->IsArray())
            for (const ui::Json& p : list->array) {
                if (!p.IsObject())
                    continue;
                const ui::Json* code = p.Find("Pattern");
                const ui::Json* col = p.Find("Color");
                if (code && be.patterns.size() < 16)
                    be.patterns.emplace_back(code->StringOr(""),
                                             col ? static_cast<int>(col->NumberOr(0)) : 0);
            }
        const ui::Json* px = nbt.Find("pairx");
        const ui::Json* pz = nbt.Find("pairz");
        if (px && pz) {
            be.paired = true;
            be.pairX = static_cast<int>(px->NumberOr(0));
            be.pairZ = static_cast<int>(pz->NumberOr(0));
        }
        if (const ui::Json* w = nbt.Find("IsWaxed"))
            be.waxed = w->NumberOr(0) != 0 || w->BoolOr(false);
        be.nbt = nbt.Dump();
        if (be.nbt.size() > 65536)
            be.nbt.clear();
        std::lock_guard lock(mutex);
        entities[Key(x, y, z)] = std::move(be);
    }

    bool BlockEntityStore::Get(int x, int y, int z, BlockEntity& out) const {
        std::lock_guard lock(mutex);
        const auto it = entities.find(Key(x, y, z));
        if (it == entities.end())
            return false;
        out = it->second;
        return true;
    }

    void BlockEntityStore::Remove(int x, int y, int z) {
        std::lock_guard lock(mutex);
        entities.erase(Key(x, y, z));
    }

    void BlockEntityStore::RemoveChunk(int cx, int cz) {
        std::lock_guard lock(mutex);
        for (auto it = entities.begin(); it != entities.end();) {
            const int x = static_cast<int>((it->first >> 38) & 0x3FFFFFFu);
            const int z = static_cast<int>((it->first >> 12) & 0x3FFFFFFu);
            const int32_t sx = static_cast<int32_t>(static_cast<uint32_t>(x) << 6) >> 6;
            const int32_t sz = static_cast<int32_t>(static_cast<uint32_t>(z) << 6) >> 6;
            if ((sx >> 4) == cx && (sz >> 4) == cz)
                it = entities.erase(it);
            else
                ++it;
        }
    }

    void BlockEntityStore::Clear() {
        std::lock_guard lock(mutex);
        entities.clear();
        lids.clear();
    }

    float BlockEntityStore::LidAt(const Lid& lid, std::chrono::steady_clock::time_point now) {
        const float moved = std::chrono::duration<float>(now - lid.at).count() * 2.0f;
        return lid.open ? std::min(1.0f, lid.from + moved) : std::max(0.0f, lid.from - moved);
    }

    void BlockEntityStore::SetLidOpen(int x, int y, int z, bool open) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex);
        Lid& lid = lids[Key(x, y, z)];
        if (lid.open == open && lid.at != std::chrono::steady_clock::time_point{})
            return;
        lid.from = lid.at == std::chrono::steady_clock::time_point{} ? 0.0f : LidAt(lid, now);
        lid.open = open;
        lid.at = now;
    }

    float BlockEntityStore::LidOpenness(int x, int y, int z) const {
        std::lock_guard lock(mutex);
        const auto it = lids.find(Key(x, y, z));
        if (it == lids.end())
            return 0.0f;
        const float p = LidAt(it->second, std::chrono::steady_clock::now());
        return 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
    }

    void BlockEntityStore::TakeFrom(BlockEntityStore& other) {
        if (&other == this)
            return;
        std::scoped_lock lock(mutex, other.mutex);
        entities = std::move(other.entities);
        other.entities.clear();
        lids = std::move(other.lids);
        other.lids.clear();
    }

    size_t BlockEntityStore::Size() const {
        std::lock_guard lock(mutex);
        return entities.size();
    }
}
