#pragma once

#include "world/level/Level.hpp"

#include <cstdint>
#include <unordered_map>

namespace perlin {
    // Owned by the chunk commit thread/mutex. A ticket is assigned when input is
    // received, and travels with deferred blob data instead of being renewed when
    // a slow download finishes.
    class ChunkLoadOrder {
    public:
        struct Ticket {
            uint64_t generation = 0;
            uint64_t column = 0;
            uint64_t revision = 0;
            int x = 0, y = 0, z = 0;
        };

        void Synchronize(uint64_t currentGeneration) {
            if (generation == currentGeneration)
                return;
            generation = currentGeneration;
            columns.clear();
        }

        Ticket BeginColumn(int x, int z) {
            auto& column = columns[{x, z}];
            column.revision = ++nextRevision;
            column.slots.clear();
            return {generation, column.revision, column.revision, x, 0, z};
        }

        Ticket BeginSlot(int x, int y, int z) {
            auto& column = columns[{x, z}];
            if (!column.revision)
                column.revision = ++nextRevision;
            const uint64_t revision = ++nextRevision;
            column.slots[y] = revision;
            return {generation, column.revision, revision, x, y, z};
        }

        void ForgetColumn(int x, int z) { columns.erase({x, z}); }
        uint64_t NextEditRevision() { return ++nextRevision; }

        bool CurrentColumn(const Ticket& ticket) const {
            const auto column = columns.find({ticket.x, ticket.z});
            return ticket.generation == generation && column != columns.end() &&
                   column->second.revision == ticket.column;
        }

        bool CurrentSlot(const Ticket& ticket) const {
            if (!CurrentColumn(ticket))
                return false;
            const auto& slots = columns.find({ticket.x, ticket.z})->second.slots;
            const auto slot = slots.find(ticket.y);
            return slot != slots.end() && slot->second == ticket.revision;
        }

        bool MayWriteSection(const Ticket& ticket, int y) const {
            if (!CurrentColumn(ticket))
                return false;
            const auto& slots = columns.find({ticket.x, ticket.z})->second.slots;
            const auto slot = slots.find(y);
            return slot == slots.end() || slot->second <= ticket.revision;
        }

        void RecordSectionWrite(const Ticket& ticket, int y) {
            if (!CurrentColumn(ticket))
                return;
            auto& revision = columns.find({ticket.x, ticket.z})->second.slots[y];
            if (revision < ticket.revision)
                revision = ticket.revision;
        }

    private:
        struct Column {
            uint64_t revision = 0;
            std::unordered_map<int, uint64_t> slots;
        };
        uint64_t generation = 0;
        uint64_t nextRevision = 0;
        std::unordered_map<ChunkPos, Column, ChunkPosHash> columns;
    };
}
