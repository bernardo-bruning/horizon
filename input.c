#include "input.h"

#include <linux/input-event-codes.h>

#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon-keysyms.h>

bool horizon_exit_shortcut_pressed(
    uint32_t keycode, uint32_t modifiers, xkb_keysym_t keysym) {
    const uint32_t required_modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT;
    const bool has_required_modifiers =
        (modifiers & required_modifiers) == required_modifiers;
    const bool is_backspace =
        keycode == KEY_BACKSPACE || keysym == XKB_KEY_BackSpace;

    return has_required_modifiers && is_backspace;
}

unsigned horizon_vt_shortcut(
    uint32_t keycode, uint32_t modifiers, xkb_keysym_t keysym) {
    const uint32_t required_modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT;
    if ((modifiers & required_modifiers) != required_modifiers) {
        return 0;
    }

    if (keysym >= XKB_KEY_F1 && keysym <= XKB_KEY_F12) {
        return (unsigned)(keysym - XKB_KEY_F1) + 1;
    }

    /* Keep working if xkb has not produced a keysym yet. */
    if (keycode >= KEY_F1 && keycode <= KEY_F12) {
        return (unsigned)(keycode - KEY_F1) + 1;
    }

    return 0;
}
