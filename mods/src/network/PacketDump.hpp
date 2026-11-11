#pragma once
#include <glaze/glaze.hpp>

#include <string>

namespace perlin::net {

    template <class P> std::string Dump(const P& packet) {
        std::string out;
        if (glz::write<glz::opts{.prettify = true}>(packet, out))
            return P::kName;
        return out;
    }

}
