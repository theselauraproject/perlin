#include "perlin_abi.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                      \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static uint32_t last_op;
static uint8_t last_in[4096];
static uint32_t last_in_len;

int32_t perlin_call(uint32_t op, const void* in, uint32_t in_len, void* out, uint32_t out_cap) {
    last_op = op;
    last_in_len = in_len < sizeof last_in ? in_len : (uint32_t)sizeof last_in;
    memcpy(last_in, in, last_in_len);
    if (op == PERLIN_OP_PLAYER_INFO) {
        uint8_t buf[512];
        perlin_writer w = perlin_writer_make(buf, sizeof buf);
        PerlinPlayerDetails d;
        memset(&d, 0, sizeof d);
        d.id = 42;
        d.name = perlin_cstr("Alice");
        d.health = 17.5f;
        d.gameMode = PERLIN_GAME_MODE_CREATIVE;
        PerlinEffect effects[2] = {{PERLIN_EFFECT_ID_SPEED, 1, 200, 0, 1}, {PERLIN_EFFECT_ID_HASTE, 0, 40, 1, 0}};
        d.effects.data = effects;
        d.effects.len = 2;
        perlin_write_PlayerDetails(&w, &d);
        if (w.len > out_cap)
            return -(int32_t)(PERLIN_NEED_MORE_BASE + w.len);
        memcpy(out, buf, w.len);
        return (int32_t)w.len;
    }
    if (op == PERLIN_OP_RUN_COMMAND) {
        const char reply[] = "There are 2 players online";
        if (sizeof reply - 1 > out_cap)
            return -(int32_t)(PERLIN_NEED_MORE_BASE + sizeof reply - 1);
        memcpy(out, reply, sizeof reply - 1);
        return (int32_t)(sizeof reply - 1);
    }
    return 0;
}

int main(void) {
    static uint8_t memory[1 << 16];
    perlin_arena a = perlin_arena_make(memory, sizeof memory);

    PerlinPlayerInfoRequest q = {42};
    PerlinPlayerDetails d;
    CHECK(perlin_player_info(&q, &d, &a) == 0);
    CHECK(last_op == PERLIN_OP_PLAYER_INFO && last_in_len == 8);
    CHECK(d.id == 42 && d.name.len == 5 && memcmp(d.name.data, "Alice", 5) == 0);
    CHECK(d.health == 17.5f && d.gameMode == PERLIN_GAME_MODE_CREATIVE);
    CHECK(d.effects.len == 2 && d.effects.data[0].id == PERLIN_EFFECT_ID_SPEED && d.effects.data[1].ticks == 40);

    PerlinRunCommandRequest c = {perlin_cstr("list")};
    perlin_str text;
    CHECK(perlin_run_command(&c, &text, &a) == 0);
    CHECK(text.len == 26 && memcmp(text.data, "There are 2", 11) == 0);
    CHECK(last_in_len == 8 && memcmp(last_in + 4, "list", 4) == 0);

    PerlinFormButton buttons[2] = {{perlin_cstr("Yes"), perlin_cstr("")}, {perlin_cstr("No"), perlin_cstr("x.png")}};
    PerlinPlayerFormRequest f;
    memset(&f, 0, sizeof f);
    f.player = 7;
    f.form.kind = PERLIN_FORM_KIND_MENU;
    f.form.title = perlin_cstr("Pick");
    f.form.buttons.data = buttons;
    f.form.buttons.len = 2;
    uint8_t buf[512];
    perlin_writer w = perlin_writer_make(buf, sizeof buf);
    perlin_write_PlayerFormRequest(&w, &f);
    CHECK(!w.overflow);
    perlin_reader r = perlin_reader_make(buf, w.len);
    PerlinPlayerFormRequest back;
    perlin_read_PlayerFormRequest(&r, &back, &a);
    CHECK(!r.failed && r.p == r.end);
    CHECK(back.player == 7 && back.form.kind == PERLIN_FORM_KIND_MENU && back.form.buttons.len == 2);
    CHECK(back.form.buttons.data[1].image.len == 5 && memcmp(back.form.buttons.data[1].image.data, "x.png", 5) == 0);

    perlin_reader shortr = perlin_reader_make(buf, 6);
    perlin_read_PlayerFormRequest(&shortr, &back, &a);
    CHECK(shortr.failed);

    uint32_t words[PERLIN_SUBSCRIPTION_WORDS] = {0};
    PERLIN_SUBSCRIBE(words, PERLIN_EVENT_PRE_LOGIN);
    CHECK(words[PERLIN_EVENT_PRE_LOGIN / 32] == (1u << (PERLIN_EVENT_PRE_LOGIN % 32)));

    printf(failures ? "c_header_test: %d failure(s)\n" : "c_header_test: ok\n", failures);
    return failures ? 1 : 0;
}
