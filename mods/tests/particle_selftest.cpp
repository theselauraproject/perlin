#include "client/particle/DataParticles.hpp"

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
}

int main() {
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
