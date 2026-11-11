#define PERLIN_DEFINE_RUNTIME
#include "perlin.h"

void* memcpy(void* d, const void* s, size_t n) {
    unsigned char* a = d;
    const unsigned char* b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void* memset(void* d, int c, size_t n) {
    unsigned char* a = d;
    while (n--)
        *a++ = (unsigned char)c;
    return d;
}

static void say(uint64_t player, perlin_str text, perlin_arena* a) {
    PerlinPlayerText m;
    m.player = player;
    m.text = text;
    perlin_player_message(&m, a);
}

static int contains_creeper(perlin_str s) {
    static const char word[] = "creeper";
    for (uint32_t i = 0; i + 7 <= s.len; i++) {
        uint32_t j = 0;
        while (j < 7 && (s.data[i + j] | 0x20) == word[j])
            j++;
        if (j == 7)
            return 1;
    }
    return 0;
}

static int same(perlin_str s, const char* lit) {
    perlin_str l = perlin_cstr(lit);
    if (s.len != l.len)
        return 0;
    for (uint32_t i = 0; i < s.len; i++)
        if (s.data[i] != l.data[i])
            return 0;
    return 1;
}

static void register_command(const char* name, const char* description, perlin_arena* a) {
    PerlinCommandRegisterRequest r;
    r.name = perlin_cstr(name);
    r.usage = perlin_cstr(name);
    r.description = perlin_cstr(description);
    perlin_command_register(&r, a);
}

static void show_menu(uint64_t player, perlin_arena* a) {
    static PerlinFormButton buttons[2];
    buttons[0].text = perlin_cstr("Heal me");
    buttons[0].image = perlin_cstr("textures/items/apple");
    buttons[1].text = perlin_cstr("Make it day");
    buttons[1].image = perlin_cstr("");
    PerlinPlayerFormRequest r;
    memset(&r, 0, sizeof r);
    r.player = player;
    r.form.kind = PERLIN_FORM_KIND_MENU;
    r.form.title = perlin_cstr("Hello");
    r.form.body = perlin_cstr("Pick one");
    r.form.buttons.data = buttons;
    r.form.buttons.len = 2;
    uint32_t id = 0;
    perlin_player_form(&r, &id, a);
}

PERLIN_EXPORT(perlin_event) uint32_t perlin_event(uint32_t id, const unsigned char* p, uint32_t len) {
    perlin_arena a = perlin_frame();
    perlin_heap_top_ = 0;
    if (id == PERLIN_EVENT_LOAD) {
        register_command("hello", "says hello", &a);
        register_command("menu", "opens a menu", &a);
        perlin_subscribe(PERLIN_EVENT_PLAYER_JOIN);
        perlin_subscribe(PERLIN_EVENT_PLAYER_CHAT);
        perlin_subscribe(PERLIN_EVENT_COMMAND);
        perlin_subscribe(PERLIN_EVENT_PLAYER_DAMAGE);
        perlin_subscribe(PERLIN_EVENT_FORM_RESPONSE);
        return 0;
    }
    if (id == PERLIN_EVENT_PLAYER_JOIN) {
        PerlinEventPlayer e;
        if (perlin_decode_event_player_join(p, len, &e, &a))
            return 0;
        char text[96] = "Welcome, ";
        uint32_t at = 9;
        for (uint32_t i = 0; i < e.name.len && at < sizeof text - 2; i++)
            text[at++] = e.name.data[i];
        text[at++] = '!';
        perlin_str s = {text, at};
        say(e.player, s, &a);
        return 0;
    }
    if (id == PERLIN_EVENT_PLAYER_CHAT) {
        PerlinEventPlayerChat e;
        if (perlin_decode_event_player_chat(p, len, &e, &a))
            return 0;
        return contains_creeper(e.message) ? PERLIN_EVENT_CANCEL : 0u;
    }
    if (id == PERLIN_EVENT_PLAYER_DAMAGE) {
        PerlinEventServerDamage e;
        if (perlin_decode_event_player_damage(p, len, &e, &a) || e.cause != PERLIN_DAMAGE_CAUSE_FALL)
            return 0;
        PerlinEventAmendRequest r;
        r.field = perlin_cstr("amount");
        r.number = e.amount / 2;
        r.text = perlin_cstr("");
        perlin_event_amend(&r, &a);
        return 0;
    }
    if (id == PERLIN_EVENT_FORM_RESPONSE) {
        PerlinEventFormResponse e;
        if (perlin_decode_event_form_response(p, len, &e, &a) || e.closed)
            return 0;
        if (e.button == 0) {
            PerlinPlayerSetHealthRequest h;
            h.player = e.player;
            h.health = 20;
            perlin_player_set_health(&h, &a);
        } else if (e.button == 1) {
            PerlinTimeSetRequest t;
            t.time = 1000;
            perlin_time_set(&t, &a);
        }
        return 0;
    }
    if (id == PERLIN_EVENT_COMMAND) {
        PerlinEventCommand e;
        if (perlin_decode_event_command(p, len, &e, &a))
            return 0;
        if (same(e.name, "menu")) {
            show_menu(e.player, &a);
            return 0;
        }
        PerlinOnlinePlayerList list;
        uint32_t count = 0;
        if (perlin_online_players(&list, &a) == 0)
            count = list.players.len;
        char text[] = "Hello! Players online: 0";
        text[sizeof text - 2] = (char)('0' + (count % 10));
        perlin_str s = {text, sizeof text - 1};
        say(e.player, s, &a);
        return 0;
    }
    return 0;
}
