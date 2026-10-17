// config_load() against real pages from every released config writer
// (tests/host/fixtures/config/<tag>/, written by that tag's own config.cpp:
// gen/regen.sh; which tag covers which release: COVERAGE.txt). The CRC check
// on load must never reset settings a released firmware saved.
#include <cstddef>
#include <cstdio>
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
