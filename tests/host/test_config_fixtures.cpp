// config_load() against real pages from every released config writer
// (tests/host/fixtures/config/<tag>/, written by that tag's own config.cpp:
// gen/regen.sh; which tag covers which release: COVERAGE.txt). The CRC check
// on load must never reset settings a released firmware saved.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "config.h"
#include "fakes.h"
#include "flash_layout.h"
#include "hardware/flash.h"
#include "test.h"
#include "fixtures/config/gen/nondefault.h"

namespace fs = std::filesystem;

namespace {
struct Fixture {
    std::string tag, scenario;
    uint32_t from_end = 0;  // where that firmware wrote it: PICO_FLASH_SIZE_BYTES - from_end
    std::vector<uint8_t> page;
    unsigned body_size = 0, inactive_off = 0;
};

std::vector<Fixture> load_fixtures() {
    std::vector<Fixture> out;
    // loop: NOT-WORTH — host test
    for (const auto &dir : fs::directory_iterator(CONFIG_FIXTURES)) {
        if (!dir.is_directory() || dir.path().filename().string().rfind('v', 0) != 0) continue;
        unsigned body = 0, inact = 0, hg = 0;
        std::FILE *lf = std::fopen((dir.path() / "layout.txt").c_str(), "r");
        if (!lf || std::fscanf(lf, "body_size %u inactive_time %u haptics_gain %u", &body, &inact, &hg) != 3) {
            std::fprintf(stderr, "  bad layout.txt in %s\n", dir.path().c_str());
            CHECK(false);
        }
        if (lf) std::fclose(lf);
        // loop: NOT-WORTH — host test
        for (const auto &f : fs::directory_iterator(dir.path())) {
            if (f.path().extension() != ".bin") continue;
            std::ifstream in(f.path(), std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
            CHECK_EQ(bytes.size(), 4u + FLASH_PAGE_SIZE);
            if (bytes.size() != 4u + FLASH_PAGE_SIZE) continue;
            Fixture x;
            x.tag = dir.path().filename().string();
            x.scenario = f.path().stem().string();
            std::memcpy(&x.from_end, bytes.data(), 4);
            x.page.assign(bytes.begin() + 4, bytes.end());
            x.body_size = body;
            x.inactive_off = inact;
            out.push_back(std::move(x));
        }
    }
    return out;
}

bool is_upstream_07(const std::string &tag) { return tag == "v0.7.0" || tag == "v0.7.2" || tag == "v0.7.2-hotfix"; }
bool is_oled(const std::string &tag) { return tag.find("-oled-edition") != std::string::npos; }

bool region_erased(uint32_t off) {
    // loop: NOT-WORTH — host test, one page
    for (uint32_t i = 0; i < FLASH_PAGE_SIZE; i++)
        if (g_fake_flash[off + i] != 0xFF) return false;
    return true;
}
}  // namespace

TEST(config_fixtures_cover_every_released_writer) {
    const auto fixtures = load_fixtures();
    // 19 writers (COVERAGE.txt), each with a default and a custom page.
    CHECK(fixtures.size() >= 38u);
    std::ifstream cov(std::string(CONFIG_FIXTURES) + "/COVERAGE.txt");
    std::string line;
    int tags = 0;
    // loop: NOT-WORTH — host test
    while (std::getline(cov, line)) {
        if (line.empty() || line[0] == '#') continue;
        tags++;
        if (line.find("UNCOVERED") != std::string::npos) {
            std::fprintf(stderr, "  %s\n", line.c_str());
            CHECK(false);
        }
    }
    CHECK(tags >= 55);
}

// Every page our released firmware wrote loads its saved settings: the same
// result as before the CRC check (the body as saved, then config_valid), never
// the defaults. Pages from before 0.7.0's sector move sit at the legacy sector
// and are migrated (and the legacy copy erased).
TEST(config_fixtures_load_their_saved_settings) {
    const auto fixtures = load_fixtures();
    // loop: NOT-WORTH — host test
    for (const Fixture &x : fixtures) {
        if (is_upstream_07(x.tag)) continue;  // their header differs, see below
        fake_flash_erase_all();
        g_fake_flash_fail = false;
        const uint32_t off = PICO_FLASH_SIZE_BYTES - x.from_end;
        std::memcpy(g_fake_flash + off, x.page.data(), FLASH_PAGE_SIZE);
        config_default();
        config_load();

        // What the page holds: that firmware's body (its own length; any
        // newer field as erased flash), as config_valid reads it.
        Config saved{};
        std::memcpy(&saved, x.page.data(), sizeof(saved));
        if (x.body_size < sizeof(Config_body))
            std::memset(reinterpret_cast<uint8_t *>(&saved.body) + x.body_size, 0xFF, sizeof(Config_body) - x.body_size);
        const Config_body loaded = get_config();
        set_config(saved.body);
        const Config_body expect = get_config();
        set_config(loaded);
        const bool same = std::memcmp(&loaded, &expect, sizeof(loaded)) == 0;
        if (!same) std::fprintf(stderr, "  %s/%s didn't load its saved settings\n", x.tag.c_str(), x.scenario.c_str());
        CHECK(same);

        if (x.scenario.rfind("custom", 0) == 0) {
            CHECK_EQ(x.inactive_off, (unsigned) offsetof(Config_body, inactive_time));
            CHECK_EQ(loaded.inactive_time, 42);
            if (is_oled(x.tag)) CHECK_EQ(loaded.current_slot, 2);
            if (x.body_size > offsetof(Config_body, lightbar_mode)) CHECK_EQ(loaded.lightbar_mode, 3);
            if (x.body_size > offsetof(Config_body, mic_level)) CHECK_EQ(loaded.mic_level, 20);
        }
        if (x.scenario == "custom_narrator") CHECK_EQ(loaded.narrator_voice, 3);
        if (x.scenario == "custom_power" && config_power_flags() != 0x03) {
            std::fprintf(stderr, "  %s/%s: power flags %u\n", x.tag.c_str(), x.scenario.c_str(), config_power_flags());
            CHECK_EQ(config_power_flags(), 0x03);
        }
        if (off == LEGACY_CONFIG_FLASH_OFFSET) {
            CHECK(region_erased(LEGACY_CONFIG_FLASH_OFFSET));  // migrated, legacy copy gone
            CHECK(!config_unsaved());                          // and saved in the new sector
        }
    }
}

// Upstream awalol 0.7.x saved its config with another header (no version
// field: the CRC and size sit 2 bytes earlier) at the legacy sector. Every
// release of ours since 0.7.0 already ignored it there (the migration checks
// the CRC at our offsets), so it loads the defaults, as it did before.
TEST(config_fixtures_upstream_07_pages_load_defaults_as_before) {
    const auto fixtures = load_fixtures();
    int seen = 0;
    // loop: NOT-WORTH — host test
    for (const Fixture &x : fixtures) {
        if (!is_upstream_07(x.tag)) continue;
        seen++;
        CHECK_EQ(PICO_FLASH_SIZE_BYTES - x.from_end, LEGACY_CONFIG_FLASH_OFFSET);
        fake_flash_erase_all();
        g_fake_flash_fail = false;
        std::memcpy(g_fake_flash + LEGACY_CONFIG_FLASH_OFFSET, x.page.data(), FLASH_PAGE_SIZE);
        config_load();
        CHECK_EQ(get_config().inactive_time, 30);
        CHECK(!region_erased(LEGACY_CONFIG_FLASH_OFFSET));  // not ours: left alone
    }
    CHECK(seen >= 6);
}

// Review fw-usb-bt N4: a one-field save (narrator, PC sleep options) on a
// page from any released writer keeps every other setting as config_load()
// read it. It used to copy the page raw: a 0.9.5-dev.9 page's power flags
// (body[49]) became mic_level, and a page failing its CRC was re-blessed.
TEST(config_fixtures_one_field_saves_keep_the_other_settings) {
    const auto fixtures = load_fixtures();
    int seen = 0;
    // loop: NOT-WORTH — host test
    for (const Fixture &x : fixtures) {
        if (is_upstream_07(x.tag)) continue;
        // loop: NOT-WORTH — host test, the two one-field saves
        for (int which = 0; which < 2; which++) {
            fake_flash_erase_all();
            g_fake_flash_fail = false;
            std::memcpy(g_fake_flash + PICO_FLASH_SIZE_BYTES - x.from_end, x.page.data(), FLASH_PAGE_SIZE);
            config_default();
            config_load();
            Config_body before = get_config();
            const uint8_t flags_before = config_power_flags();
            if (which == 0) {
                CHECK(config_save_narrator(5));
                before.narrator_voice = 5;
            } else {
                CHECK(config_save_power_flags(0x02));
            }
            config_default();
            config_load();
            const Config_body after = get_config();
            const bool same = std::memcmp(&before, &after, sizeof(before)) == 0;
            if (!same) std::fprintf(stderr, "  %s/%s: save %d changed other settings\n", x.tag.c_str(), x.scenario.c_str(), which);
            CHECK(same);
            CHECK_EQ(config_power_flags(), which == 0 ? flags_before : 0x02);
            seen++;
        }
    }
    CHECK(seen > 0);
}

// A page failing its CRC loads defaults; a one-field save then starts from
// those (RAM), not from the bad page's bytes.
TEST(config_one_field_save_never_reblesses_a_bad_page) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_default();
    Config_body b = get_config();
    b.inactive_time = 42;
    set_config(b);
    CHECK(config_save());
    g_fake_flash[CONFIG_FLASH_OFFSET + offsetof(Config, body) + offsetof(Config_body, inactive_time)] ^= 0x01;  // torn
    config_load();
    CHECK_EQ(get_config().inactive_time, 30);  // defaults
    CHECK(config_save_narrator(2));
    config_load();
    CHECK_EQ(get_config().inactive_time, 30);
    CHECK_EQ(get_config().narrator_voice, 2);
}

// Review update-flash N7: going back to the last stable release keeps the
// settings. gen/stable_reads.sh runs that release's own config.cpp on the
// page this firmware saves with the custom settings (stable/current_custom.bin)
// and records the result in stable/RESULT.txt. This test holds the recorded
// page to what this firmware saves now: when the saved page changes, rerun
// the host tests with DS5_STABLE_PAGE_OUT=<that file> and then the script.
TEST(config_stable_release_reads_the_current_page) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body b = get_config();
    nondefault(b);
    set_config(b);
    CHECK(config_save());
    const uint32_t from_end = PICO_FLASH_SIZE_BYTES - CONFIG_FLASH_OFFSET;
    std::vector<uint8_t> now(4 + FLASH_PAGE_SIZE);
    std::memcpy(now.data(), &from_end, 4);
    std::memcpy(now.data() + 4, g_fake_flash + CONFIG_FLASH_OFFSET, FLASH_PAGE_SIZE);
    if (const char *out = std::getenv("DS5_STABLE_PAGE_OUT")) {
        std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char *>(now.data()), (std::streamsize) now.size());
    }
    const std::string dir = std::string(CONFIG_FIXTURES) + "/stable/";
    std::ifstream in(dir + "current_custom.bin", std::ios::binary);
    const std::vector<uint8_t> recorded((std::istreambuf_iterator<char>(in)), {});
    const bool same = recorded == now;
    if (!same) std::fprintf(stderr, "  the saved page changed: rerun gen/stable_reads.sh (see this test)\n");
    CHECK(same);
    std::ifstream res(dir + "RESULT.txt");
    std::string verdict;
    res >> verdict;
    CHECK(verdict == "ok");
}
