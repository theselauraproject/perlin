#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <map>
#include <unordered_set>
#include <vector>

namespace perlin {

    class BlockPalette {
    public:
        static constexpr size_t kMaxLocal = 65536;

        static BlockPalette& Instance();

        static int32_t AirHash();
        struct StateValue {
            enum Kind { Bool, Int, String } kind = Bool;
            int32_t i = 0;
            std::string s;
        };
        static int32_t HashState(const std::string& name,
                                 const std::vector<std::pair<std::string, StateValue>>& sortedStates);

        bool LoadVanillaDefinitions(const std::string& mojangBlocksJsonPath);
        bool LoadServerStates(const std::string& path);
        bool LoadServerStatesText(const std::string& text, const std::string& source);
        bool NamesLoaded() const { return namesLoaded; }
        std::vector<std::pair<int32_t, std::string>> OneStatePerName() const;
        int32_t FindHash(const std::string& fullName, const std::string& key,
                         const std::string& valueText) const;
        size_t StateCount() const { return nameByHash.size(); }

        uint16_t Intern(int32_t networkId);
        void SetSequential(bool on);
        bool Sequential() const { return sequential.load(std::memory_order_acquire); }
        void SetSequentialAir(int32_t runtimeId) {
            sequentialAir.store(runtimeId, std::memory_order_release);
        }
        int32_t SequentialAir() const { return sequentialAir.load(std::memory_order_acquire); }
        void AppendSequence(int32_t hash);
        void TruncateSequence();
        bool LoadVersionDeltas(const std::string& text);
        void UseProtocol(int protocol);
        int SequenceProtocol() const { return sequenceProtocol; }
        bool SequenceLoaded() const { return !baseSequence.empty(); }
        static uint64_t NameSortKey(const std::string& name);
        struct CustomRun {
            std::string name;
            std::vector<int32_t> hashes;
        };
        void InsertCustomRuns(std::vector<CustomRun> runs);
        bool CalibrateSequence(int32_t serverAirId);
        bool SequenceVerified() const {
            std::lock_guard<std::mutex> lk(mutex);
            return layoutVerified;
        }
        uint16_t InternRuntime(int32_t runtimeId);
        int32_t RuntimeId(uint16_t local) const;
        bool LearnName(int32_t networkId, const std::string& name);
        void LearnState(int32_t networkId, const std::string& name,
                        std::vector<std::pair<std::string, StateValue>> states);
        int32_t NetworkId(uint16_t local) const;
        std::string Name(uint16_t local) const;
        std::vector<std::pair<std::string, StateValue>> States(uint16_t local) const;
        int StateInt(uint16_t local, const std::string& key, int fallback) const;
        std::string StateString(uint16_t local, const std::string& key, const std::string& fallback) const;
        bool HasState(uint16_t local, const std::string& key) const;
        size_t Count() const { return count.load(std::memory_order_acquire); }

        std::vector<std::string> AllNames() const;
        std::string ResourcePackKey(const std::string& fullName) const;
        bool BlockHasProperty(const std::string& fullName, const std::string& prop) const;
        std::vector<std::string> BlockProperties(const std::string& fullName) const;

    private:
        BlockPalette();
        std::atomic<int32_t> sequentialAir{-1};

        std::atomic<bool> namesLoaded{false};
        std::mutex loadMutex;
        std::unordered_map<int32_t, std::string> nameByHash;
        std::unordered_map<int32_t, std::vector<std::pair<std::string, StateValue>>> statesByHash;
        std::unordered_map<std::string, std::string> rpKeyByName;
        std::unordered_map<std::string, std::vector<std::string>> propsByName;

        mutable std::mutex mutex;
        mutable std::mutex learnedMutex;
        std::unordered_map<int32_t, std::string> learned;
        std::unordered_map<int32_t, std::vector<std::pair<std::string, StateValue>>> learnedStates;
        std::unordered_map<int32_t, uint16_t> localByNet;
        std::atomic<bool> sequential{false};
        std::vector<int32_t> sequence;
        std::unordered_map<int32_t, int32_t> sequenceIndex;
        bool sequenceFrozen = false;
        size_t vanillaSequence = 0;
        std::vector<int32_t> baseSequence;
        std::unordered_set<int32_t> reservedHashes;
        std::unordered_set<int32_t> dataDrivenHashes;
        std::unordered_set<int32_t> versionHidden;
        struct VersionDelta {
            int before = 0;
            int protocol = 0;
            std::unordered_set<std::string> removed;
            std::map<std::string, std::vector<std::pair<std::string, StateValue>>> pinned;
        };
        std::vector<VersionDelta> versionDeltas;
        bool versionDeltasLoaded = false;
        int sequenceProtocol = 0;
        std::vector<uint64_t> baseKeys, sequenceKeys;
        std::vector<int32_t> netByLocal;
        std::vector<CustomRun> customRuns;
        std::unordered_set<std::string> keptDataDriven;
        bool layoutVerified = false;
        bool layoutAppended = false;
        void BuildLayoutLocked(bool appended);
        std::atomic<size_t> count{1};
    };

}
