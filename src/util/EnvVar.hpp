#pragma once
#include <cstdlib>
#include <string>

namespace perlin::env {
    inline bool Set(const std::string& name, const std::string& value) {
#ifdef _WIN32
        return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
        return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
    }

    inline bool Unset(const std::string& name) {
#ifdef _WIN32
        return _putenv_s(name.c_str(), "") == 0;
#else
        return unsetenv(name.c_str()) == 0;
#endif
    }
}
