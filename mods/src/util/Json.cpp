#include "util/Json.hpp"

namespace perlin::json {

    std::optional<Document> Document::Parse(std::string_view text, bool comments) {
        Document document;
        const auto error =
            comments
                ? glz::read<glz::opts{.comments = true, .error_on_unknown_keys = false}>(document.value, text)
                : glz::read<glz::opts{.error_on_unknown_keys = false}>(document.value, text);
        if (error)
            return std::nullopt;
        return document;
    }

}
