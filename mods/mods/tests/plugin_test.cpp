#include <perlin/plugin.hpp>

#include <cstdio>
#include <cstring>
#include <string>

using namespace perlin;

namespace {
    int failures = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                      \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

    std::string messages;
    int registeredCommands = 0, registeredKeys = 0;
    std::vector<Op> ops;
    enum class Mode { Fast, Slow };
}

extern "C" int32_t perlin_host_call(uint32_t op, const void* in, uint32_t inLen, void*, uint32_t) {
    const auto o = static_cast<Op>(op);
    ops.push_back(o);
    abi::Reader r(static_cast<const uint8_t*>(in), inLen);
    if (o == Op::LocalMessage)
        messages += r.str() + "\n";
    if (o == Op::CommandRegister)
        registeredCommands++;
    if (o == Op::KeyRegister)
        registeredKeys++;
    return 0;
}

struct Hello : Plugin {
    int enabled = 0, disabled = 0, ticks = 0, chats = 0, waves = 0, looks = 0;
    std::string greeted;
    Mode mode = Mode::Fast;

    void registerCommands(PluginCtx& c) override {
        c.command<"greet">("Say hello", this, &Hello::greet);
        c.command<"mode">("Set mode", [this](std::optional<Mode> m) { mode = m.value_or(Mode::Slow); });
    }
    void onEnable(PluginCtx& c) override {
        enabled++;
        c.bind<"wave">("Wave", Key::R, this, &Hello::wave);
    }
    void onDisable(PluginCtx&) override { disabled++; }
    void onTick(const abi::EventTick&) { ticks++; }
    void onLook() { looks++; }
    Flow onChatReceived(const abi::EventChat& chat) {
        chats++;
        return chat.message == "cancel" ? Flow::cancel : Flow::pass;
    }
    void greet(std::string who, std::optional<int> times) {
        greeted = who + ":" + std::to_string(times.value_or(1));
    }
    void wave(bool down) { waves += down; }
};
PERLIN_REGISTER(Hello)

namespace {
    void send_event(abi::Event id, const auto& payload, uint32_t* flags = nullptr) {
        abi::Writer w;
        w.write(payload);
        const uint32_t f = perlin_event(static_cast<uint32_t>(id), w.Data(), static_cast<uint32_t>(w.Size()));
        if (flags)
            *flags = f;
    }
}

int main() {
    perlin_event(static_cast<uint32_t>(abi::Event::Load), nullptr, 0);
    Hello& h = detail_plugin::instance<Hello>();
    CHECK(h.enabled == 1);
    CHECK(registeredCommands == 2);
    CHECK(registeredKeys == 1);
    CHECK(&h.ctx() != nullptr);

    abi::EventTick t;
    send_event(abi::Event::Tick, t);
    CHECK(h.ticks == 1);

    abi::EventChat chat;
    chat.message = "hi";
    uint32_t flags = 0;
    send_event(abi::Event::ChatReceived, chat, &flags);
    CHECK(h.chats == 1 && !(flags & abi::kEventCancel));
    chat.message = "cancel";
    send_event(abi::Event::ChatReceived, chat, &flags);
    CHECK(flags & abi::kEventCancel);

    abi::EventCommand c;
    c.name = "greet";
    c.args = "bob 3";
    send_event(abi::Event::Command, c);
    CHECK(h.greeted == "bob:3");
    c.args = "amy";
    send_event(abi::Event::Command, c);
    CHECK(h.greeted == "amy:1");

    abi::EventKey k;
    k.id = "wave";
    k.down = true;
    send_event(abi::Event::Key, k);
    CHECK(h.waves == 1);

    c.name = "mode";
    c.args = "slow";
    send_event(abi::Event::Command, c);
    CHECK(h.mode == Mode::Slow);
    c.args = "bogus";
    h.mode = Mode::Fast;
    send_event(abi::Event::Command, c);
    CHECK(h.mode == Mode::Fast);

    abi::EventLook look;
    send_event(abi::Event::Look, look);
    CHECK(h.looks == 1);

    perlin_event(static_cast<uint32_t>(abi::Event::Unload), nullptr, 0);
    CHECK(h.disabled == 1);

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("plugin_test ok\n");
    return 0;
}
