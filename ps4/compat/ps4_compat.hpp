// PS4 (OpenOrbis, libc++ 11): what the C++20 code of the port expects and this libc++ lacks.
// Force-included in every C++ file of the PS4 build (-include ps4_compat.hpp), which is built with -fno-char8_t:
// libc++ 11 has no char8_t support in std::filesystem, and on the PS4 every path is plain UTF-8 anyway, so
// char8_t, std::u8string and u8"" literals all become plain char here.
#pragma once
#ifdef __cplusplus
#include <compare>
#include <cstdlib>
#include <string>
#include <string_view>

typedef char char8_t;
namespace std {
    using u8string = string;
    using u8string_view = string_view;
    // libc++ 11 has no quick_exit; nothing registers at_quick_exit handlers here.
    [[noreturn]] inline void quick_exit(int code) noexcept { _Exit(code); }
}
#endif
