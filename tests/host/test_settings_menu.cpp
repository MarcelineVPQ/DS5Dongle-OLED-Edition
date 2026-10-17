// src/settings_menu.h: the OLED Settings screen's layout A (category menu and
// group pages). Every row fits its 19 glyphs at every value, values end on one
// column, nothing reaches the footer band, the selected row's bar stays inside
// its x bounds; the buttons (menu <-> group, back keeps the cursor, save from
// both levels, holds only on Reset); and a save still takes every item's field
// by offset in the new order (settings_merge).
#include <cstddef>
#include <cstring>
#include <set>
#include <string>
#include "bit_canvas.h"
#include "config.h"
#include "fakes.h"
#include "settings_menu.h"
#include "test.h"

using namespace settings_menu;

namespace {
constexpr uint8_t kUp = 0, kRight = 2, kDown = 4, kLeft = 6, kRel = 8;

Config_body defaults() {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    return get_config();
}

// Every value item `it` can show: lo..hi by its own D-pad steps, both ways.
std::set<std::string> all_values(const SetItem &it, Config_body body, uint8_t power) {
    std::set<std::string> out;
    char v[24];
    // loop: NOT-WORTH — host test; ≤ 251 steps per item
    for (int dir : {-1, +1}) {
        // loop: NOT-WORTH — host test
        for (int i = 0; i < 300; i++) {
            format_value(it, body, power, v, sizeof(v));
            out.insert(v);
            adjust(it, body, power, dir);
        }
    }
    return out;
}

// Press and release one D-pad direction / face button.
void dpad(Nav &n, uint8_t d, uint32_t t = 0) { n.step(d, 0, t); n.step(kRel, 0, t); }
Step face(Nav &n, uint8_t f, uint32_t t = 0) {
    Step a = n.step(kRel, f, t);
    const Step b = n.step(kRel, 0, t + 1000);
    if (b.action != kNone) a.action = b.action;
    return a;
}
void draw_page(BitCanvas &c, const Nav &n, const Config_body &local, uint8_t power, const Config_body &saved,
               uint8_t saved_power, bool dirty, const char *status) {
    c.clear();
    const View v{local, power, saved, saved_power, dirty, status};
    draw(c, n, v);
}
bool any_lit(const BitCanvas &c, int x0, int y0, int x1, int y1) {
    // loop: NOT-WORTH — host test
    for (int y = y0; y <= y1; y++)
        // loop: NOT-WORTH — host test
        for (int x = x0; x <= x1; x++)
            if (c.p[y][x]) return true;
    return false;
}
}  // namespace

TEST(settings_menu_every_row_fits_19_glyphs_at_every_value) {
    const Config_body body = defaults();
    // A row: the label from column 1, one blank at least, the value ending on column 18.
    // loop: NOT-WORTH — host test, 27 items
    for (const SetItem &it : kItems) {
        const int label = static_cast<int>(std::strlen(it.label));
        CHECK(kLabelCol + label <= kValueEndCol + 1);
        // loop: NOT-WORTH — host test
        for (const std::string &v : all_values(it, body, POWER_FLAGS_DEFAULT)) {
            if (kLabelCol + label + 1 + static_cast<int>(v.size()) > kValueEndCol + 1) {
                std::fprintf(stderr, "  \"%s\" + \"%s\" doesn't fit\n", it.label, v.c_str());
                CHECK(false);
            }
        }
        if (it.hint) CHECK(std::strlen(it.hint) <= kGlyphs);
        CHECK((it.kind == SK_ACTION) == (it.act != ACT_NONE));
    }
    // loop: NOT-WORTH — host test, 6 groups
    for (int g = 0; g < kNumGroups; g++) {
        CHECK(kLabelCol + std::strlen(kGroups[g].name) + 1 + 1 <= static_cast<size_t>(kValueEndCol + 1));
        Nav n;
        n.group = static_cast<uint8_t>(g);
        n.in_group = true;
        char t[32];
        title_text(n, true, t, sizeof(t));
        CHECK(std::strlen(t) <= kGlyphs);
    }
    CHECK(std::strlen(kMenuFooter) <= kGlyphs);
    CHECK(std::strlen(kPageFooter) <= kGlyphs);
    // loop: NOT-WORTH — host test
    for (const char *s : {"Saved!", "Save FAIL", "Reset!", "Reset FAIL", "Slots wiped!", "Wipe FAIL"})
        CHECK(std::strlen(s) <= kGlyphs);
}

TEST(settings_menu_groups_in_order_with_their_items) {
    const char *const want[][11] = {
        {"Haptics/trigger", "Vibration L", "Vibration R", "Trig. effect", "AutoHap", "AutoHap gain", "Auto filter"},
        {"Audio", "Spk volume", "Spk boost", "Lock volume", "Audio out", "Built-in mic", "Mic used", "Mic level",
         "Echo cancel", "Audio buffer"},
        {"Connection", "Controller", "Poll rate", "Disconnect", "Idle time"},
        {"Screen & lights", "Screen dim", "Screen off", "Wake on input", "Status LED"},
        {"PC sleep", "Turn off ctrl", "PS wakes PC"},
        {"Reset", "Reset to defaults", "Wipe all slots"},
    };
    CHECK_EQ(kNumGroups, 6);
    CHECK_EQ(kNumItems, 27);
    // loop: NOT-WORTH — host test
    for (int g = 0; g < kNumGroups; g++) {
        CHECK(std::strcmp(kGroups[g].name, want[g][0]) == 0);
        // loop: NOT-WORTH — host test
        for (int i = 0; i < kGroups[g].count; i++) CHECK(std::strcmp(kItems[kGroups[g].first + i].label, want[g][i + 1]) == 0);
        CHECK(want[g][kGroups[g].count + 1] == nullptr);
    }
    // Only the Reset page holds actions.
    // loop: NOT-WORTH — host test
    for (int i = 0; i < kNumItems; i++) CHECK((kItems[i].act != ACT_NONE) == (i >= kGroups[5].first));
}

// Every page through draw(): in bounds, values right-aligned to one column,
// the footer band clear, the bar inside x 8..122 and its row's y-1..y+7.
TEST(settings_menu_pages_draw_inside_their_bounds) {
    const Config_body body = defaults();
    Config_body changed_body = body;
    changed_body.speaker_volume = -100.0f;  // the widest value, cut out of the bar
    // loop: NOT-WORTH — host test
    for (int g = -1; g < kNumGroups; g++) {
        Nav n;
        n.in_group = g >= 0;
        n.group = static_cast<uint8_t>(g < 0 ? 0 : g);
        // loop: NOT-WORTH — host test
        for (int sel = 0; sel < n.rows(); sel++) {
            if (n.in_group) n.row = static_cast<uint8_t>(sel); else n.group = static_cast<uint8_t>(sel);
            // loop: NOT-WORTH — host test
            for (int adj = 0; adj < 2; adj++) {
                BitCanvas c;
                draw_page(c, n, adj ? changed_body : body, adj ? 0x00 : POWER_FLAGS_DEFAULT, body, POWER_FLAGS_DEFAULT, adj, nullptr);
                CHECK_EQ(c.off_screen, 0);
                CHECK(!any_lit(c, 0, 0, kX - 1, 63));           // the chrome strip is flush_fb's
                CHECK(!any_lit(c, kX, row_y(kVisible - 1) + kRowH, 127, kFooterY - 1));  // the gap above the footer (y 53..55)
                CHECK(!any_lit(c, kBarX + kBarW, 0, kScrollX - 1, 63));  // between the bar and the scroll bar
                CHECK(!any_lit(c, kX + kGlyphs * kGlyphW, kFooterY, 127, 63));  // footer within 19 glyphs
                // The scroll bar only when the page scrolls.
                CHECK(any_lit(c, kScrollX, kScrollY, 127, kScrollY + kScrollH - 1) == (n.rows() > kVisible));
                const int top = top_row(sel);
                // loop: NOT-WORTH — host test
                for (int i = 0; i < kVisible && top + i < n.rows(); i++) {
                    const int y = row_y(i);
                    const bool selected = top + i == sel;
                    if (selected) {
                        // The bar: lit at its corners, nothing just outside it.
                        CHECK(c.p[y - 1][kBarX] && c.p[y + 7][kBarX]);
                        CHECK(c.p[y - 1][kBarX + kBarW - 1] && c.p[y + 7][kBarX + kBarW - 1]);
                        CHECK(!any_lit(c, kX, y - 1, kBarX - 1, y + 7));
                    } else {
                        // The value's last glyph ends on column 18 (x 114..118), nothing after it.
                        const char *label;
                        char v[24];
                        row_text(n, top + i, View{body, POWER_FLAGS_DEFAULT, body, POWER_FLAGS_DEFAULT, false, nullptr},
                                 label, v, sizeof(v));
                        CHECK(!any_lit(c, kX + kGlyphs * kGlyphW - 1, y, kBarX + kBarW - 1, y + 6));
                        if (v[0]) CHECK(any_lit(c, kX + kValueEndCol * kGlyphW, y, kX + kValueEndCol * kGlyphW + 4, y + 6));
                        // Label from column 1.
                        CHECK(!any_lit(c, kX, y, kX + kLabelCol * kGlyphW - 1, y + 6));
                        CHECK(any_lit(c, kX + kLabelCol * kGlyphW, y, kX + kLabelCol * kGlyphW + 4, y + 6));
                    }
                }
            }
        }
    }
}

// A changed value is cut out of the bar (kNotch): a dark window, lit text, a lit cap.
TEST(settings_menu_changed_value_is_cut_out_of_the_bar) {
    const Config_body body = defaults();
    Config_body local = body;
    Nav n;
    n.in_group = true;
    n.group = 1;  // Audio, Spk volume
    BitCanvas plain, adj;
    draw_page(plain, n, local, POWER_FLAGS_DEFAULT, body, POWER_FLAGS_DEFAULT, false, nullptr);
    local.speaker_volume = -20.0f;
    draw_page(adj, n, local, POWER_FLAGS_DEFAULT, body, POWER_FLAGS_DEFAULT, true, nullptr);
    const int y = row_y(0), x0 = value_x(5) - 2;  // "-20dB"
    CHECK(plain.p[y - 1][kBarX + kBarW - 1]);  // a full bar while nothing changed
    CHECK(adj.p[y - 1][kBarX + kBarW - 1]);    // the bar's 2 px cap stays lit
    CHECK(!adj.p[y - 1][kBarX + kBarW - kNotchCap - 1]);  // the value's window is dark
    CHECK(!adj.p[y - 1][x0] && adj.p[y - 1][x0 - 1]);
    CHECK(any_lit(adj, x0, y, kBarX + kBarW - 1, y + 6));  // the value lit on dark
    // Back to the saved value: a full bar again.
    local.speaker_volume = body.speaker_volume;
    draw_page(adj, n, local, POWER_FLAGS_DEFAULT, body, POWER_FLAGS_DEFAULT, true, nullptr);
    CHECK(adj.same(plain) == false);  // the title still says (*)
    CHECK(adj.p[y - 1][kBarX + kBarW - 1]);
    // A power flag counts too.
    n.group = 4;
    draw_page(adj, n, body, 0x00, body, POWER_FLAGS_DEFAULT, true, nullptr);
    CHECK(!adj.p[y - 1][kBarX + kBarW - kNotchCap - 1]);
}

TEST(settings_menu_menu_opens_groups_and_back_keeps_the_cursor) {
    Nav n;
    CHECK(!n.in_group);
    dpad(n, kLeft);  // does nothing on the menu
    CHECK(!n.in_group);
    CHECK_EQ(n.group, 0);
    dpad(n, kUp);    // wraps
    CHECK_EQ(n.group, 5);
    dpad(n, kDown);
    dpad(n, kDown);
    CHECK_EQ(n.group, 1);
    face(n, kCross);  // X opens
    CHECK(n.in_group);
    CHECK_EQ(n.group, 1);
    CHECK_EQ(n.row, 0);
    dpad(n, kDown);
    dpad(n, kDown);
    CHECK_EQ(n.item(), kGroups[1].first + 2);
    face(n, kCross);  // X on a page does nothing
    CHECK(n.in_group);
    face(n, kCircle);  // O goes back, the menu cursor kept
    CHECK(!n.in_group);
    CHECK_EQ(n.group, 1);
    face(n, kCircle);  // O on the menu does nothing
    CHECK(!n.in_group);
    CHECK_EQ(n.group, 1);
    dpad(n, kDown);
    dpad(n, kRight);  // Right opens too, at the page's top
    CHECK(n.in_group);
    CHECK_EQ(n.group, 2);
    CHECK_EQ(n.row, 0);
    dpad(n, kUp);  // wraps within the group
    CHECK_EQ(n.row, kGroups[2].count - 1);
    // Held buttons act once: no repeat while held.
    Nav h;
    h.step(kDown, 0, 0);
    h.step(kDown, 0, 100000);
    CHECK_EQ(h.group, 1);
    h.step(kRel, kCross, 0);
    h.step(kRel, kCross, 0);
    CHECK(h.in_group);
    h.step(kRel, kCross | kCircle, 0);  // O rising while X held: back
    CHECK(!h.in_group);
}

TEST(settings_menu_left_right_adjust_only_on_a_page) {
    Nav n;
    Step s = n.step(kRight, 0, 0);
    CHECK_EQ(s.adjust, 0);  // opened the group instead
    CHECK(n.in_group);
    n.step(kRel, 0, 0);
    s = n.step(kRight, 0, 0);
    CHECK_EQ(s.adjust, +1);
    n.step(kRel, 0, 0);
    s = n.step(kLeft, 0, 0);
    CHECK_EQ(s.adjust, -1);
    n.step(kRel, 0, 0);
    s = n.step(kDown, 0, 0);
    CHECK_EQ(s.adjust, 0);
}

TEST(settings_menu_triangle_saves_from_both_levels) {
    Nav n;
    CHECK_EQ(face(n, kTriangle).action, kSave);  // from the menu
    CHECK_EQ(face(n, kTriangle).action, kSave);
    n.group = 5;                                    // cursor on Reset: still a save from the menu
    CHECK_EQ(face(n, kTriangle).action, kSave);
    n.group = 1;
    face(n, kCross);
    CHECK_EQ(face(n, kTriangle).action, kSave);  // from a group page
    // Saves on the release, once.
    CHECK_EQ(n.step(kRel, kTriangle, 0).action, kNone);
    CHECK_EQ(n.step(kRel, kTriangle, 5'000'000).action, kNone);  // a long hold on a normal page is a save too
    CHECK_EQ(n.step(kRel, 0, 5'000'001).action, kSave);
    CHECK_EQ(n.step(kRel, 0, 5'000'002).action, kNone);
}

TEST(settings_menu_holds_only_on_the_reset_page) {
    Nav n;
    n.group = 5;
    face(n, kCross);
    CHECK(n.hold_row());
    // A short press does nothing.
    CHECK_EQ(face(n, kTriangle, 0).action, kNone);
    // 2 s held: Reset, once, and no save on the release.
    uint32_t t = 10'000'000;
    CHECK_EQ(n.step(kRel, kTriangle, t).action, kNone);
    CHECK_EQ(n.step(kRel, kTriangle, t + kHoldUs - 1).action, kNone);
    CHECK_EQ(n.step(kRel, kTriangle, t + kHoldUs).action, kReset);
    CHECK_EQ(n.step(kRel, kTriangle, t + kHoldUs + 500000).action, kNone);
    CHECK_EQ(n.step(kRel, 0, t + kHoldUs + 600000).action, kNone);
    // Wipe on the second row.
    dpad(n, kDown);
    t = 20'000'000;
    n.step(kRel, kTriangle, t);
    CHECK_EQ(n.step(kRel, kTriangle, t + kHoldUs).action, kWipe);
    CHECK_EQ(n.step(kRel, 0, t + kHoldUs + 1).action, kNone);
    // Elsewhere a hold is not a reset.
    face(n, kCircle);
    n.group = 0;
    face(n, kCross);
    CHECK(!n.hold_row());
    n.step(kRel, kTriangle, 0);
    CHECK_EQ(n.step(kRel, kTriangle, kHoldUs + 1).action, kNone);
    CHECK_EQ(n.step(kRel, 0, kHoldUs + 2).action, kSave);
    // The timer wraps (time_us_32): a hold across the wrap still counts.
    n.group = 5;
    face(n, kCircle);
    face(n, kCross);
    n.step(kRel, kTriangle, 0xFFFFFFFFu - 1000);
    CHECK_EQ(n.step(kRel, kTriangle, kHoldUs - 1001).action, kReset);
}

// The reorder keeps the save keyed by field: every item's field is taken from
// the screen's copy, nothing else is, and each once.
TEST(settings_menu_save_takes_every_items_field_in_the_new_order) {
    settings_merge::Field fields[kNumItems];
    const size_t n = merge_fields(fields);
    CHECK_EQ(n, static_cast<size_t>(kNumItems - 4));  // 2 power flags + 2 actions aren't body fields
    std::set<int> seen;
    // loop: NOT-WORTH — host test
    for (size_t i = 0; i < n; i++) {
        CHECK(fields[i].off + fields[i].size <= sizeof(Config_body));
        // loop: NOT-WORTH — host test
        for (int b = 0; b < fields[i].size; b++) CHECK(seen.insert(fields[i].off + b).second);
    }
    const Config_body live = defaults();
    Config_body local = live;
    uint8_t power = POWER_FLAGS_DEFAULT;
    // Change every item once on the screen's copy.
    // loop: NOT-WORTH — host test
    for (const SetItem &it : kItems) adjust(it, local, power, +1);
    Config_body other = live;  // changed elsewhere since the visit
    other.lightbar_mode = 3;
    other.current_slot = 2;
    other.screen_brightness = 1;
    other.narrator_voice = 4;
    set_config(other);
    set_config(settings_merge::apply(get_config(), local, fields, n));
    CHECK(config_save());
    config_load();
    const Config_body &c = get_config();
    const auto *pc = reinterpret_cast<const uint8_t *>(&c);
    const auto *pl = reinterpret_cast<const uint8_t *>(&local);
    // loop: NOT-WORTH — host test
    for (const SetItem &it : kItems) {
        if (const uint8_t size = field_size(it)) {
            if (std::memcmp(pc + it.off, pl + it.off, size) != 0) {
                std::fprintf(stderr, "  %s not saved\n", it.label);
                CHECK(false);
            }
        }
    }
    CHECK(c.haptics_gain == local.haptics_gain);
    CHECK(c.haptics_gain_right == local.haptics_gain_right);
    CHECK(c.speaker_volume == local.speaker_volume);
    CHECK_EQ(c.mic_echo_cancel, local.mic_echo_cancel);
    CHECK_EQ(c.disable_pico_led, local.disable_pico_led);
    CHECK_EQ(c.lightbar_mode, 3);
    CHECK_EQ(c.current_slot, 2);
    CHECK_EQ(c.screen_brightness, 1);
    CHECK_EQ(c.narrator_voice, 4);
    CHECK(power != POWER_FLAGS_DEFAULT);  // the power flags changed on the copy only
}
