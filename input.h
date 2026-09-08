#ifndef HORIZON_INPUT_H
#define HORIZON_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

bool horizon_exit_shortcut_pressed(
    uint32_t keycode, uint32_t modifiers, xkb_keysym_t keysym);

/* Returns the target VT for Ctrl+Alt+F1..F12, or 0 if not a VT shortcut. */
unsigned horizon_vt_shortcut(
    uint32_t keycode, uint32_t modifiers, xkb_keysym_t keysym);

#endif
