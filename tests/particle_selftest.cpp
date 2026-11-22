#include "client/systems/particle/DataParticles.hpp"
#include "client/systems/particle/TerrainTile.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {
    int failures = 0;

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }

    void CheckTerrainTiles() {
        using perlin::particle::TerrainTileFor;
        perlin::BlockAppearance grass;
        grass.top = 10;
        grass.side = 11;
        grass.bottom = 12;
        grass.overlayTile = 13;
        grass.overlayFrac = 1.0f;
        Check(TerrainTileFor(grass, 1, true).tile == 10, "grass top face uses the top texture");
        Check(TerrainTileFor(grass, 1, true).tintable, "grass top face takes the biome tint");
        for (int face : {2, 3, 4, 5}) {
            Check(TerrainTileFor(grass, face, true).tile == 12, "grass side face uses dirt");
            Check(!TerrainTileFor(grass, face, true).tintable, "grass side face is not tinted");
        }
        Check(TerrainTileFor(grass, 0, true).tile == 12, "grass bottom face uses dirt");
        Check(!TerrainTileFor(grass, 0, true).tintable, "grass bottom face is not tinted");
        Check(TerrainTileFor(grass, -1, true).tile == 10, "grass general texture stays the top");

        perlin::BlockAppearance log;
        log.top = 20;
        log.side = 21;
        log.bottom = 22;
        Check(TerrainTileFor(log, 1, false).tile == 20, "log top face");
        Check(TerrainTileFor(log, 0, false).tile == 22, "log bottom face");
        Check(TerrainTileFor(log, 2, false).tile == 21, "log side face");
        Check(TerrainTileFor(log, -1, false).tile == 21, "log general texture is the side");

        perlin::BlockAppearance furnace;
        furnace.top = 30;
        furnace.side = 31;
        furnace.bottom = 30;
        furnace.sideTile[0] = 32;
        furnace.sideTile[3] = 33;
        Check(TerrainTileFor(furnace, 5, false).tile == 32, "east face uses its own texture");
        Check(TerrainTileFor(furnace, 2, false).tile == 33, "north face uses its own texture");
        Check(TerrainTileFor(furnace, 3, false).tile == 31, "south face falls back to the side");
        Check(TerrainTileFor(furnace, 4, false).tile == 31, "west face falls back to the side");

        perlin::BlockAppearance topOnly;
        topOnly.top = 40;
        Check(TerrainTileFor(topOnly, 3, false).tile == 40, "missing face texture falls back");
    }
}

int main() {
    CheckTerrainTiles();
    const fs::path root = fs::temp_directory_path() / "perlin_particle_selftest";
    fs::remove_all(root);
    fs::create_directories(root / "particles");
    std::ofstream(root / "particles" / "burst.json") << R"({
  "format_version": "1.10.0",
  "particle_effect": {
    "description": {
      "identifier": "test:burst",
      "basic_render_parameters": {"material": "particles_blend", "texture": "textures/particle/test"}
    },
    "curves": {
      "variable.fade": {"type": "linear", "input": "variable.particle_age", "horizontal_range": "variable.particle_lifetime", "nodes": [1.0, 0.0]}
    },
    "components": {
      "minecraft:emitter_rate_instant": {"num_particles": 12},
      "minecraft:emitter_lifetime_once": {"active_time": 1},
      "minecraft:emitter_shape_sphere": {"radius": 0.5, "direction": "outwards"},
      "minecraft:particle_lifetime_expression": {"max_lifetime": 0.5},
      "minecraft:particle_initial_speed": 2,
      "minecraft:particle_motion_dynamic": {"linear_acceleration": [0, -4, 0], "linear_drag_coefficient": 0.5},
      "minecraft:particle_appearance_billboard": {
        "size": ["0.1 * variable.fade", 0.1],
        "facing_camera_mode": "lookat_xyz",
        "uv": {"texture_width": 64, "texture_height": 64, "flipbook": {"base_UV": [0, 0], "size_UV": [16, 16], "step_UV": [16, 0], "max_frame": 4, "stretch_to_lifetime": true, "loop": false}}
      },
      "minecraft:particle_appearance_tinting": {"color": ["1.0", "0.5", "0.25", "variable.fade"]}
    }
  }
})";
    perlin::particle::DataParticles system;
    system.Configure({root.string()});
    Check(system.Has("test:burst"), "definition loaded");
    Check(!system.Has("test:missing"), "missing definition");
    Check(system.Spawn("test:burst", {10, 64, 10}), "spawn");
    perlin::particle::DataWorld world;
    system.Update(0.05f, world);
    Check(system.Count() == 12, "instant emitter count");
    perlin::particle::CameraView view;
    std::vector<perlin::particle::CustomQuad> quads;
    system.Collect(view, world, quads);
    Check(quads.size() == 12, "quad per particle");
    Check(!quads.empty() && quads[0].texture == "textures/particle/test", "texture path");
    Check(!quads.empty() && quads[0].blend == 2, "blend material");
    double radius = 0;
    for (const auto& q : quads)
        radius = std::max(radius, std::abs(q.centre.x - 10.0) + std::abs(q.centre.z - 10.0));
    Check(radius > 0.1, "particles move outward");
    for (int i = 0; i < 20; i++)
        system.Update(0.05f, world);
    Check(system.Count() == 0, "particles expire");
    quads.clear();
    system.Collect(view, world, quads);
    Check(quads.empty(), "no quads after expiry");
    fs::remove_all(root);
    std::printf(failures ? "particle_selftest FAILED\n" : "particle_selftest ok\n");
    return failures ? 1 : 0;
}
