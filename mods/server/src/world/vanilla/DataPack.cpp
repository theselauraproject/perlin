#include "world/vanilla/DataPack.hpp"

#include <miniz.h>

namespace perlin::server::vanilla {
    extern const uint8_t kOverworldShapeArchive[];
    extern const size_t kOverworldShapeArchiveSize;

    struct DataPack::Impl {
        mz_zip_archive zip{};
        std::map<std::string, mz_uint> entries;
    };

    DataPack::~DataPack() {
        if (impl)
            mz_zip_reader_end(&impl->zip);
    }

    std::unique_ptr<DataPack> DataPack::Embedded() {
        auto pack = std::unique_ptr<DataPack>(new DataPack());
        pack->impl = std::make_unique<Impl>();
        // the archive is compiled in and checked by the tests; an unreadable one is a broken build, which shows up as every lookup missing
        if (!mz_zip_reader_init_mem(&pack->impl->zip, kOverworldShapeArchive, kOverworldShapeArchiveSize, 0))
            return pack;
        const std::string prefix = "data/minecraft/";
        const mz_uint count = mz_zip_reader_get_num_files(&pack->impl->zip);
        for (mz_uint i = 0; i < count; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&pack->impl->zip, i, &st) || st.m_is_directory)
                continue;
            const std::string name = st.m_filename;
            if (name.starts_with(prefix))
                pack->impl->entries[name.substr(prefix.size())] = i;
        }
        return pack;
    }

    bool DataPack::Has(const std::string& relative) const {
        return impl->entries.contains(relative);
    }

    std::string DataPack::Read(const std::string& relative) const {
        const auto it = impl->entries.find(relative);
        if (it == impl->entries.end())
            return {};
        size_t size = 0;
        void* data =
            mz_zip_reader_extract_to_heap(const_cast<mz_zip_archive*>(&impl->zip), it->second, &size, 0);
        if (!data)
            return {};
        std::string out(static_cast<const char*>(data), size);
        mz_free(data);
        return out;
    }

    std::vector<std::string> DataPack::List(const std::string& prefix) const {
        std::vector<std::string> out;
        for (const auto& [name, index] : impl->entries)
            if (name.starts_with(prefix))
                out.push_back(name);
        return out;
    }
}
