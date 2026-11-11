#pragma once
#include <miniz.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace perlin {
    inline bool OpenZipReader(mz_zip_archive& zip, const std::filesystem::path& path, FILE*& file) {
        file = nullptr;
#ifdef _WIN32
        file = _wfopen(path.c_str(), L"rb");
#else
        file = std::fopen(path.c_str(), "rb");
#endif
        if (file) {
            zip = {};
            if (mz_zip_reader_init_cfile(&zip, file, 0, 0))
                return true;
            std::fclose(file);
            file = nullptr;
        }
        zip = {};
        return mz_zip_reader_init_file(&zip, path.string().c_str(), 0) != 0;
    }
}
