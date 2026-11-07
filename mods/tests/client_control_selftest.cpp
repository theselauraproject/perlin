#include "client/ClientControl.hpp"
#include "client/input/InputManager.hpp"
#include "util/Json.hpp"
#include <httplib.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace perlin;
static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                                                 \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (false)

int main() {
    Input input;
    Options options;
    InputManager manager(input, options);
    std::bitset<kActionCount> held;
    held.set(static_cast<size_t>(Action::Forward));
    held.set(static_cast<size_t>(Action::Jump));
    manager.SetControlTick(10);
    manager.SetControlActions(held, 12);
    manager.Poll();
    CHECK(manager.Pressed(Action::Jump));
    CHECK(manager.Axis(Action::Forward, Action::Back) == 1);
    manager.SetControlTick(11);
    CHECK(manager.IsDown(Action::Jump));
    manager.SetControlTick(12);
    CHECK(!manager.IsDown(Action::Jump));
    manager.SetControlActions(held, 14);
    manager.Poll();
    CHECK(manager.Pressed(Action::Jump));
    manager.SetServerLocks(512);
    CHECK(!manager.IsDown(Action::Forward));
    manager.SetServerLocks(0);
    manager.SetBlocked(true);
    manager.SetControlActions({}, 14);
    manager.SetControlActions(held, 14);
    manager.Poll();
    CHECK(!manager.Pressed(Action::Jump));
    CHECK(!manager.IsDown(Action::Jump));
    manager.SetBlocked(false);

    httplib::Server probe;
    const int port = probe.bind_to_any_port("127.0.0.1");
    CHECK(port >= 1024);
    probe.stop();
    ClientControl server;
    CHECK(server.Start(port));
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(2);
    client.set_read_timeout(2);
    const std::string token = server.Token();
    CHECK(token.size() == 32);
    auto result = client.Get("/state");
    CHECK(result && result->status == 401);
    result = client.Post("/command", {{"X-Perlin-Token", std::string(token.size(), '0')}},
                         R"({"op":"shutdown"})", "application/json");
    CHECK(result && result->status == 401);
    result = client.Get("/state?token=" + token);
    CHECK(result && result->status == 401);
    result = client.Get("/?token=" + token);
    CHECK(result && result->status == 200 && result->body.contains(token) &&
          !result->body.contains("__PERLIN_TOKEN__"));
    CHECK(server.TakeCommands().empty());
    client.set_default_headers({{"X-Perlin-Token", token}});
    result = client.Post("/command", R"({"op":"input","actions":["forward","jump"],"ticks":20})",
                         "application/json");
    CHECK(result && result->status == 202);
    auto commands = server.TakeCommands();
    CHECK(commands.size() == 1 && commands[0].id == 1 && commands[0].ticks == 20);
    result = client.Post("/command", R"({"op":"input","actions":["unknown"]})", "application/json");
    CHECK(result && result->status == 400);
    result = client.Post("/command", R"({"op":"input","ticks":-1})", "application/json");
    CHECK(result && result->status == 400);
    result = client.Post("/command", R"({"op":"shutdown"})", "text/plain");
    CHECK(result && result->status == 415);
    result = client.Post("/command", {{"Origin", "https://unrelated.example"}}, R"({"op":"shutdown"})",
                         "application/json");
    CHECK(result && result->status == 403);
    CHECK(server.TakeCommands().empty());
    result = client.Post("/command", R"({"op":"character","key":128578})", "application/json");
    CHECK(result && result->status == 202);
    commands = server.TakeCommands();
    CHECK(commands.size() == 1 && commands[0].key == 128578);
    result = client.Post("/command", R"({"op":"character","key":55296})", "application/json");
    CHECK(result && result->status == 400);
    result = client.Post("/command", R"({"op":"key","key":128578})", "application/json");
    CHECK(result && result->status == 400);
    ControlSnapshot snapshot;
    snapshot.tick = 41;
    snapshot.corrections = 3;
    snapshot.position = {1, 2, 3};
    ControlActor actor;
    actor.runtimeId = "9007199254740993";
    actor.uniqueId = "-9223372036854775807";
    actor.vehicleUid = "9007199254740995";
    actor.type = "minecraft:boat";
    actor.position = {4, 5, 6};
    snapshot.actors.push_back(actor);
    server.Publish(snapshot);
    result = client.Get("/state");
    CHECK(result && result->status == 200);
    auto state = result ? json::Read<ControlSnapshot>(result->body) : std::nullopt;
    CHECK(state && state->tick == 41 && state->corrections == 3 && state->position[2] == 3);
    CHECK(state && state->actors.size() == 1 && state->actors[0].runtimeId == actor.runtimeId &&
          state->actors[0].uniqueId == actor.uniqueId && state->actors[0].vehicleUid == actor.vehicleUid &&
          state->actors[0].position[2] == 6);
    CHECK(result && result->body.contains("\"9007199254740993\""));
    const uint8_t bgra[16] = {0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    server.Frame(2, 2, 8, bgra, false);
    for (int i = 0; i < 200; ++i) {
        result = client.Get("/frame");
        if (!result || result->status != 202)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(result && result->status == 200);
    int w = 0, h = 0, n = 0;
    auto* pixels = result ? stbi_load_from_memory(reinterpret_cast<const unsigned char*>(result->body.data()),
                                                  static_cast<int>(result->body.size()), &w, &h, &n, 3)
                          : nullptr;
    CHECK(pixels && w == 2 && h == 2 && pixels[0] > 200 && pixels[1] < 50 && pixels[2] < 50);
    stbi_image_free(pixels);
    CHECK(server.TakeFrameRequest());
    CHECK(!server.TakeFrameRequest());
    server.Trace("{\"tick\":41}");
    result = client.Get("/trace");
    CHECK(result && result->body == "{\"tick\":41}\n");
    server.Stop();
    std::printf("client control: %d failures\n", failures);
    return failures ? 1 : 0;
}
