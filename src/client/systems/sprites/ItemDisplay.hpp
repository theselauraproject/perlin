#pragma once
#include <string>
#include <string_view>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace perlin::itemdisplay {

    // One ItemTransform of a Java item model: rotation in degrees, translation in sixteenths of a block.
    struct Transform {
        glm::vec3 rotation{0.0f}, translation{0.0f}, scale{1.0f};
        bool present = false;
    };

    // Which items/*.json branch to read: the hand (first and third person), a dropped item, the GUI, or
    // the hand while the item is in use (minecraft:using_item, e.g. the raised shield_blocking model).
    enum class Context { Hand, Ground, Gui, HandUse };

    struct Entry {
        enum class Kind { Flat, Block, Special } kind = Kind::Flat;
        // The special renderer's type ("shield", "chest", "banner", ...) for Kind::Special.
        std::string special;
        std::string model;
        Transform firstRight, firstLeft, thirdRight, thirdLeft, ground, head, fixed, gui;
        // The items/*.json "transformation"s on the path to this model, multiplied after the display
        // transform (ItemStackRenderState.LayerRenderState.applyTransform).
        glm::mat4 local{1.0f};
        bool hasLocal = false;
        // Arguments of the special renderer ("texture", "color", "kind", ...), without namespaces.
        std::unordered_map<std::string, std::string> args;
        // gui_light: true for "front" (flat lit), false for "side" (3D lit).
        bool frontLight = false;
        bool found = false;

        const Transform& FirstPerson(bool leftHand) const { return leftHand ? firstLeft : firstRight; }
        const Transform& ThirdPerson(bool leftHand) const { return leftHand ? thirdLeft : thirdRight; }
        std::string Arg(const std::string& key, const std::string& fallback = {}) const {
            const auto it = args.find(key);
            return it == args.end() ? fallback : it->second;
        }
    };

    // The Java item id for a Bedrock item id (without namespace). Most ids are shared; this maps the ones
    // Bedrock names differently (snow is Java's snow_block, snow_layer is Java's snow, stone_stairs is
    // cobblestone_stairs, ...) and the aux-valued legacy ids (bed, banner, skull) when `aux` is known.
    std::string JavaItemName(std::string_view name, int aux = -1);

    // `name` may be a Bedrock id, with or without namespace; it goes through JavaItemName.
    const Entry& Lookup(const std::string& name, int aux, Context context);
    inline const Entry& Lookup(const std::string& name, Context context) { return Lookup(name, -1, context); }
    inline const Entry& Lookup(const std::string& name, bool ground) {
        return Lookup(name, -1, ground ? Context::Ground : Context::Hand);
    }

    // translate * rotationXYZ * scale, mirrored for the left hand (x translation, y and z rotation
    // negated); without the -0.5 recentring.
    inline glm::mat4 Apply(const Transform& t, bool leftHand) {
        if (!t.present)
            return glm::mat4(1.0f);
        constexpr float k = 3.14159265358979323846f / 180.0f;
        const float side = leftHand ? -1.0f : 1.0f;
        glm::mat4 m = glm::translate(
            glm::mat4(1.0f), glm::vec3(side * t.translation.x, t.translation.y, t.translation.z) / 16.0f);
        m = glm::rotate(m, t.rotation.x * k, glm::vec3(1, 0, 0));
        m = glm::rotate(m, side * t.rotation.y * k, glm::vec3(0, 1, 0));
        m = glm::rotate(m, side * t.rotation.z * k, glm::vec3(0, 0, 1));
        return glm::scale(m, t.scale);
    }

    // Exactly ItemTransform.apply: the transform (identity when absent) followed by translate(-0.5).
    inline glm::mat4 Pose(const Transform& t, bool leftHand) {
        return glm::translate(Apply(t, leftHand), glm::vec3(-0.5f));
    }

    // Pose(t) followed by the entry's local transformation: maps model space (0..1 block) to the
    // display context's space.
    inline glm::mat4 ModelPose(const Entry& e, const Transform& t, bool leftHand) {
        const glm::mat4 pose = Pose(t, leftHand);
        return e.hasLocal ? pose * e.local : pose;
    }
}
