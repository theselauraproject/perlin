#include "client/systems/entity/CapeGeometry.hpp"

#include "util/JsonValue.hpp"
#include "util/ResourcePaths.hpp"
#include "util/Resources.hpp"

namespace perlin::entity {
    bool LoadCapeGeometryText(attachable::GeometryLibrary& library, const std::string& text,
                              const char* expectedId, std::string& error) {
        json::Value root;
        std::string parseError;
        if (!json::Value::Parse(text, root, parseError)) {
            error = "cape geometry is not valid JSON: " + parseError;
            return false;
        }
        const json::Value* list = root.Find("minecraft:geometry");
        if (!list || !list->IsArray() || list->array.empty()) {
            error = "cape geometry has no minecraft:geometry array";
            return false;
        }
        bool found = false;
        for (const json::Value& entry : list->array) {
            const json::Value* description = entry.Find("description");
            const json::Value* id = description ? description->Find("identifier") : nullptr;
            const json::Value* bones = entry.Find("bones");
            if (!id || !id->IsString() || !bones || !bones->IsArray() || bones->array.empty()) {
                error = "cape geometry entry lacks an identifier or bones";
                return false;
            }
            if (id->string == expectedId)
                found = true;
        }
        if (!found) {
            error = std::string("cape geometry does not define ") + expectedId;
            return false;
        }
        library.Load(text);
        if (!library.Find(expectedId)) {
            error = std::string("cape geometry ") + expectedId + " failed to build";
            return false;
        }
        return true;
    }

    bool LoadCapeGeometries(attachable::GeometryLibrary& library, std::string& error) {
        return LoadCapeGeometryText(library, resources::ReadText(resources::path::kCapeGeometryPlain),
                                    kCapeGeometryPlainId, error) &&
               LoadCapeGeometryText(library, resources::ReadText(resources::path::kCapeGeometryShoulder),
                                    kCapeGeometryShoulderId, error);
    }
}
