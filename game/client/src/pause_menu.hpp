#pragma once

// Pause menu overlay (RESUME / AUTO-JUMP toggle / QUIT). Moved verbatim out of
// main.cpp by T-M1 (pure code motion): the hit-test rectangles, the 44 px
// AUTO-JUMP band, every colour and the draw order are unchanged.
//
// The menu only *reports* what the player clicked; the state it acts on
// (unpausing, resetting the tick clock, re-locking the cursor, closing the
// window) lives in main.cpp, which applies the outcome in the same order as
// before the split.

#include "opencraft/render/rhi.hpp"

// Forward declaration: the menu only ever holds the handle, so no GLFW include.
struct GLFWwindow;

namespace opencraft::client {

// GL resources the menu draws through.
struct PauseMenuResources {
    GLFWwindow *window;
    const render::Shader &flat_shader; // backdrops and buttons
    const render::Shader &text_shader; // bitmap-font labels
    const render::Texture2D &font;
};

// Outcome of one paused frame.
struct PauseMenuOut {
    bool resume = false;           // RESUME was clicked this frame
    bool quit = false;             // QUIT was clicked this frame
    bool auto_jump_enabled = true; // option value after this frame's click edge
    bool clicked = false;          // raw left-button state, for the edge detector
};

// Draws the menu over the live scene and returns the click outcome. The caller
// restores the GL state it was disabled from (as the pre-split code did).
[[nodiscard]] PauseMenuOut draw_pause_menu(const PauseMenuResources &res, bool auto_jump_enabled,
                                           bool prev_menu_clicked, int fb_width, int fb_height);

} // namespace opencraft::client
