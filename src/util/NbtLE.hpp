#pragma once
// Little-endian NBT, as Bedrock keeps it on disk (level.dat). Enough to read a world's summary and to change
// a field and write the file back without touching anything else in it.
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace perlin::nbtle {
    enum Type : uint8_t {
        End = 0,
        Byte = 1,
        Short = 2,
        Int = 3,
        Long = 4,
        Float = 5,
        Double = 6,
        ByteArray = 7,
        String = 8,
        List = 9,
        Compound = 10,
        IntArray = 11,
        LongArray = 12
    };

    struct Tag {
        Type type = End;
        int64_t integer = 0;              // Byte, Short, Int, Long
        double real = 0;                  // Float, Double
        std::string text;                 // String
        std::vector<int64_t> numbers;     // ByteArray, IntArray, LongArray
        Type listType = End;              // List
        std::vector<Tag> items;           // List
        std::vector<std::pair<std::string, Tag>> fields; // Compound, in file order

        const Tag* Find(const std::string& name) const;
        Tag* Find(const std::string& name);
        int64_t Int(const std::string& name, int64_t fallback) const;
        std::string Str(const std::string& name, const std::string& fallback = {}) const;
        void SetString(const std::string& name, const std::string& value);
    };

    // A root compound: its name (usually empty) and the tag.
    bool Read(std::span<const uint8_t> bytes, Tag& root, std::string* rootName = nullptr);
    std::vector<uint8_t> Write(const Tag& root, const std::string& rootName = {});

    // level.dat: an 8-byte header (storage version, body length) then the root compound.
    bool ReadLevelDat(const std::string& path, Tag& root, uint32_t& storageVersion);
    bool WriteLevelDat(const std::string& path, const Tag& root, uint32_t storageVersion);
}
