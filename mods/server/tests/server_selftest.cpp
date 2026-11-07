#include <openssl/core_names.h>
#include "test_util.hpp"
#include "test_auth.hpp"
#include "core/Auth.hpp"

#include "core/Access.hpp"
#include "core/Config.hpp"
#include "core/Json.hpp"
#include "net/Batch.hpp"
#include "net/Connection.hpp"
#include "plugin/PluginHost.hpp"
#include "inventory/Inventory.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"
#include "world/Chunk.hpp"
#include "world/Generator.hpp"
#include "world/Loot.hpp"

#include "world/level/chunk/ChunkCodec.hpp"
#include "protocol/Packets.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

using namespace perlin::server;
namespace abi = perlin::abi;

static void TestJson() {
    json::Value v;
    CHECK(json::Parse(R"({"a":[1,2,{"b":"xé\n"}],"t":true,"n":null,"f":-1.5e1})", v));
    CHECK(v["a"][2]["b"].String() == "x\xc3\xa9\n");
    CHECK(v["t"].Bool() && v["n"].IsNull() && v["f"].Number() == -15.0);
    CHECK(v["missing"]["deeper"][3].IsNull());
    CHECK(!json::Parse("{\"a\":", v));
    CHECK(!json::Parse("[1,2] x", v));
    CHECK(!json::Parse(std::string(500, '[') + std::string(500, ']'), v));
    json::Value claims;
    CHECK(json::DecodeJwtPayload("e30.eyJleHRyYURhdGEiOnsiZGlzcGxheU5hbWUiOiJBbGljZSJ9fQ.sig", claims));
    CHECK(claims["extraData"]["displayName"].String() == "Alice");
    CHECK(json::Escape("a\"b\\c\n") == "a\\\"b\\\\c\\n");
}

static void TestBlockHash() {
    CHECK(BlockNetworkHash("minecraft:air", {}) == -604749536);
    BlockRegistry& r = BlockRegistry::Instance();
    const uint16_t dirt = r.Intern("minecraft:dirt");
    CHECK(dirt != 0 && r.Intern("minecraft:dirt") == dirt);
    CHECK(r.Name(dirt) == "minecraft:dirt" && r.Hash(dirt) == BlockNetworkHash("minecraft:dirt", {}));
    const uint16_t a =
        r.Intern("minecraft:log", {{"pillar_axis", std::string("y")}, {"stripped_bit", false}});
    const uint16_t b =
        r.Intern("minecraft:log", {{"stripped_bit", false}, {"pillar_axis", std::string("y")}});
    CHECK(a == b);
    CHECK(r.Find("minecraft:nope") == 0);
}

static ServerConfig FlatConfig() {
    ServerConfig c;
    c.generator = "flat";
    c.terrainBackend = "cpu";
    return c;
}

static void TestFlatGeneration() {
    ServerConfig c = FlatConfig();
    FlatCpuBackend gen(c);
    std::vector<ChunkPos> pos = {{0, 0}, {5, -7}};
    std::vector<ChunkColumn> cols(pos.size());
    gen.Generate(pos, cols);
    BlockRegistry& r = BlockRegistry::Instance();
    for (const ChunkColumn& col : cols) {
        CHECK(col.Get(3, -64, 9) == BlockCatalog::Instance().DefaultBlock("minecraft:bedrock"));
        CHECK(col.Get(0, -63, 0) == r.Find("minecraft:dirt"));
        CHECK(col.Get(15, -62, 15) == r.Find("minecraft:dirt"));
        CHECK(col.Get(7, -61, 7) == r.Find("minecraft:grass_block"));
        CHECK(col.Get(7, -60, 7) == 0);
        CHECK(col.Get(7, 319, 7) == 0);
        CHECK(col.IsUniform(5) && col.IsUniform(23));
        CHECK(!col.IsUniform(0));
    }
    CHECK(FlatSurfaceY(c) == -60);
}

static void TestChunkEncodingDecodesWithClientCodec() {
    ServerConfig c = FlatConfig();
    FlatCpuBackend gen(c);
    std::vector<ChunkPos> pos = {{0, 0}};
    std::vector<ChunkColumn> cols(1);
    gen.Generate(pos, cols);
    cols[0].Set(2, -61, 3, BlockRegistry::Instance().Intern("minecraft:stone"));
    std::vector<uint8_t> payload;
    cols[0].EncodeNetwork(payload);

    std::vector<std::pair<int, perlin::SubChunkData>> subs;
    size_t used = 0;
    const char* why = nullptr;
    const bool ok = perlin::ChunkCodec::ParseColumn(payload.data(), payload.size(), kSections, kMinY >> 4,
                                                    kSections, subs, used, &why);
    CHECK(ok);
    if (!ok) {
        std::fprintf(stderr, "ParseColumn: %s\n", why ? why : "?");
        return;
    }
    CHECK(subs.size() == static_cast<size_t>(kSections));
    BlockRegistry& r = BlockRegistry::Instance();
    for (const auto& [y, sc] : subs) {
        CHECK(sc.storages.size() == 1);
        perlin::ChunkCodec::CellIds cells;
        CHECK(perlin::ChunkCodec::Expand(sc.storages[0], cells));
        if (y == kMinY >> 4) {
            CHECK(cells[perlin::server::CellIndex(2, 3, 3)] == r.Hash(r.Find("minecraft:stone")));
            CHECK(cells[perlin::server::CellIndex(0, 0, 0)] ==
                  r.Hash(BlockCatalog::Instance().DefaultBlock("minecraft:bedrock")));
            CHECK(cells[perlin::server::CellIndex(8, 15, 8)] == r.Hash(0));
        }
    }
    CHECK(used <= payload.size());
}

static void TestBatchCodec() {
    BatchCodec plain, comp;
    comp.EnableCompression(64);
    std::vector<uint8_t> content;
    const std::vector<uint8_t> small = {1, 2, 3};
    const std::vector<uint8_t> big(5000, 7);
    BatchCodec::AppendPacket(content, 9, small);
    BatchCodec::AppendPacket(content, 58, big);

    for (BatchCodec* codec : {&plain, &comp}) {
        const std::vector<uint8_t> wire = codec->Seal(content);
        std::vector<uint8_t> back;
        CHECK(codec->Open(wire, back));
        CHECK(back == content);
        std::vector<std::pair<uint32_t, size_t>> seen;
        CHECK(BatchCodec::Split(
            back, [&](uint32_t id, std::span<const uint8_t> body) { seen.emplace_back(id, body.size()); }));
        const std::pair<uint32_t, size_t> first{9, 3}, second{58, 5000};
        CHECK(seen.size() == 2 && seen[0] == first && seen[1] == second);
    }
    CHECK(comp.Seal(content).size() < 200 && comp.Seal(content)[0] == 0x00);
    std::vector<uint8_t> tiny;
    BatchCodec::AppendPacket(tiny, 9, small);
    CHECK(comp.Seal(tiny)[0] == 0xFF);

    std::vector<uint8_t> junk;
    CHECK(!comp.Open(std::vector<uint8_t>{0x00, 1, 2, 3, 4}, junk));
    CHECK(!comp.Open(std::vector<uint8_t>{0x42, 1}, junk));
    CHECK(!BatchCodec::Split(std::vector<uint8_t>{0x05, 0x01}, [](uint32_t, std::span<const uint8_t>) {}));
}

static void TestFragments() {
    const std::vector<uint8_t> data(kFragmentSize * 2 + 10, 0xAB);
    const auto parts = SplitFragments(data.data(), data.size());
    CHECK(parts.size() == 3 && parts[0][0] == 2 && parts[1][0] == 1 && parts[2][0] == 0);
    FragmentAssembler asm_;
    std::vector<uint8_t> out;
    CHECK(!asm_.Push(parts[0].data(), parts[0].size(), out));
    CHECK(!asm_.Push(parts[1].data(), parts[1].size(), out));
    CHECK(asm_.Push(parts[2].data(), parts[2].size(), out));
    CHECK(out == data);
    CHECK(SplitFragments(data.data(), 0).empty());
}

static void TestConfig() {
    const std::string path = (std::filesystem::temp_directory_path() / "perlin_server_cfg.json").string();
    ServerConfig c;
    c.name = "Test \"quoted\"";
    c.port = 24000;
    c.operators = {"Alice", "Bob"};
    c.flatLayers = {{"minecraft:bedrock", 2}, {"minecraft:stone", 5}};
    CHECK(c.Save(path));
    std::string error;
    const ServerConfig d = ServerConfig::Load(path, &error);
    CHECK(error.empty() && d.name == c.name && d.port == 24000 && d.operators.size() == 2);
    CHECK(d.flatLayers.size() == 2 && d.flatLayers[1].block == "minecraft:stone" &&
          d.flatLayers[1].count == 5);
    std::filesystem::remove(path);
    std::ofstream(path) << "{ not json";
    const ServerConfig e = ServerConfig::Load(path, &error);
    CHECK(!error.empty() && e.port == ServerConfig{}.port);
    std::filesystem::remove(path);
}

static void TestProperties() {
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() / "perlin_server.properties").string();
    {
        std::ofstream out(path);
        out << "# comment\nserver-name=My "
               "Server\ngamemode=creative\ndifficulty=hard\nmax-players=7\nonline-mode=false\nallow-list="
               "true\n"
               "server-port=20000\nserver-udp-ports=21200-21300:19400-19500\nview-distance=16\nlevel-seed="
               "Perlin\n"
               "default-player-permission-level=visitor\ncompression-threshold=1\ncompression-algorithm="
               "snappy\n"
               "chat-restriction=disabled\nmax-players-bogus=1\nplayer-idle-timeout=5\ntick-distance="
               "nope\nlevel-name=\n"
               "persistent-ticking-area-max-count=11\n";
    }
    std::string error;
    ServerConfig c = ServerConfig::Load(path, &error);
    CHECK(error.empty());
    CHECK(c.name == "My Server" && c.motd == "My Server" && c.gameMode == 1 && c.difficulty == 3 &&
          c.maxPlayers == 7);
    CHECK(!c.xboxAuth && c.whitelist && c.port == 20000 && c.viewDistance == 16);
    CHECK(c.udpPortsSet && c.udpPortBegin == 19400 && c.udpPortEnd == 19500 && c.udpPublicBegin == 21200 &&
          c.udpPublicEnd == 21300);
    CHECK(!c.randomSeed && static_cast<int64_t>(c.seed) == -1907856268LL);
    CHECK(c.defaultPermission == "visitor" && c.compressionThreshold == 1 &&
          c.compressionAlgorithm == "snappy");
    CHECK(c.chatRestriction == "Disabled" && c.idleTimeoutMinutes == 5);
    CHECK(c.tickDistance == 4 && c.levelName == "Bedrock level");
    CHECK(c.SaveProperties(path));
    const ServerConfig d = ServerConfig::Load(path, &error);
    CHECK(d.name == c.name && d.gameMode == 1 && d.udpPortBegin == 19400 && d.udpPublicBegin == 21200 &&
          d.maxPlayers == 7);
    CHECK(static_cast<int64_t>(d.seed) == -1907856268LL && d.levelSeedText == "Perlin");
    bool keptBogus = false, keptTicking = false;
    for (const auto& [k, v] : d.extraProperties) {
        keptBogus = keptBogus || (k == "max-players-bogus" && v == "1");
        keptTicking = keptTicking || (k == "persistent-ticking-area-max-count" && v == "11");
    }
    CHECK(keptBogus && keptTicking);
    {
        std::ifstream in(path);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (const char* key :
             {"server-name=", "allow-list=", "server-udp-ports=21200-21300:19400-19500",
              "enable-lan-visibility=true", "transport=nethernet", "compression-threshold=1",
              "block-network-ids-are-hashes=true", "server-build-radius-ratio=Disabled"})
            CHECK(text.contains(key));
    }

    const auto ports = [](const std::string& v) {
        ServerConfig x;
        x.SetProperty("server-udp-ports", v);
        return x;
    };
    ServerConfig p = ports("19489");
    CHECK(p.udpPortBegin == 19489 && p.udpPortEnd == 19489 && p.udpPublicBegin == 0);
    p = ports("29489:19489");
    CHECK(p.udpPortBegin == 19489 && p.udpPublicBegin == 29489 && p.udpPublicEnd == 29489);
    p = ports("12.19.20.18:19489:19489");
    CHECK(p.udpPublicAddress == "12.19.20.18" && p.udpPortBegin == 19489 && p.udpPublicBegin == 19489);
    p = ports("[2001:db8::2021:26:03]:21200-21300:19400-19500");
    CHECK(p.udpPublicAddress == "2001:db8::2021:26:03" && p.udpPortBegin == 19400 && p.udpPortEnd == 19500 &&
          p.udpPublicBegin == 21200);
    p = ports("");
    CHECK(p.udpPortsSet && p.udpPortBegin == 0);
    p = ports("21200-21300:19400-19450");
    CHECK(!p.udpPortsSet);
    p = ports("70000");
    CHECK(!p.udpPortsSet);

    ServerConfig s;
    s.SetProperty("level-seed", "12345");
    CHECK(s.seed == 12345 && !s.randomSeed);
    s.SetProperty("level-seed", "");
    CHECK(s.randomSeed);
    CHECK(ServerConfig::SeedFromText("-7") == static_cast<uint64_t>(-7LL));
    fs::remove(path);

    const fs::path dir = fs::temp_directory_path() / "perlin_cfg_dir";
    fs::remove_all(dir);
    fs::create_directories(dir);
    { std::ofstream(dir / "server.json") << "{\"name\": \"legacy\", \"port\": 24001}"; }
    ServerConfig legacy = ServerConfig::LoadDirectory(dir.string(), &error);
    CHECK(legacy.name == "legacy" && legacy.port == 24001);
    { std::ofstream(dir / "server.properties") << "server-name=props\n"; }
    legacy = ServerConfig::LoadDirectory(dir.string(), &error);
    CHECK(legacy.name == "props" && legacy.port == 19132);
    fs::remove_all(dir);
}

static void TestAccessFiles() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "perlin_access";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream(dir / "allowlist.json")
            << "[\n {\n \"ignoresPlayerLimit\": false,\n \"name\": \"MyPlayer\"\n },\n {\n "
               "\"ignoresPlayerLimit\": true,\n \"name\": \"AnotherPlayer\",\n \"xuid\": \"274817248\"\n "
               "}\n]";
        std::ofstream(dir / "permissions.json")
            << "[\n {\"permission\": \"operator\", \"xuid\": \"451298348\"},\n {\"permission\": \"member\", "
               "\"xuid\": \"52819329\"},\n {\"permission\": \"visitor\", \"xuid\": \"234114123\"}\n]";
    }
    AccessList allow(AccessList::Kind::Allowlist);
    allow.Load((dir / "allowlist.json").string());
    CHECK(allow.Entries().size() == 2);
    CHECK(allow.Contains("myplayer", "") && allow.Contains("", "274817248"));
    CHECK(allow.Find("AnotherPlayer", "")->ignoresPlayerLimit &&
          !allow.Find("MyPlayer", "")->ignoresPlayerLimit);
    CHECK(allow.Add("MyPlayer", "999"));
    CHECK(allow.Find("", "999") != nullptr);
    AccessList perms(AccessList::Kind::Permissions);
    perms.Load((dir / "permissions.json").string());
    CHECK(perms.Entries().size() == 3);
    CHECK(perms.Find("", "451298348")->permission == "operator");
    CHECK(perms.Find("", "234114123")->permission == "visitor");
    CHECK(perms.SetPermission("", "52819329", "operator"));
    CHECK(!perms.SetPermission("", "52819329", "operator"));
    AccessList again(AccessList::Kind::Permissions);
    again.Load((dir / "permissions.json").string());
    CHECK(again.Find("", "52819329")->permission == "operator");
    {
        std::ifstream in(dir / "permissions.json");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text.contains("\"permission\": \"operator\"") && !text.contains("ignoresPlayerLimit"));
    }
    {
        std::ifstream in(dir / "allowlist.json");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text.contains("\"ignoresPlayerLimit\": true") && !text.contains("\"permission\""));
    }
    fs::remove_all(dir);
}

struct MockApi final : PluginApi {
    template <class T> using R = abi::host::Result<T>;
    std::vector<std::string> broadcasts, messages, commands;
    R<void> OpBroadcast(PluginCall&, const abi::TextMessage& m) override {
        broadcasts.push_back(m.text);
        return {};
    }
    R<void> OpPlayerMessage(PluginCall&, const abi::PlayerText& m) override {
        messages.push_back(std::to_string(m.player) + ":" + m.text);
        return {};
    }
    void RegisterCommand(const std::string& plugin, const std::string& name, const std::string&) override {
        commands.push_back(plugin + "/" + name);
    }
    R<abi::OnlinePlayerList> OpOnlinePlayers(PluginCall&) override {
        return abi::OnlinePlayerList{{{7, "Alice", 1, 2, 3}, {8, "Bob", 4, 5, 6}}};
    }
    R<abi::TextMessage> OpBlockName(PluginCall&, const abi::BlockCoord&) override {
        return abi::TextMessage{"minecraft:stone"};
    }
};

static void TestPluginHost(const char* wasm) {
    if (!wasm) {
        std::fprintf(stderr, "skip plugin test: no wasm path given\n");
        return;
    }
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "perlin_server_plugins";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::copy_file(wasm, dir / "hello_server.wasm");
    std::ofstream(dir / "broken.wasm") << "not wasm";

    MockApi api;
    PluginHost host(api);
    host.LoadDirectory(dir.string());
    const auto list = host.List();
    CHECK(list.size() == 1 && list[0].name == "hello_server" && list[0].enabled);
    CHECK(api.commands.size() == 2 && api.commands[0] == "hello_server/hello" && api.commands[1] == "hello_server/menu");

    host.Dispatch<abi::Event::PlayerJoin>({7, "Alice"});
    CHECK(api.messages.size() == 1 && api.messages[0] == "7:Welcome, Alice!");

    CHECK(host.Dispatch<abi::Event::PlayerChat>({7, "Alice", "look a Creeper!"}));
    CHECK(!host.Dispatch<abi::Event::PlayerChat>({7, "Alice", "hello everyone"}));

    api.messages.clear();
    CHECK(host.DispatchCommand("hello_server", 7, "hello", ""));
    CHECK(api.messages.size() == 1 && api.messages[0] == "7:Hello! Players online: 2");
    CHECK(!host.AnyoneListens(abi::Event::Tick));
    CHECK(host.AnyoneListens(abi::Event::PlayerChat));
    host.UnloadAll();
    fs::remove_all(dir);
}

static void TestBlockCatalog() {
    BlockCatalog& c = BlockCatalog::Instance();
    const std::string states = BlockCatalog::FindFile("", "block_states.json");
    const std::string names = BlockCatalog::FindFile("", "block_destroy.tsv");
    CHECK(!states.empty() && !names.empty());
    std::string error;
    CHECK(c.Load(states, names, error));
    CHECK(c.StateCount() > 20000 && c.Items().size() > 300);
    json::Value root;
    std::ifstream in(states);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(json::Parse(text, root));
    size_t bad = 0;
    for (const json::Value& e : root.items) {
        StateList list;
        for (const auto& [k, v] : e[2].members) {
            const std::string t = v[0].String();
            if (t == "b")
                list.emplace_back(k, StateValue(v[1].Int() != 0));
            else if (t == "i")
                list.emplace_back(k, StateValue(static_cast<int32_t>(v[1].Int())));
            else
                list.emplace_back(k, StateValue(v[1].String()));
        }
        if (BlockNetworkHash(e[1].String(), list) != static_cast<int32_t>(e[0].Int()))
            bad++;
    }
    CHECK(bad == 0);
    const CatalogItem* stone = c.ItemByName("minecraft:stone");
    CHECK(stone && c.ItemById(stone->networkId) == stone && c.ItemByBlockHash(stone->blockHash) == stone);
    const CatalogItem* log = c.ItemByName("minecraft:oak_log");
    CHECK(log && c.RegistryId(log->blockHash) != 0);
    CHECK(c.RegistryId(12345) == 0);
}

static perlin::proto2225::ItemStackRequestActionsEntry
Act(perlin::proto2225::ItemStackRequestActionsEntryTypeId type, perlin::proto2225::ContainerSlotType from,
    int fromSlot, perlin::proto2225::ContainerSlotType to, int toSlot, int count) {
    perlin::proto2225::ItemStackRequestActionsEntry a;
    a.typeId = type;
    a.count = static_cast<uint8_t>(count);
    a.source.slotType.containerId = from;
    a.source.slot = static_cast<uint8_t>(fromSlot);
    a.destination.slotType.containerId = to;
    a.destination.slot = static_cast<uint8_t>(toSlot);
    return a;
}

static void TestInventory() {
    namespace p = perlin::proto2225;
    using T = p::ItemStackRequestActionsEntryTypeId;
    using C = p::ContainerSlotType;
    const CatalogItem* stone = BlockCatalog::Instance().ItemByName("minecraft:stone");
    const CatalogItem* dirt = BlockCatalog::Instance().ItemByName("minecraft:dirt");
    CHECK(stone && dirt);
    PlayerInventory inv;

    p::ItemStackRequest r;
    r.requestId = -1;
    auto craft = Act(T::CraftCreative, C::HotbarAndInventory, 0, C::HotbarAndInventory, 0, 0);
    craft.itemId = static_cast<uint32_t>(stone->networkId);
    r.actions = {craft, Act(T::Take, C::CreativeOutput, 50, C::Cursor, 0, 64),
                 Act(T::Place, C::Cursor, 0, C::HotbarAndInventory, 2, 40)};
    RequestResult res = ApplyStackRequest(inv, r, true);
    CHECK(res.ok);
    CHECK(inv.main[2].networkId == stone->networkId && inv.main[2].count == 40);
    CHECK(inv.cursor.count == 24 && inv.output.Empty());
    CHECK(inv.main[2].stackId > 0);
    const auto response = MakeResponse(inv, r.requestId, res);
    CHECK(response.status == p::ItemStackResponsesEntryStatus::Ok && response.containers &&
          response.containers->size() == 2);

    PlayerInventory survival;
    CHECK(!ApplyStackRequest(survival, r, false).ok);

    PlayerInventory before = inv;
    p::ItemStackRequest bad;
    bad.requestId = -3;
    bad.actions = {Act(T::Place, C::Cursor, 0, C::HotbarAndInventory, 5, 10),
                   Act(T::Take, C::HotbarAndInventory, 30, C::Cursor, 0, 1)};
    CHECK(!ApplyStackRequest(inv, bad, true).ok);
    CHECK(inv.cursor.count == before.cursor.count && inv.main[5].Empty());

    p::ItemStackRequest stale;
    stale.requestId = -5;
    auto move = Act(T::Place, C::HotbarAndInventory, 2, C::HotbarAndInventory, 3, 5);
    move.source.stackId = inv.main[2].stackId + 100;
    stale.actions = {move};
    CHECK(!ApplyStackRequest(inv, stale, true).ok);
    move.source.stackId = inv.main[2].stackId;
    stale.actions = {move};
    CHECK(ApplyStackRequest(inv, stale, true).ok && inv.main[3].count == 5 && inv.main[2].count == 35);

    inv.main[4] = inv.MakeStack(dirt->networkId, 3);
    p::ItemStackRequest swap;
    swap.requestId = -7;
    swap.actions = {Act(T::Swap, C::HotbarAndInventory, 3, C::HotbarAndInventory, 4, 0)};
    CHECK(ApplyStackRequest(inv, swap, true).ok && inv.main[3].networkId == dirt->networkId &&
          inv.main[4].networkId == stone->networkId);
    inv.main[4].count = 60;
    p::ItemStackRequest merge;
    merge.requestId = -9;
    merge.actions = {Act(T::Place, C::HotbarAndInventory, 2, C::HotbarAndInventory, 4, 30)};
    CHECK(!ApplyStackRequest(inv, merge, true).ok);
    p::ItemStackRequest destroy;
    destroy.requestId = -11;
    destroy.actions = {Act(T::Destroy, C::Cursor, 0, C::Cursor, 0, 24)};
    CHECK(ApplyStackRequest(inv, destroy, true).ok && inv.cursor.Empty());

    PlayerInventory full;
    CHECK(full.Give(stone->networkId, 100) == 0);
    CHECK(full.main[0].count == 64 && full.main[1].count == 36);
    CHECK(full.Give(stone->networkId, 64 * 36) == 100);
    full.selected = 0;
    CHECK(full.TakeHeld(64) && full.main[0].Empty() && !full.TakeHeld(1));
}

static void TestAuth() {
    using namespace perlin::server;
    for (const std::string& k : auth::DefaultRootKeys()) {
        const std::string der = json::Base64UrlDecode(k);
        const unsigned char* p = reinterpret_cast<const unsigned char*>(der.data());
        EVP_PKEY* key = d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size()));
        CHECK(key != nullptr);
        EVP_PKEY_free(key);
    }

    testauth::Account alice("Alice", "2535400000000001");
    const std::vector<std::string> roots = {alice.root.spki};
    auto login = auth::VerifyLogin(alice.Identity(alice.Chain()), alice.ClientToken(), roots);
    CHECK(login.chainValid && login.xboxVerified && login.name == "Alice" &&
          login.xuid == "2535400000000001" && login.clientKey == alice.player.spki);
    CHECK(login.clientDataValid && login.clientData["SkinId"].String() == "s");

    testauth::Key stranger;
    login = auth::VerifyLogin(alice.Identity(alice.Chain()), alice.ClientToken(), {stranger.spki});
    CHECK(login.chainValid && !login.xboxVerified && login.xuid.empty());

    login = auth::VerifyLogin(alice.Identity(alice.Chain(false), 2), alice.ClientToken(), roots);
    CHECK(login.chainValid && !login.xboxVerified && login.name == "Alice" && login.xuid.empty());

    {
        testauth::Account mallory("Mallory", "999");
        const std::string lie = "{\"displayName\":\"Victim\",\"XUID\":\"42\",\"identity\":\"aaaaaaaa-bbbb-"
                                "cccc-dddd-eeeeeeeeeeee\"}";
        std::vector<std::string> chain = {testauth::Link(mallory.player, mallory.root, lie),
                                          testauth::Link(mallory.root, mallory.intermediate),
                                          testauth::Link(mallory.intermediate, mallory.player)};
        login = auth::VerifyLogin(mallory.Identity(chain), mallory.ClientToken(), {mallory.root.spki});
        CHECK(login.chainValid && !login.xboxVerified && login.xuid != "42");
    }

    {
        std::vector<std::string> chain = alice.Chain();
        std::string forged = chain[2];
        forged[forged.find('.') + 5] ^= 1;
        CHECK(!auth::VerifyLogin(alice.Identity({chain[0], chain[1], forged}), alice.ClientToken(), roots)
                   .chainValid);
        CHECK(!auth::VerifyLogin(alice.Identity({chain[0], chain[2], chain[1]}), alice.ClientToken(), roots)
                   .chainValid);
        testauth::Key other;
        CHECK(!auth::VerifyLogin(alice.Identity({chain[0], chain[1],
                                                 testauth::Link(other, alice.player,
                                                                "{\"displayName\":\"X\",\"XUID\":\"1\"}")}),
                                 alice.ClientToken(), roots)
                   .chainValid);
        testauth::Key thief;
        const std::string badClient = testauth::Jwt(thief, "{\"ThirdPartyName\":\"Alice\"}");
        CHECK(!auth::VerifyLogin(alice.Identity(chain), badClient, roots).chainValid);
        CHECK(!auth::VerifyLogin("not json", alice.ClientToken(), roots).chainValid);
        CHECK(!auth::VerifyLogin(alice.Identity({}), alice.ClientToken(), roots).chainValid);
    }

    {
        const int64_t now = testauth::Now();
        const std::string old =
            testauth::Jwt(alice.player, "{\"identityPublicKey\":\"" + alice.player.spki +
                                            "\",\"exp\":" + std::to_string(now - 7200) + "}");
        CHECK(!auth::VerifyLogin(alice.Identity({old}), alice.ClientToken(), roots).chainValid);
    }

    {
        testauth::Key authority;
        unsigned char point[97];
        size_t pointLen = 0;
        EVP_PKEY_get_octet_string_param(authority.pkey, OSSL_PKEY_PARAM_PUB_KEY, point, sizeof point, &pointLen);
        const std::string x = testauth::B64Url(std::string(reinterpret_cast<char*>(point) + 1, 48));
        const std::string y = testauth::B64Url(std::string(reinterpret_cast<char*>(point) + 49, 48));
        json::Value jwks;
        CHECK(json::Parse("{\"keys\":[{\"kty\":\"EC\",\"kid\":\"k1\",\"crv\":\"P-384\",\"x\":\"" + x +
                              "\",\"y\":\"" + y + "\"}]}",
                          jwks));
        auth::SetTokenKeys("https://issuer.example/", jwks);
        CHECK(auth::TokenKeysReady());
        const int64_t now = testauth::Now();
        const auto token = [&](const std::string& kid, const testauth::Key& signer) {
            const std::string h = testauth::B64Url("{\"alg\":\"ES384\",\"kid\":\"" + kid + "\"}");
            const std::string p = testauth::B64Url(
                "{\"cpk\":\"" + alice.player.spki + "\",\"xid\":\"2535400000000001\",\"xname\":\"Alice\","
                "\"aud\":\"api://auth-minecraft-services/multiplayer\",\"iss\":\"https://issuer.example\",\"nbf\":" +
                std::to_string(now - 60) + ",\"exp\":" + std::to_string(now + 3600) + "}");
            return h + "." + p + "." + testauth::B64Url(signer.Sign(h + "." + p));
        };
        const auto identity = [](const std::string& t) {
            return "{\"Certificate\":\"{\\\"chain\\\":[\\\"\\\"]}\",\"AuthenticationType\":2,\"Token\":\"" + t + "\"}";
        };
        auto login = auth::VerifyLogin(identity(token("k1", authority)), alice.ClientToken(), roots);
        CHECK(login.chainValid && login.xboxVerified && login.name == "Alice" && login.xuid == "2535400000000001" &&
              login.clientKey == alice.player.spki && login.clientDataValid && login.uuid.size() == 36);
        testauth::Key forger;
        login = auth::VerifyLogin(identity(token("k1", forger)), alice.ClientToken(), roots);
        CHECK(login.chainValid && !login.xboxVerified && login.xuid.empty() && login.name == "Alice");
        login = auth::VerifyLogin(identity(token("unknown", authority)), alice.ClientToken(), roots);
        CHECK(login.chainValid && !login.xboxVerified);
        testauth::Key thief;
        login = auth::VerifyLogin(identity(token("k1", authority)), testauth::Jwt(thief, "{\"ThirdPartyName\":\"Alice\"}"), roots);
        CHECK(!login.chainValid);
    }

    const std::string sdp = "v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\na=fingerprint:sha-256 "
                            "AB:CD:EF:01\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n";
    const std::string offer = testauth::AssertOffer(sdp, alice.player);
    std::string why;
    CHECK(auth::VerifyOfferBinding(offer, alice.player.spki, why));
    testauth::Key notHer;
    CHECK(!auth::VerifyOfferBinding(offer, notHer.spki, why));
    std::string moved = offer;
    moved.replace(moved.find("AB:CD"), 5, "00:00");
    CHECK(!auth::VerifyOfferBinding(moved, alice.player.spki, why));
    CHECK(!auth::VerifyOfferBinding(sdp, alice.player.spki, why));
}

static int LootCount(const std::vector<LootDrop>& drops, const std::string& item) {
    int n = 0;
    for (const LootDrop& d : drops)
        if (d.item == item)
            n += d.count;
    return n;
}

static void TestLoot() {
    BlockCatalog& c = BlockCatalog::Instance();
    std::string error;
    CHECK(c.Load(BlockCatalog::FindFile("", "block_states.json"), BlockCatalog::FindFile("", "block_destroy.tsv"), error,
                 BlockCatalog::FindFile("", "extra_items.txt")));
    const LootTables& loot = LootTables::Instance();
    std::mt19937 rng(7);
    const auto roll = [&](const std::string& bedrock, StateList states, const std::string& tool) {
        LootContext ctx;
        ctx.rng = &rng;
        ctx.tool = tool;
        ctx.block = LootTables::ViewOf(bedrock, states);
        return loot.Roll("blocks/" + ctx.block.java, ctx);
    };
    CHECK(LootTables::ViewOf("minecraft:stone", {}).java == "stone");
    CHECK(LootCount(roll("minecraft:stone", {}, "minecraft:wooden_pickaxe"), "minecraft:cobblestone") == 1);
    CHECK(LootCount(roll("minecraft:diamond_ore", {}, "minecraft:iron_pickaxe"), "minecraft:diamond") == 1);
    CHECK(!LootTables::CanHarvest("minecraft:diamond_ore", "minecraft:stone_pickaxe"));
    CHECK(LootTables::CanHarvest("minecraft:diamond_ore", "minecraft:iron_pickaxe"));
    CHECK(!LootTables::CanHarvest("minecraft:stone", ""));
    CHECK(LootTables::CanHarvest("minecraft:dirt", ""));
    const int lapis = LootCount(roll("minecraft:lapis_ore", {}, "minecraft:stone_pickaxe"), "minecraft:lapis_lazuli");
    CHECK(lapis >= 4 && lapis <= 9);
    StateList ripe = {{"growth", StateValue(int32_t{7})}};
    StateList young = {{"growth", StateValue(int32_t{2})}};
    CHECK(LootCount(roll("minecraft:wheat", ripe, ""), "minecraft:wheat") == 1);
    CHECK(LootCount(roll("minecraft:wheat", young, ""), "minecraft:wheat") == 0);
    CHECK(LootCount(roll("minecraft:wheat", young, ""), "minecraft:wheat_seeds") == 1);
    CHECK(LootCount(roll("minecraft:snow_layer", {{"height", StateValue(int32_t{3})}, {"covered_bit", StateValue(false)}},
                         "minecraft:wooden_shovel"),
                    "minecraft:snowball") == 4);
    CHECK(LootCount(roll("minecraft:oak_double_slab", {{"minecraft:vertical_half", StateValue(std::string("bottom"))}}, ""),
                    "minecraft:oak_slab") == 2);
    CHECK(LootCount(roll("minecraft:grass_block", {}, ""), "minecraft:dirt") == 1);
    CHECK(LootCount(roll("minecraft:glass", {}, ""), "minecraft:glass") == 0);
    int sticks = 0, leaves = 0;
    for (int i = 0; i < 400; i++) {
        const auto drops = roll("minecraft:oak_leaves", {}, "");
        sticks += LootCount(drops, "minecraft:stick");
        leaves += LootCount(drops, "minecraft:oak_leaves");
    }
    CHECK(sticks > 0 && leaves == 0);
    CHECK(LootCount(roll("minecraft:oak_leaves", {}, "minecraft:shears"), "minecraft:oak_leaves") == 1);
    LootContext cow;
    cow.rng = &rng;
    cow.onFire = true;
    int cooked = 0, raw = 0;
    for (int i = 0; i < 20; i++) {
        const auto drops = loot.Roll("entities/cow", cow);
        cooked += LootCount(drops, "minecraft:cooked_beef");
        raw += LootCount(drops, "minecraft:beef");
    }
    CHECK(cooked >= 20 && raw == 0);
    LootContext sheep;
    sheep.rng = &rng;
    CHECK(LootCount(loot.Roll("entities/sheep", sheep), "minecraft:white_wool") == 1);
    CHECK(LootTables::JavaEntityName("minecraft:zombie_pigman") == "zombified_piglin");
    CHECK(loot.Has("entities/zombified_piglin") && loot.Has("entities/zombie"));
    size_t missing = 0;
    std::string examples;
    for (const CatalogItem& item : c.Items()) {
        if (!item.block)
            continue;
        const LootBlock view = LootTables::ViewOf(item.name, {});
        if (view.java.starts_with("element_") || view.java.starts_with("hard_") || view.java.starts_with("light_block"))
            continue;
        if (!loot.Has("blocks/" + view.java)) {
            missing++;
            if (missing <= 12)
                examples += " " + item.name;
        }
    }
    std::printf("loot: %zu placeable blocks without a java table (%s )\n", missing, examples.c_str());
    CHECK(missing < 24);
    std::set<std::string> unresolved;
    for (int i = 0; i < kLootSourceCount; i++) {
        const std::string text = kLootSources[i].json;
        size_t at = 0;
        while ((at = text.find("\"name\":\"", at)) != std::string::npos) {
            at += 8;
            const size_t end = text.find('"', at);
            const std::string name = text.substr(at, end - at);
            if (LootTables::BedrockItemName(name).empty())
                unresolved.insert(name);
            at = end;
        }
    }
    std::printf("loot: %zu java item names have no item in this block catalog\n", unresolved.size());
    for (const char* name : {"minecraft:diamond", "minecraft:string", "minecraft:rotten_flesh", "minecraft:cobweb",
                             "minecraft:bricks", "minecraft:terracotta", "minecraft:snow_block", "minecraft:melon"})
        CHECK(!LootTables::BedrockItemName(name).empty());
    ItemStack worn;
    worn.networkId = c.ItemByName("minecraft:diamond_sword")->networkId;
    worn.count = 1;
    worn.damage = 17;
    const auto wire = ToWire(worn);
    const auto* extra = std::get_if<2>(&wire.extra);
    CHECK(extra && extra->nbt && extra->nbt->nbt.bytes.size() == 17 && extra->nbt->nbt.bytes[12] == 17);
    CHECK(MaxDurability(worn.networkId) == 1561);
    CHECK(ArmorPoints(c.ItemByName("minecraft:diamond_chestplate")->networkId) == 8);
    CHECK(ArmorPoints(c.ItemByName("minecraft:leather_boots")->networkId) == 1);
}

int main(int argc, char** argv) {
    TestJson();
    TestBlockHash();
    TestAuth();
    TestBlockCatalog();
    TestInventory();
    TestLoot();
    TestFlatGeneration();
    TestChunkEncodingDecodesWithClientCodec();
    TestBatchCodec();
    TestFragments();
    TestConfig();
    TestProperties();
    TestAccessFiles();
    TestPluginHost(argc > 1 ? argv[1] : nullptr);
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("server_selftest ok");
    return 0;
}
