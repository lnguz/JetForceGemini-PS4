// PS4: replaces src/renderer/renderer.hpp (RT64). Same dino::renderer API, without RT64 types; the renderer itself is
// ps4/src/gfx_*.cpp (GNM).
#pragma once

#include <memory>
#include <string>
#include <filesystem>

#include "ultramodern/renderer_context.hpp"
#include "librecomp/mods.hpp"

namespace dino::renderer {
    inline const std::string special_option_texture_pack_enabled = "_recomp_texture_pack_enabled";

    std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(uint8_t *rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode);

    // The PS4 renderer has no MSAA sample positions or RT64's high-precision framebuffers.
    inline bool RT64SamplePositionsSupported() { return false; }
    inline bool RT64HighPrecisionFBEnabled() { return false; }
    inline void preload_common_shaders() {}

    // Texture packs (RT64 .rtz) are not supported on PS4 yet: the mod system still registers them, they do nothing.
    inline void trigger_texture_pack_update() {}
    inline void enable_texture_pack(const recomp::mods::ModContext&, const recomp::mods::ModHandle&) {}
    inline void disable_texture_pack(const recomp::mods::ModHandle&) {}
    inline void secondary_enable_texture_pack(const std::string&) {}
    inline void secondary_disable_texture_pack(const std::string&) {}
    inline bool is_texture_pack_enable_config_option(const recomp::mods::ConfigOption&, bool) { return false; }
}
