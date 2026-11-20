// Jet Force Gemini PS4: what the parts of the port left out of the PS4 build (RT64 renderer, the RmlUi menus' RT64
// pieces, ImGui backend, N64Recomp's live recompiler) define for the rest. Each one keeps the behaviour the game needs:
// settings are plain values (config.cpp loads and saves them as on PC), the menus report "closed", the RT64 tuning
// values have their defaults, and code mods are refused.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <filesystem>
#include <pthread.h>
#include <string>
#include <vector>

#include "ultramodern/ultramodern.hpp"
#include "config/config.hpp"
#include "input/input.hpp"
#include "ui/recomp_ui.h"
#include "debug_ui/backend.hpp"
#include "jfg/jfg_rt64.h"
#include "recompiler/context.h"
#include "recompiler/live_recompiler.h"

#include "ps4_platform.h"

#include <orbis/libkernel.h>

// ---------------------------------------------------------------- settings (src/ui/ui_config.cpp on PC)

namespace {
    std::atomic<int> main_volume{100};
    int rumble_strength = 25;
    int gyro_sensitivity = 50;
    int mouse_sensitivity = 0;
    int joystick_deadzone = 5;
    int joystick_range = 100;
    dino::input::BackgroundInputMode background_input_mode = dino::input::BackgroundInputMode::On;
    int scanned_input_index = -1;
}

int dino::config::get_main_volume() { return main_volume.load(); }
void dino::config::set_main_volume(int volume) { main_volume.store(volume); }
void dino::config::reset_sound_settings() { main_volume.store(100); }
void dino::config::open_quit_game_prompt(const std::string&) {}

int dino::input::get_rumble_strength() { return rumble_strength; }
void dino::input::set_rumble_strength(int v) { rumble_strength = v; }
int dino::input::get_gyro_sensitivity() { return gyro_sensitivity; }
void dino::input::set_gyro_sensitivity(int v) { gyro_sensitivity = v; }
int dino::input::get_mouse_sensitivity() { return mouse_sensitivity; }
void dino::input::set_mouse_sensitivity(int v) { mouse_sensitivity = v; }
int dino::input::get_joystick_deadzone() { return joystick_deadzone; }
void dino::input::set_joystick_deadzone(int v) { joystick_deadzone = v; }
int dino::input::get_joystick_range() { return joystick_range; }
void dino::input::set_joystick_range(int v) { joystick_range = v; }
dino::input::BackgroundInputMode dino::input::get_background_input_mode() { return background_input_mode; }
void dino::input::set_background_input_mode(dino::input::BackgroundInputMode m) { background_input_mode = m; }
int dino::input::get_scanned_input_index() { return scanned_input_index; }
void dino::input::cancel_scanning_input() { dino::input::stop_scanning_input(); scanned_input_index = -1; }

// ---------------------------------------------------------------- menus (src/ui/ui_state.cpp and friends on PC)

namespace recompui {
    void message_box(const char* msg) { ps4_log("[message] %s", msg ? msg : ""); }
    void update_supported_options() {}
    void process_game_started() { ps4_log("game started"); }
    void queue_event(const SDL_Event&) {}
    void activate_mouse() {}
    void toggle_fullscreen() {}
    void get_window_size(int& width, int& height) { width = PS4_DISPLAY_WIDTH; height = PS4_DISPLAY_HEIGHT; }
    bool get_cont_active() { return true; }
    void drop_files(const std::list<std::filesystem::path>&) {}
    void update_mod_list(bool) {}
    void focus_mod_configure_button() {}
    bool focus_selected_mod() { return false; }

    void show_context(ContextId, std::string_view) {}
    void hide_context(ContextId) {}
    void hide_all_contexts() {}
    bool is_context_shown(ContextId) { return false; }
    bool is_context_capturing_input() { return false; }
    bool is_any_context_shown() { return false; }
    ContextId get_launcher_context_id() { return ContextId{ 0xFFFFFFFFu }; }
    ContextId get_config_context_id() { return ContextId{ 0xFFFFFFFFu }; }
    ContextId get_config_sub_menu_context_id() { return ContextId{ 0xFFFFFFFFu }; }
    ContextId get_config_sub_menu_parent_context() { return ContextId{ 0xFFFFFFFFu }; }

    Rml::ElementDocument* load_document(const std::filesystem::path&) { return nullptr; }
    Rml::ElementDocument* create_empty_document() { return nullptr; }
    void queue_image_from_bytes_rgba32(const std::string&, const std::vector<char>&, uint32_t, uint32_t) {}
    void queue_image_from_bytes_file(const std::string&, const std::vector<char>&) {}
    void release_image(const std::string&) {}
}

// ---------------------------------------------------------------- ImGui debug UI backend (src/debug_ui/backend.cpp)

namespace dino::debug_ui::backend {
    ImGuiContext* jfg_imgui_ctx = nullptr;
    bool is_open() { return false; }
    bool in_ui_frame() { return false; }
    void set_overlay(bool) {}
    bool take_reset_request() { return false; }
    bool take_next_pack_request() { return false; }
    bool take_fly_request() { return false; }
    void begin() {}
    void end() {}
}

// ---------------------------------------------------------------- values shared with RT64 on PC (jfg_rt64.h)

std::atomic<uint32_t> rt64_jfg_screen_fb{0};
std::atomic<uint32_t> rt64_jfg_screen_updates{0};
std::atomic<uint32_t> rt64_jfg_present_count{0};
RT64JfgPresentInfo rt64_jfg_present_info{};
std::atomic<int> rt64_jfg_ssao{0};
std::atomic<float> rt64_jfg_ssao_strength{0.0f};
std::atomic<float> rt64_jfg_ssao_radius{0.0f};
std::atomic<int> rt64_jfg_bloom{0};
std::atomic<int> rt64_jfg_paused{0};
std::atomic<float> rt64_jfg_bloom_intensity{0.0f};
std::atomic<float> rt64_jfg_bloom_threshold{0.0f};
std::atomic<float> rt64_jfg_bloom_radius{0.0f};
std::atomic<float> rt64_jfg_fov_tan{0.0f};
std::atomic<int> rt64_jfg_fog{0};
std::atomic<int> rt64_jfg_fog_blocked{0};
std::atomic<uint32_t> rt64_jfg_fog_drawn{0};
std::atomic<uint32_t> rt64_jfg_fog_game_scenes{0};
std::atomic<float> rt64_jfg_fog_start{0.0f};
std::atomic<float> rt64_jfg_fog_end{0.0f};
std::atomic<float> rt64_jfg_fog_density{0.0f};
std::atomic<uint32_t> rt64_jfg_fog_color{0};
std::atomic<int> rt64_jfg_vignette{0};
std::atomic<float> rt64_jfg_vignette_strength{0.0f};
std::atomic<float> rt64_jfg_vignette_size{0.0f};

// Interpolation tags (rt64_rsp.cpp / rt64_gbi_f3ddkr.cpp on PC): the PS4 renderer has no in-between frames yet.
uint32_t rt64_jfg_matrix_check(const uint8_t*, uint32_t) { return 0; }
void rt64_jfg_tag_matrix(uint32_t, uint32_t, uint32_t, const float*) {}
void rt64_jfg_tag_bone_buffer(uint32_t, uint32_t) {}
void rt64_jfg_shadow_batch(uint32_t, uint32_t, float, float) {}

extern "C" int jfg_renderer_adds_frames(void) { return 0; }
extern "C" int jfg_renderer_rate_needs_restart(void) { return 0; }

// ---------------------------------------------------------------- N64Recomp live recompiler (code mods)

void N64Recomp::live_recompiler_init() {}
N64Recomp::ModSymbolsError N64Recomp::parse_mod_symbols(std::span<const char>, std::span<const uint8_t>,
    const std::unordered_map<uint32_t, uint16_t>&, N64Recomp::Context&) {
    ps4_log("mods: code mods are not supported on PS4 (no live recompiler)");
    return N64Recomp::ModSymbolsError::NotASymbolFile;
}
N64Recomp::LiveGeneratorOutput::~LiveGeneratorOutput() {}
N64Recomp::ShimFunction::ShimFunction(recomp_func_ext_t*, uintptr_t) {}
N64Recomp::ShimFunction::~ShimFunction() {}

// ---------------------------------------------------------------- runtime platform hooks

void ultramodern::set_native_thread_name(const std::string& name) {
    pthread_set_name_np(pthread_self(), name.c_str());
}

void ultramodern::set_native_thread_priority(ThreadPriority) {
    // The console's scheduler priorities are left at their defaults; the game threads are cooperative anyway
    // (the runtime runs one N64 thread at a time).
}

// thread_local destructors: the runtime's threads live as long as the game, so there is nothing to run at their exit.
extern "C" int __cxa_thread_atexit_impl(void (*)(void*), void*, void*) { return 0; }

// libc++ 11's std::atomic::wait support lives in its shared library, which OpenOrbis does not ship. A short sleep is a
// valid spurious wake-up: every caller re-checks the value in a loop.
namespace std { inline namespace __1 {
    void __libcpp_atomic_wait(void const volatile*, long) { sceKernelUsleep(50); }
}}
