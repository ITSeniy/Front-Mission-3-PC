// launcher.h — SDL2/OpenGL launcher front-end.
//
// Shown in a temporary SDL window before the emulator boots: the user
// picks renderer / supersampling / AA / colour model / SPU-HQ, the BIOS,
// disc, memory cards and controllers, then presses LAUNCH. The chosen
// values are written back into the UserSettings the runtime then applies
// (and persisted to settings.toml by the caller).
//
// This module creates its own SDL window + GL 3.3 core context — the
// caller creates the window, calls run(), then tears everything down.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

namespace PSXRecompV4 { struct UserSettings; }

namespace psx_launcher {

enum class Result {
    Launch,       // user pressed LAUNCH — proceed to boot with `io`
    Quit,         // user closed the window — caller should exit
    Unavailable,  // launcher could not initialise (assets/GL); caller boots as if skipped
};

struct GameInfo {
    const char* name             = nullptr;
    const char* expected_serial  = nullptr;
    uint32_t    expected_crc     = 0;
    bool        has_expected_crc = false;
    bool        allow_hybrid     = true;
    bool        lock_mode        = false;
    int         locked_mode      = 2; // PAD_MODE_DIGITAL

    bool        lock_device      = false; // true hides the Player controller-card device selector
    bool        ws_offered       = true;  // false hides the widescreen toggle + clamps to 4:3

    struct Language { std::string code; std::string label; };
    std::vector<Language> languages;
};

Result run(SDL_Window* window, void* gl_context,
           PSXRecompV4::UserSettings& io,
           const GameInfo& game, const char* assets_dir);

} // namespace psx_launcher
