// Jet Force Gemini PS4: replaces src/runtime/gfx.cpp. There is no window: the "window" handed to the renderer is the
// sceVideoOut handle, and the event pump reads the DualShock 4 through sdl_ps4.cpp.
#include "runtime/gfx.hpp"

#include "input/input.hpp"
#include "common/sdl.hpp"
#include "ps4_platform.h"

namespace dino::runtime {

ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    SDL_Init(SDL_INIT_GAMECONTROLLER);
    dino::input::initialize_controllers();
    return {};
}

ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t, int, int) {
    if (ps4_video_handle() < 0 && !ps4_video_init()) {
        ps4_log("video out could not be opened");
    }
    return ultramodern::renderer::WindowHandle{ ps4_video_handle() };
}

SDL_Window* get_window() { return nullptr; }

void update_gfx(void*) {
    dino::input::handle_events();
}

}
