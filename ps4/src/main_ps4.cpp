// Jet Force Gemini PS4: replaces src/main.cpp. Same game registration and runtime callbacks; no launcher window (the
// ROM is /data/jfg/baserom.us.z64), console services from ps4_platform.c.
//
// Paths: settings, saves and the imported ROM go to /data/jfg (the "executable folder" the port caches on first use);
// read-only data the package carries (assets/) is read relative to /app0.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "ultramodern/ultramodern.hpp"
#include "librecomp/game.hpp"

#include "input/input.hpp"
#include "input/controls.hpp"
#include "renderer/renderer.hpp"
#include "config/config.hpp"
#include "recomp_api/general_api.hpp"
#include "recomp_api/recomp_data_api.hpp"
#include "ui/recomp_ui.h"

#include "runtime/audio.hpp"
#include "runtime/crash.hpp"
#include "runtime/gfx.hpp"
#include "runtime/mods.hpp"
#include "runtime/overlays.hpp"
#include "runtime/rsp.hpp"
#include "runtime/threads.hpp"

#include "ps4_platform.h"

const std::string version_string = JFG_VERSION_STRING;

extern "C" void recomp_entrypoint(uint8_t *rdram, recomp_context *ctx);
gpr get_entrypoint_address();
extern "C" void jfg_apply_asset_mods(void);
extern "C" void jfg_check_mod_replacements(void);
extern "C" void jfg_locale_start(void);
extern "C" void jfg_on_init(uint8_t* rdram, recomp_context* ctx);
extern "C" void jfg_register_mod_exports(void);
std::vector<uint8_t> jfg_prelink_rom(std::span<const uint8_t> rom);

static void custom_recomp_entrypoint(uint8_t *rdram, recomp_context *ctx) {
    ps4_log("game: entry point (RDRAM at %p)", rdram);
    dino::runtime::crash_register_rdram(rdram);
    dino::runtime::crash_register_thread_context(ctx);
    jfg_apply_asset_mods();
    jfg_check_mod_replacements();
    jfg_locale_start();
    recomp_entrypoint(rdram, ctx);
}

// Every N64 thread the game creates, for the watchdog's dumps (renderer_ps4.cpp).
extern "C" {
    uint32_t jfg_ps4_threads[64];
    std::atomic<int> jfg_ps4_thread_count{0};
}

// See src/main.cpp: JFG's osCreateThread starts every thread with SR_FR set except those with a negative id.
static void custom_thread_create_callback(uint8_t* rdram, recomp_context* ctx) {
    dino::runtime::crash_register_thread_context(ctx);
    PTR(OSThread) self = ultramodern::this_thread();
    if (self != NULLPTR) {
        const int n = jfg_ps4_thread_count.load();
        if (n < 64) {
            jfg_ps4_threads[n] = (uint32_t)self;
            jfg_ps4_thread_count.store(n + 1);
        }
    }
    const bool fr = (self == NULLPTR) || (TO_PTR(OSThread, self)->id >= 0);
    if (fr) {
        ctx->f_odd = &ctx->f1.u32l;
        ctx->mips3_float_mode = true;
        ctx->status_reg |= 0x04000000u; // SR_FR
    }
}

static std::vector<recomp::GameEntry> supported_games = {
    {
        .rom_hash = 0x44830C263C2DE108, // XXH3-64 of the US ROM
        .internal_name = "JET FORCE GEMINI    ",
        .game_id = u8"jfg-us",
        .mod_game_id = "jet-force-gemini",
        .save_type = recomp::SaveType::Flashram,
        .is_enabled = true,
        .decompression_routine = jfg_prelink_rom,
        .has_compressed_code = true,
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = custom_recomp_entrypoint,
        .thread_create_callback = custom_thread_create_callback,
        .on_init_callback = jfg_on_init,
    },
};

static ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t data) {
    return dino::runtime::create_window(data, PS4_DISPLAY_WIDTH, PS4_DISPLAY_HEIGHT);
}

static const char* find_rom() {
    static const char* const candidates[] = {
        JFG_DATA_DIR "/baserom.us.z64",
        JFG_DATA_DIR "/Jet Force Gemini (USA).z64",
        JFG_DATA_DIR "/rom.z64",
    };
    for (const char* path : candidates) {
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 0) return path;
    }
    return nullptr;
}

static int game_main() {
    ps4_log_open();
    ps4_crash_install();
    ps4_clock_selftest();
    {
        // The runtime measures time with high_resolution_clock (libc++: clock_gettime(CLOCK_MONOTONIC)).
        const auto t0 = std::chrono::high_resolution_clock::now();
        const double p0 = ps4_time_seconds();
        usleep(100000);
        const double clock_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
        ps4_log("high_resolution_clock over a 100 ms sleep: %.1f ms (process time %.1f ms)", clock_ms,
                (ps4_time_seconds() - p0) * 1000.0);
    }
    ps4_log("Jet Force Gemini Recompiled PS4 %s starting", version_string.c_str());
    ps4_pads_init();
    if (!ps4_video_init()) return 1;
    ps4_hide_splash();

    // The port caches its "executable folder" on first use: make it /data/jfg, then read packaged data from /app0.
    mkdir(JFG_DATA_DIR, 0777);
    chdir(JFG_DATA_DIR);
    ps4_log("app folder: %s", dino::config::get_app_folder_path().string().c_str());
    chdir("/app0");

    recomp::Version project_version{};
    if (!recomp::Version::from_string(version_string, project_version)) {
        ps4_log("invalid version string %s", version_string.c_str());
        return 1;
    }

    dino::runtime::reset_audio(48000);

    recomp::register_config_path(dino::config::get_app_folder_path());
    for (const auto& game : supported_games) recomp::register_game(game);
    recomp::check_all_stored_roms();

    if (!recomp::is_rom_valid(supported_games[0].game_id)) {
        const char* rom = find_rom();
        if (rom == nullptr) {
            ps4_log("no ROM: copy Jet Force Gemini (USA) .z64 to " JFG_DATA_DIR "/baserom.us.z64");
            return 1;
        }
        const recomp::RomValidationError error = recomp::select_rom(rom, supported_games[0].game_id);
        ps4_log("ROM %s: %s", rom, error == recomp::RomValidationError::Good ? "accepted" : "REJECTED (wrong or modified ROM)");
        if (error != recomp::RomValidationError::Good) return 1;
        recomp::check_all_stored_roms();
    }

    jfg_register_mod_exports();
    dino::runtime::register_overlays();
    dino::config::load_config();

    recomp::rsp::callbacks_t rsp_callbacks{
        .get_rsp_microcode = dino::runtime::get_rsp_microcode,
    };
    ultramodern::renderer::callbacks_t renderer_callbacks{
        .create_render_context = dino::renderer::create_render_context,
    };
    ultramodern::gfx_callbacks_t gfx_callbacks{
        .create_gfx = dino::runtime::create_gfx,
        .create_window = create_window,
        .update_gfx = dino::runtime::update_gfx,
    };
    ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = dino::runtime::queue_samples,
        .get_frames_remaining = dino::runtime::get_frames_remaining,
        .set_frequency = dino::runtime::set_frequency,
    };
    ultramodern::input::callbacks_t input_callbacks{
        .poll_input = dino::input::poll_inputs,
        .get_input = dino::input::get_n64_input,
        .set_rumble = dino::input::set_rumble,
        .get_connected_device_info = dino::input::get_connected_device_info,
    };
    ultramodern::events::callbacks_t events_callbacks{
        .vi_callback = dino::input::update_rumble,
        .gfx_init_callback = recompui::update_supported_options,
    };
    ultramodern::error_handling::callbacks_t error_handling_callbacks{
        .message_box = recompui::message_box,
    };
    ultramodern::threads::callbacks_t threads_callbacks{
        .get_game_thread_name = dino::runtime::get_game_thread_name,
    };

    dino::runtime::register_mods();

    // No launcher on the console: straight into the game.
    recomp::start_game(supported_games[0].game_id);

    recomp::Configuration config = {
        .project_version = project_version,
        .rsp_callbacks = rsp_callbacks,
        .renderer_callbacks = renderer_callbacks,
        .audio_callbacks = audio_callbacks,
        .input_callbacks = input_callbacks,
        .gfx_callbacks = gfx_callbacks,
        .events_callbacks = events_callbacks,
        .error_handling_callbacks = error_handling_callbacks,
        .threads_callbacks = threads_callbacks,
        .message_queue_control = { .requeue_si = false }, // see src/main.cpp
    };
    ps4_log("runtime: starting");
    recomp::start(config);
    ps4_log("runtime: returned");
    return 0;
}

// The recompiled game code nests deeply: run it on a large stack.
static void* game_thread(void*) {
    try {
        game_main();
    } catch (const std::exception& e) {
        ps4_log("FATAL: uncaught exception: %s", e.what());
    } catch (...) {
        ps4_log("FATAL: uncaught exception of an unknown type");
    }
    ps4_exit_to_system();
}

int main(void) {
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 16u * 1024u * 1024u);
    pthread_t thread;
    if (pthread_create(&thread, &attributes, game_thread, nullptr) != 0) game_thread(nullptr);
    pthread_join(thread, nullptr);
    return 0;
}
