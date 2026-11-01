#pragma once
#include "client/attachable/Animation.hpp"
#include "client/resources/TextureLoader.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::attachable {
    struct Quad {
        glm::vec3 p[4];
        glm::vec2 uv[4];
        glm::vec3 n;
    };

    struct Cube {
        glm::vec3 origin{0.0f}, size{0.0f};
        glm::vec3 pivot{0.0f}, rotation{0.0f};
        float inflate = 0;
        bool mirror = false;
        bool hasRotation = false;
        bool boxUv = true;
        glm::vec2 uv{0.0f};
        struct FaceUv {
            bool present = false;
            glm::vec2 uv{0.0f}, size{0.0f};
            int rotation = 0; // uv_rotation in quarter turns, clockwise
        } faces[6];
    };

    struct TextureMesh {
        std::string texture;
        glm::vec3 position{0.0f}, rotation{0.0f}, scale{1.0f}, localPivot{0.0f};
    };

    struct Bone {
        std::string name;
        int parent = -1;
        glm::vec3 pivot{0.0f}, rotation{0.0f};
        std::vector<Cube> cubes;
        std::vector<TextureMesh> meshes;
        std::vector<Quad> voxels;
        // poly_mesh polygons in model space; triangles repeat their last corner. UVs are in texels unless polyNormalized.
        std::vector<Quad> polys;
        bool polyNormalized = true;
        std::string binding;
        std::string parentName;
        float inflate = 0;
        bool neverRender = false;
        bool reset = false, hasInflate = false, hasNeverRender = false;
    };

    struct Geometry {
        std::string id;
        std::string parentId;
        bool linked = false;
        float textureWidth = 64, textureHeight = 64;
        std::vector<Bone> bones;
    };

    class GeometryLibrary {
    public:
        void Load(const std::string& text);
        const Geometry* Find(const std::string& id) const;
        size_t Size() const { return geometries.size(); }
        void Detach(const std::string& id, const std::vector<std::string>& bones);

    private:
        void Link();
        std::unordered_map<std::string, Geometry> geometries;
    };

    struct BuiltMesh {
        std::vector<Quad> body;
        struct Extra {
            std::string texture;
            std::vector<Quad> quads;
        };
        std::vector<Extra> extras;
    };
    using TextureLookup = std::function<const Texture*(const std::string& shortName)>;
    using BoneFilter = std::function<bool(const std::string&)>;
    // model-space world matrices of a posed geometry's bones, by (lower case) name
    using BoneWorlds = std::unordered_map<std::string, glm::mat4>;
    // attach: bones whose name is in it take that world matrix as is instead of their own parent chain, so worn
    // geometry (armour) follows the wearer's posed parts exactly whatever hierarchy its own file uses
    void BuildMesh(const Geometry& geometry, const Pose& pose, const TextureLookup& textures, BuiltMesh& out,
                   const BoneFilter& visible = nullptr, const glm::mat4* rootParent = nullptr,
                   bool mirrorTextures = false, const BoneWorlds* attach = nullptr);
    void ComputeBoneWorlds(const Geometry& geometry, const Pose& pose, BoneWorlds& out);

    Geometry VoxelizeSkinLayers(const Geometry& source, const uint8_t* rgba, int width, int height);
    std::vector<Quad> ExtrudeSprite(const Texture& atlas, int tileX, int tileY, int tileWidth, int tileHeight,
                                    float width, float height, float depth);

    glm::mat4 EulerZYX(const glm::vec3& degrees);

    bool BoneWorld(const Geometry& geometry, const Pose& pose, const std::string& lowerName, glm::mat4& world,
                   glm::vec3& pivot);
}
