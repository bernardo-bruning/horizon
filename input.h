#ifndef HORIZON_INPUT_H
#define HORIZON_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

bool horizon_exit_shortcut_pressed(
    uint32_t keycode, uint32_t modifiers, xkb_keysym_t keysym);

#endif
