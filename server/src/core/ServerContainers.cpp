#include "core/Server.hpp"

#include "inventory/Recipes.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"

#include <algorithm>

namespace perlin::server {
    namespace {
        int64_t Key(int x, int y, int z, int dimension) {
            return (static_cast<int64_t>(x) & 0x3FFFFFF) << 38 | (static_cast<int64_t>(z) & 0x3FFFFFF) << 12 |
                   static_cast<int64_t>(dimension & 3) << 10 | static_cast<int64_t>((y + 512) & 1023);
        }

        std::string Bare(const std::string& n) { return n.starts_with("minecraft:") ? n.substr(10) : n; }

        proto::ContainerSlotType SlotType(int kind) {
            switch (kind) {
            case 1:
                return proto::ContainerSlotType::Barrel;
            case 2:
                return proto::ContainerSlotType::FurnaceIngredient;
            case 3:
                return proto::ContainerSlotType::BlastFurnaceIngredient;
            case 4:
                return proto::ContainerSlotType::SmokerIngredient;
            default:
                return proto::ContainerSlotType::Container;
            }
        }

        proto::WindowType WindowOf(int kind) {
            switch (kind) {
            case 2:
                return proto::WindowType::Furnace;
            case 3:
                return proto::WindowType::BlastFurnace;
            case 4:
                return proto::WindowType::Smoker;
            case 5:
                return proto::WindowType::Workbench;
            default:
                return proto::WindowType::Container;
            }
        }

        std::string ItemName(int16_t id) {
            const CatalogItem* item = BlockCatalog::Instance().ItemById(id);
            return item ? item->name : std::string();
        }
    }

    int Server::ContainerKindOf(const std::string& blockName) {
        const std::string n = Bare(blockName);
        if (n == "chest" || n == "trapped_chest")
            return 0;
        if (n == "barrel")
            return 1;
        if (n == "furnace" || n == "lit_furnace")
            return 2;
        if (n == "blast_furnace" || n == "lit_blast_furnace")
            return 3;
        if (n == "smoker" || n == "lit_smoker")
            return 4;
        if (n == "crafting_table")
            return 5;
        return -1;
    }

    bool Server::UseBlock(Session& by, int x, int y, int z) {
        const int dim = by.DimensionId();
        ChunkManager& world = World(dim);
        if (by.Spectator() || by.Visitor() || !by.InReach(x, y, z) || !world.Peek({x >> 4, z >> 4}))
            return false;
        const std::string name = BlockRegistry::Instance().Name(world.GetBlock(x, y, z));
        const int kind = ContainerKindOf(name);
        if (kind < 0)
            return false;
        if (HookContainerOpen(by, x, y, z, name))
            return true;
        CloseContainer(by);
        static int windowCounter = 0;
        const int window = 1 + (windowCounter++ % 99);
        if (kind == 5) {
            by.Inventory().containerKind = -1;
            by.Inventory().container.clear();
            by.OpenWindow(window, WindowOf(kind), x, y, z);
            return true;
        }
        auto [it, fresh] = containers.try_emplace(Key(x, y, z, dim));
        BlockContainer& c = it->second;
        if (fresh) {
            c.kind = kind;
            c.dimension = dim;
            c.x = x;
            c.y = y;
            c.z = z;
            c.slots.assign(kind >= 2 ? 3 : 27, ItemStack{});
        }
        by.Inventory().containerKind = kind;
        by.Inventory().container = c.slots;
        by.openContainer = Key(x, y, z, dim);
        by.OpenWindow(window, WindowOf(kind), x, y, z);
        by.SendWindowContents(SlotType(kind), c.slots);
        if (kind >= 2) {
            by.SendWindowData(0, c.cook);
            by.SendWindowData(1, c.burn);
            by.SendWindowData(2, c.burnMax);
        }
        return true;
    }

    void Server::SyncContainer(Session& by) {
        const auto it = containers.find(by.openContainer);
        if (it == containers.end())
            return;
        it->second.slots = by.Inventory().container;
        for (Session* s : InGamePlayers())
            if (s != &by && s->openContainer == by.openContainer) {
                s->Inventory().container = it->second.slots;
                s->SendWindowContents(SlotType(it->second.kind), it->second.slots);
            }
    }

    void Server::CloseContainer(Session& by) {
        PlayerInventory& inv = by.Inventory();
        bool gave = false;
        for (ItemStack& s : inv.craft)
            if (!s.Empty()) {
                const int left = inv.Give(s);
                if (left > 0) {
                    ItemStack rest = s;
                    rest.count = static_cast<uint16_t>(left);
                    DropFromPlayer(by, rest);
                }
                s = ItemStack{};
                gave = true;
            }
        if (inv.containerKind >= 0) {
            SyncContainer(by);
            inv.containerKind = -1;
            inv.container.clear();
        }
        by.openContainer = INT64_MIN;
        by.ClearWindow();
        if (gave)
            by.SendInventory();
    }

    void Server::DropContainer(int dimension, int x, int y, int z) {
        const auto it = containers.find(Key(x, y, z, dimension));
        if (it == containers.end())
            return;
        for (Session* s : InGamePlayers())
            if (s->openContainer == it->first) {
                s->Inventory().containerKind = -1;
                s->Inventory().container.clear();
                s->openContainer = INT64_MIN;
                s->CloseWindowFromServer();
            }
        for (const ItemStack& stack : it->second.slots)
            if (!stack.Empty())
                SpawnItem(stack, x + 0.5, y + 0.5, z + 0.5, 0.0, 0.15, 0.0, 10, dimension);
        containers.erase(it);
    }

    void Server::PushContainer(BlockContainer& c, bool slotsChanged) {
        const int64_t key = Key(c.x, c.y, c.z, c.dimension);
        for (Session* s : InGamePlayers())
            if (s->openContainer == key) {
                if (slotsChanged) {
                    s->Inventory().container = c.slots;
                    s->SendWindowContents(SlotType(c.kind), c.slots);
                }
                s->SendWindowData(0, c.cook);
                s->SendWindowData(1, c.burn);
                s->SendWindowData(2, c.burnMax);
            }
    }

    void Server::TickContainers() {
        for (auto& [key, c] : containers) {
            if (c.kind < 2)
                continue;
            const int station = c.kind - 2;
            const int cookTime = station == 0 ? 200 : 100;
            ItemStack& in = c.slots[0];
            ItemStack& fuel = c.slots[1];
            ItemStack& out = c.slots[2];
            const SmeltRecipe* recipe = in.Empty() ? nullptr : Recipes::Instance().Smelt(ItemName(in.networkId), station);
            const bool room = recipe && (out.Empty() || (out.networkId == recipe->outputId &&
                                                          out.count + recipe->outputCount <= MaxStack(out.networkId)));
            bool changed = false;
            const bool wasLit = c.burn > 0;
            if (c.burn > 0)
                c.burn--;
            if (c.burn == 0 && room && !fuel.Empty()) {
                const std::string fuelName = ItemName(fuel.networkId);
                const int ticks = Recipes::FuelTicks(fuelName) / (station == 0 ? 1 : 2);
                if (ticks > 0) {
                    c.burn = c.burnMax = ticks;
                    if (Bare(fuelName) == "lava_bucket") {
                        if (const CatalogItem* bucket = BlockCatalog::Instance().ItemByName("minecraft:bucket"))
                            fuel.networkId = bucket->networkId;
                    } else if (--fuel.count == 0) {
                        fuel = ItemStack{};
                    }
                    changed = true;
                }
            }
            if (c.burn > 0 && room) {
                if (++c.cook >= cookTime) {
                    c.cook = 0;
                    if (out.Empty()) {
                        out.networkId = recipe->outputId;
                        out.count = static_cast<uint16_t>(recipe->outputCount);
                    } else {
                        out.count = static_cast<uint16_t>(out.count + recipe->outputCount);
                    }
                    if (--in.count == 0)
                        in = ItemStack{};
                    changed = true;
                }
            } else if (c.cook > 0) {
                c.cook = std::max(0, c.cook - 2);
            }
            const bool lit = c.burn > 0;
            ChunkManager* world = LoadedWorld(c.dimension);
            if (lit != wasLit && world && world->Peek({c.x >> 4, c.z >> 4})) {
                const uint16_t block = world->GetBlock(c.x, c.y, c.z);
                std::string text = BlockCatalog::Instance().StateText(block);
                const std::string name = text.substr(0, text.find('['));
                const std::string bare = Bare(name);
                std::string target = bare;
                if (lit && !bare.starts_with("lit_"))
                    target = "lit_" + bare;
                if (!lit && bare.starts_with("lit_"))
                    target = bare.substr(4);
                if (target != bare)
                    ApplyBlock(c.dimension, c.x, c.y, c.z, BlockCatalog::Instance().Resolve("minecraft:" + target + text.substr(name.size())));
            }
            if (changed)
                for (ItemStack& s : c.slots)
                    if (!s.Empty())
                        s.stackId = static_cast<int32_t>(tick & 0x7FFFFFFF) + static_cast<int32_t>(&s - c.slots.data());
            if (changed || (lit && tick % 5 == 0) || lit != wasLit)
                PushContainer(c, changed);
        }
    }
}
