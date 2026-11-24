#include "client/ui/UiBackend.hpp"

#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/resources/AssetJobs.hpp"
#include "client/systems/resources/TextureLoader.hpp"
#include "client/systems/resources/TexturePack.hpp"
#include "client/systems/sprites/Glint.hpp"
#include "client/systems/sprites/PerlinBadge.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/systems/text/McFont.hpp"
#include "client/ui/TenFont.hpp"
#include "client/ui/UiSprite.hpp"
#include "client/ui/draw/SceneView.hpp"
#include "util/Log.hpp"
#include "util/Resources.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <unordered_map>

namespace perlin::ui {
    namespace {
        UiBackend::ImageSource gImageSource;
        std::atomic<int> gMissingTextures{0};

        void Premultiply(Texture& t) {
            for (size_t i = 0; i + 3 < t.pixels.size(); i += 4) {
                const unsigned a = t.pixels[i + 3];
                if (a == 255)
                    continue;
                t.pixels[i] = static_cast<unsigned char>((t.pixels[i] * a + 127) / 255);
                t.pixels[i + 1] = static_cast<unsigned char>((t.pixels[i + 1] * a + 127) / 255);
                t.pixels[i + 2] = static_cast<unsigned char>((t.pixels[i + 2] * a + 127) / 255);
            }
        }

        constexpr int kSlotDocument = 0, kSlotServer = 1, kSlotPlugin = 2, kSlotOverlay = 3;
        constexpr size_t kMaxCached = 6;
    }

    struct UiBackend::Impl : draw::TextureSource {
        RenderContext* renderer = nullptr;
        int guiW = 0, guiH = 0, scale = 1;
        struct Doc {
            Scene scene;
            draw::Rendered rendered;
            draw::HoverSet hover;
            std::vector<uint32_t> meshes;
            bool dirty = true;
            bool upload = false;
            int renderW = -1, renderH = -1, renderScale = -1;
            uint64_t used = 0;
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        };
        struct Slot {
            std::vector<std::unique_ptr<Doc>> cache;
            Doc* current = nullptr;
        };
        Slot slots[4];
        std::unordered_map<std::string, draw::TexInfo> textures;
        std::unordered_map<std::string, std::pair<float, float>> moves;
        uint64_t clock = 0;
        float mx = 0, my = 0;
        bool mouse = false;
        uint32_t glintTexture = 0;
        std::string fontKey;
        uint32_t fontRevision = 0;
        std::vector<uint8_t> fontScratch;

        int Device() const override { return scale; }

        draw::TexInfo Get(const std::string& key) override {
            const auto it = textures.find(key);
            if (it != textures.end()) {
                if (it->second.handle && key == fontKey)
                    SyncFontRevision(it->second.handle);
                return it->second;
            }
            draw::TexInfo info = Load(key);
            if (key.starts_with("mcfont:")) {
                fontKey = key;
                fontRevision = Font().Revision();
            }
            textures.emplace(key, info);
            return info;
        }

        void SyncFontRevision(uint32_t handle) {
            const McFont& f = Font();
            if (fontRevision == f.Revision())
                return;
            int x = 0, y = 0, w = 0, h = 0;
            if (!f.DirtySince(fontRevision, x, y, w, h)) {
                x = y = 0;
                w = f.AtlasWidth();
                h = f.AtlasHeight();
            }
            if (w > 0 && h > 0) {
                const std::vector<uint8_t>& atlas = f.Atlas();
                fontScratch.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
                for (int row = 0; row < h; row++) {
                    const uint8_t* src =
                        atlas.data() + (static_cast<size_t>(y + row) * static_cast<size_t>(f.AtlasWidth()) + x) * 4;
                    uint8_t* dst = fontScratch.data() + static_cast<size_t>(row) * static_cast<size_t>(w) * 4;
                    for (int i = 0; i < w * 4; i += 4) {
                        const unsigned a = src[i + 3];
                        dst[i] = static_cast<uint8_t>((src[i] * a + 127) / 255);
                        dst[i + 1] = static_cast<uint8_t>((src[i + 1] * a + 127) / 255);
                        dst[i + 2] = static_cast<uint8_t>((src[i + 2] * a + 127) / 255);
                        dst[i + 3] = static_cast<uint8_t>(a);
                    }
                }
                renderer->UiUpdateTexture(handle, x, y, w, h, fontScratch.data(), w * 4);
            }
            fontRevision = f.Revision();
        }

        draw::TexInfo Upload(const Texture& t) {
            draw::TexInfo info;
            info.handle = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
            info.w = t.width;
            info.h = t.height;
            return info;
        }

        draw::TexInfo Load(const std::string& source) {
            draw::TexInfo none;
            if (source == "mchead:perlin:badge") {
                std::vector<Texture> chain;
                for (int size = 256; size >= 1; size /= 2) {
                    chain.push_back(PerlinBadge(size));
                    if (chain.back().width != size) {
                        chain.pop_back();
                        break;
                    }
                }
                if (chain.empty())
                    return none;
                std::vector<const uint8_t*> levels;
                for (Texture& level : chain) {
                    Premultiply(level);
                    levels.push_back(level.pixels.data());
                }
                draw::TexInfo info;
                info.handle = renderer->UiCreateTextureMipped(256, 256, levels);
                info.w = info.h = 256;
                return info;
            }
            Texture t;
            if (source == "mc:white") {
                t.width = t.height = 1;
                t.pixels = {255, 255, 255, 255};
            } else if (source.starts_with("mcfont:")) {
                const McFont& f = McFont::Instance();
                t.width = f.AtlasWidth();
                t.height = f.AtlasHeight();
                t.pixels = f.Atlas();
            } else if (source.starts_with("mcten:atlas#")) {
                const int emPx = std::atoi(source.c_str() + 12);
                const TenFont::Page* page = TenFont::Instance().PageFor(emPx);
                if (!page || page->rgba.empty())
                    return none;
                t.width = page->width;
                t.height = page->height;
                t.pixels = page->rgba;
            } else if (source.starts_with("mcres:")) {
                std::string bytes;
                std::string stem = source.substr(6);
                bool fromPack = false;
                if (TexturePack::Instance().Active() && stem.starts_with("ui_")) {
                    std::string name = stem;
                    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".png") == 0)
                        name.resize(name.size() - 4);
                    if (name.starts_with("ui_menu_btn") || name == "ui_menu_logo") {
                        fromPack = PackSpriteNative(name + ".png", t);
                    } else {
                        std::string baseBytes;
                        Texture base;
                        if (resources::Read(stem, baseBytes) &&
                            LoadTextureMemory(reinterpret_cast<const unsigned char*>(baseBytes.data()),
                                              baseBytes.size(), base))
                            fromPack = PackSpriteFor(name + ".png", base.width, base.height, t);
                    }
                }
                if (!fromPack && (!resources::Read(source.substr(6), bytes) ||
                                  !LoadTextureMemory(reinterpret_cast<const unsigned char*>(bytes.data()),
                                                     bytes.size(), t)))
                    return none;
            } else if (source.starts_with("mcurl:")) {
                std::string bytes;
                if (!gImageSource || !gImageSource(source.substr(6), bytes) ||
                    !LoadTextureMemory(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), t))
                    return none;
            } else if (source.starts_with("mcfile:")) {
                // an image file on disk (a world's preview); "#square..." after the path asks for its centre square,
                // and the rest of the fragment only tells a changed file apart so it is loaded again
                std::string path = source.substr(7);
                const size_t hash = path.rfind('#');
                const bool square = hash != std::string::npos && path.compare(hash + 1, 6, "square") == 0;
                if (hash != std::string::npos)
                    path.resize(hash);
                if (!LoadTexture(path, t))
                    return none;
                if (square && t.width != t.height && t.width > 0 && t.height > 0) {
                    const int side = std::min(t.width, t.height);
                    const int x0 = (t.width - side) / 2, y0 = (t.height - side) / 2;
                    std::vector<uint8_t> cropped(static_cast<size_t>(side) * side * 4);
                    for (int y = 0; y < side; y++)
                        std::copy_n(&t.pixels[(static_cast<size_t>(y + y0) * t.width + x0) * 4], static_cast<size_t>(side) * 4,
                                    &cropped[static_cast<size_t>(y) * side * 4]);
                    t.pixels = std::move(cropped);
                    t.width = t.height = side;
                }
            } else if (source.starts_with("mchead:")) {
                if (!UiAssets::Instance().Head(source.substr(7), t))
                    return none;
            } else if (source.starts_with("mcicon:")) {
                UiAssets& assets = UiAssets::Instance();
                if (!assets.ItemIcon(source.substr(7), t))
                    return none;
                if (!assets.BlocksReady())
                    gMissingTextures++;
            } else if (!UiAssets::Instance().Image(source, t)) {
                std::string tail = source;
                const size_t slash = tail.find_last_of("/\\:");
                if (slash != std::string::npos)
                    tail.erase(0, slash + 1);
                for (char& c : tail)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (tail == "null" || tail == "null.png" || tail == "null.tga")
                    return none;
                Log("ui", "missing texture {}", source);
                gMissingTextures++;
                t.width = t.height = 2;
                t.pixels = {248, 0, 248, 255, 0, 0, 0, 255, 0, 0, 0, 255, 248, 0, 248, 255};
            }
            Premultiply(t);
            return Upload(t);
        }

        void DestroyMeshes(Doc& d) {
            for (const uint32_t id : d.meshes)
                renderer->UiDestroyMesh(id);
            d.meshes.clear();
        }

        void Free(Doc& d) {
            DestroyMeshes(d);
            d.rendered.batches.clear();
            d.rendered.hits.clear();
        }

        void UploadDoc(Doc& d) {
            if (!d.upload)
                return;
            DestroyMeshes(d);
            std::vector<uint32_t> created;
            bool failed = false;
            for (draw::Batch& b : d.rendered.batches) {
                b.mesh = 0;
                if (b.vertices.empty() || b.indices.empty())
                    continue;
                const uint32_t id = renderer->UiCreateMesh(b.vertices.data(), static_cast<uint32_t>(b.vertices.size()),
                                                           b.indices.data(), static_cast<uint32_t>(b.indices.size()));
                if (!id) {
                    failed = true;
                    break;
                }
                b.mesh = id;
                created.push_back(id);
            }
            if (failed) {
                for (const uint32_t id : created)
                    renderer->UiDestroyMesh(id);
                for (draw::Batch& b : d.rendered.batches)
                    b.mesh = 0;
                return;
            }
            d.meshes = std::move(created);
            for (draw::Batch& b : d.rendered.batches) {
                std::vector<UiVertex>().swap(b.vertices);
                std::vector<uint32_t>().swap(b.indices);
            }
            d.upload = false;
        }

        void Refresh(Doc& d, bool claimMouse, bool& claimed) {
            if (d.renderW != guiW || d.renderH != guiH || d.renderScale != scale ||
                d.rendered.fontStamp != FontStampNow())
                d.dirty = true;
            if (d.dirty) {
                draw::RenderScene(d.scene, d.hover, *this, guiW, guiH, d.rendered);
                d.renderW = guiW;
                d.renderH = guiH;
                d.renderScale = scale;
                d.dirty = false;
                d.upload = true;
            }
            if (claimMouse && mouse) {
                const int hit = draw::HitTest(d.rendered, mx, my);
                draw::HoverSet next;
                if (hit >= 0)
                    draw::HoverChain(d.rendered, hit, next);
                if (next != d.hover) {
                    d.hover = std::move(next);
                    if (d.rendered.usesHover) {
                        draw::RenderScene(d.scene, d.hover, *this, guiW, guiH, d.rendered);
                        d.upload = true;
                    }
                }
                if (hit >= 0)
                    claimed = true;
            } else if (!d.hover.empty()) {
                d.hover.clear();
                if (d.rendered.usesHover) {
                    draw::RenderScene(d.scene, d.hover, *this, guiW, guiH, d.rendered);
                    d.upload = true;
                }
            }
            UploadDoc(d);
        }

        static const McFont& Font() { return McFont::Instance(); }
        static uint32_t FontStampNow() { return Font().Version(); }

        void UpdateAll() {
            bool claimed = false;
            for (const int s : {kSlotOverlay, kSlotPlugin, kSlotServer, kSlotDocument}) {
                Doc* d = slots[s].current;
                if (!d)
                    continue;
                Refresh(*d, !claimed, claimed);
            }
        }

        bool Load(int slotIndex, const Scene& scene, std::string& error) {
            (void)error;
            if (!renderer)
                return false;
            Slot& slot = slots[slotIndex];
            Doc* hit = nullptr;
            for (auto& d : slot.cache)
                if (d->scene == scene) {
                    hit = d.get();
                    break;
                }
            if (!hit) {
                static const bool dump = std::getenv("PERLIN_UI_DUMP") != nullptr;
                if (dump) {
                    SceneDoc sd;
                    sd.scene = scene;
                    Log("ui", "scene slot {}\n{}", slotIndex, DumpScene(sd));
                }
                auto doc = std::make_unique<Doc>();
                doc->scene = scene;
                while (slot.cache.size() >= kMaxCached) {
                    auto oldest = slot.cache.end();
                    for (auto it = slot.cache.begin(); it != slot.cache.end(); ++it)
                        if (it->get() != slot.current && (oldest == slot.cache.end() || (*it)->used < (*oldest)->used))
                            oldest = it;
                    if (oldest == slot.cache.end())
                        break;
                    Free(**oldest);
                    slot.cache.erase(oldest);
                }
                hit = doc.get();
                slot.cache.push_back(std::move(doc));
            }
            hit->used = ++clock;
            if (slot.current != hit && slot.current)
                slot.current->hover.clear();
            slot.current = hit;
            return true;
        }

        void Submit(Doc& d) {
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - d.start).count();
            const float s = static_cast<float>(scale);
            for (const draw::Batch& b : d.rendered.batches) {
                if (!b.mesh)
                    continue;
                UiDraw draw;
                draw.mesh = b.mesh;
                draw.texture = b.texture;
                draw.model[0] = s;
                draw.model[5] = s;
                draw.model[10] = 1.0f;
                if (b.shift >= 0 && static_cast<size_t>(b.shift) < d.rendered.shifts.size()) {
                    const draw::Shift& sh = d.rendered.shifts[static_cast<size_t>(b.shift)];
                    if (sh.id == "pano") {
                        draw.model[12] = std::round(draw::PanoOffset(d.rendered.panoSpan, seconds) * s);
                    } else if (const auto it = moves.find(sh.id); it != moves.end()) {
                        draw.model[12] = std::round((it->second.first - sh.x) * s);
                        draw.model[13] = std::round((it->second.second - sh.y) * s);
                    }
                }
                draw.scissor = b.scissor;
                for (int i = 0; i < 4; i++)
                    draw.scissorRect[i] = b.rect[i];
                draw.premultiplied = true;
                if (b.glint) {
                    if (glintTexture == 0)
                        glintTexture = CreateGlintTexture(*renderer);
                    draw.glintTexture = glintTexture;
                    if (draw.glintTexture == 0)
                        continue;
                    const auto ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                              std::chrono::steady_clock::now().time_since_epoch())
                                                              .count());
                    const GlintTransform g = MakeGlintTransform(ms);
                    std::copy(g.matrix, g.matrix + 4, draw.glintMatrix);
                    std::copy(g.offset, g.offset + 2, draw.glintOffset);
                }
                renderer->UiSubmit(b.layer, draw);
            }
        }

        void DropTextures() {
            for (auto& [key, info] : textures)
                if (info.handle)
                    renderer->UiDestroyTexture(info.handle);
            textures.clear();
            if (glintTexture) {
                renderer->UiDestroyTexture(glintTexture);
                glintTexture = 0;
            }
        }

        void Reload() {
            for (Slot& slot : slots)
                for (auto& d : slot.cache) {
                    DestroyMeshes(*d);
                    d->rendered.batches.clear();
                    d->dirty = true;
                }
            DropTextures();
        }
    };

    UiBackend::UiBackend() = default;
    UiBackend::~UiBackend() = default;

    void UiBackend::SetImageSource(ImageSource source) { gImageSource = std::move(source); }

    bool UiBackend::Initialize(RenderContext& renderer) {
        if (ready)
            return true;
        impl = std::make_unique<Impl>();
        impl->renderer = &renderer;
        ready = true;
        return true;
    }

    void UiBackend::Shutdown() {
        if (!ready)
            return;
        for (Impl::Slot& slot : impl->slots) {
            for (auto& d : slot.cache)
                impl->DestroyMeshes(*d);
            slot.cache.clear();
            slot.current = nullptr;
        }
        impl->DropTextures();
        impl.reset();
        ready = false;
    }

    void UiBackend::Resize(int w, int h, int s) {
        if (!impl)
            return;
        impl->guiW = w;
        impl->guiH = h;
        impl->scale = std::max(1, s);
    }

    bool UiBackend::SetDocument(const Scene& scene, std::string& error) {
        return impl && impl->Load(kSlotDocument, scene, error);
    }
    bool UiBackend::SetOverlay(const Scene& scene, std::string& error) {
        return impl && impl->Load(kSlotOverlay, scene, error);
    }
    bool UiBackend::SetPluginLayer(const Scene& scene, std::string& error) {
        return impl && impl->Load(kSlotPlugin, scene, error);
    }
    bool UiBackend::SetServerLayer(const Scene& scene, std::string& error) {
        return impl && impl->Load(kSlotServer, scene, error);
    }

    void UiBackend::ClearOverlay() {
        if (impl)
            impl->slots[kSlotOverlay].current = nullptr;
    }
    void UiBackend::ClearPluginLayer() {
        if (impl)
            impl->slots[kSlotPlugin].current = nullptr;
    }
    void UiBackend::ClearServerLayer() {
        if (impl)
            impl->slots[kSlotServer].current = nullptr;
    }
    bool UiBackend::HasPluginLayer() const { return impl && impl->slots[kSlotPlugin].current != nullptr; }
    bool UiBackend::HasOverlay() const { return impl && impl->slots[kSlotOverlay].current != nullptr; }

    void UiBackend::MoveOverlayElement(const char* id, int guiX, int guiY) {
        if (impl)
            impl->moves[id] = {static_cast<float>(guiX), static_cast<float>(guiY)};
    }

    void UiBackend::MouseMove(float guiX, float guiY) {
        if (!impl)
            return;
        impl->mx = guiX;
        impl->my = guiY;
        impl->mouse = true;
    }

    namespace {
        const SceneNode* HoveredNode(const draw::Rendered& r, const draw::HoverSet& hover, int& index) {
            (void)hover;
            return index >= 0 ? r.hits[static_cast<size_t>(index)].node : nullptr;
        }
    }

    std::string UiBackend::OverlayHover() {
        if (!impl || !impl->slots[kSlotOverlay].current)
            return {};
        impl->UpdateAll();
        const Impl::Doc& d = *impl->slots[kSlotOverlay].current;
        int index = impl->mouse ? draw::HitTest(d.rendered, impl->mx, impl->my) : -1;
        for (; index >= 0; index = d.rendered.hits[static_cast<size_t>(index)].parent) {
            const SceneNode* n = HoveredNode(d.rendered, d.hover, index);
            if (n && !n->id.empty())
                return n->id;
        }
        return {};
    }

    bool UiBackend::OverlayHoverClickSound(std::string& name, float& volume, float& pitch) {
        if (!impl || !impl->slots[kSlotOverlay].current)
            return false;
        const Impl::Doc& d = *impl->slots[kSlotOverlay].current;
        int index = impl->mouse ? draw::HitTest(d.rendered, impl->mx, impl->my) : -1;
        static const char* const kNative[] = {"btn", "sbtn", "rbtn", "pg", "row", "ticon", "go", "acc"};
        for (; index >= 0; index = d.rendered.hits[static_cast<size_t>(index)].parent) {
            const SceneNode& n = *d.rendered.hits[static_cast<size_t>(index)].node;
            if (draw::HasClass(n, "off"))
                return false;
            if (!n.snd.empty()) {
                name = n.snd;
                volume = n.vol.empty() ? 1.0f : static_cast<float>(std::atof(n.vol.c_str()));
                pitch = n.pit.empty() ? 1.0f : static_cast<float>(std::atof(n.pit.c_str()));
                return true;
            }
            if (draw::HasClass(n, "bt"))
                return false;
            if (n.kind == SceneNode::Button || n.kind == SceneNode::Slider) {
                name = "random.click";
                volume = pitch = 1.0f;
                return true;
            }
            for (const char* cls : kNative)
                if (draw::HasClass(n, cls)) {
                    name = "random.click";
                    volume = pitch = 1.0f;
                    return true;
                }
        }
        return false;
    }

    std::string UiBackend::HoverLink() {
        if (!impl)
            return {};
        impl->UpdateAll();
        for (const int s : {kSlotOverlay, kSlotPlugin, kSlotServer, kSlotDocument}) {
            const Impl::Doc* d = impl->slots[s].current;
            if (!d || !impl->mouse)
                continue;
            int index = draw::HitTest(d->rendered, impl->mx, impl->my);
            if (index < 0)
                continue;
            for (; index >= 0; index = d->rendered.hits[static_cast<size_t>(index)].parent) {
                const SceneNode& n = *d->rendered.hits[static_cast<size_t>(index)].node;
                if (n.id.starts_with("link:"))
                    return n.id;
            }
            return {};
        }
        return {};
    }

    void UiBackend::Update() {
        if (impl)
            impl->UpdateAll();
    }

    void UiBackend::Render() {
        if (!impl)
            return;
        {
            static uint64_t lastSignature = ~0ull;
            static int sinceRetry = 0, retries = 0;
            const assets::Job& samples = assets::Samples();
            const assets::Job& client = assets::Client();
            const bool busy = assets::Busy(samples) || assets::Busy(client);
            const uint64_t signature = (static_cast<uint64_t>(UiAssets::RootGeneration().load()) << 40) ^
                                       (static_cast<uint64_t>(samples.stage.load()) << 56) ^
                                       (static_cast<uint64_t>(client.stage.load()) << 48) ^
                                       (samples.filesDone.load() << 16) ^ client.filesDone.load();
            sinceRetry++;
            if (gMissingTextures.load() > 0 && !busy) {
                const bool changed = signature != lastSignature;
                const int interval = retries < 2 ? 300 : 3600;
                if (changed || sinceRetry > interval) {
                    retries = changed ? 0 : retries + 1;
                    lastSignature = signature;
                    sinceRetry = 0;
                    gMissingTextures = 0;
                    ReloadTextures();
                }
            } else if (gMissingTextures.load() == 0) {
                lastSignature = signature;
            }
        }
        impl->UpdateAll();
        for (const int s : {kSlotDocument, kSlotServer, kSlotPlugin, kSlotOverlay})
            if (impl->slots[s].current)
                impl->Submit(*impl->slots[s].current);
    }

    void UiBackend::UnloadDocuments() {
        if (!impl)
            return;
        for (Impl::Slot& slot : impl->slots) {
            for (auto& d : slot.cache)
                impl->DestroyMeshes(*d);
            slot.cache.clear();
            slot.current = nullptr;
        }
        impl->DropTextures();
    }

    void UiBackend::ReloadTextures() {
        if (impl)
            impl->Reload();
    }
}
