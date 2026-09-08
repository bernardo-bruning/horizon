#include <assert.h>
#include <linux/input-event-codes.h>
#include <stdio.h>

#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "../input.h"

static void test_exit_shortcut_accepts_physical_backspace(void) {
    assert(horizon_exit_shortcut_pressed(
        KEY_BACKSPACE,
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
        XKB_KEY_NoSymbol));
}

static void test_exit_shortcut_accepts_backspace_keysym(void) {
    assert(horizon_exit_shortcut_pressed(
        999,
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
        XKB_KEY_BackSpace));
}

static void test_exit_shortcut_allows_other_modifiers(void) {
    assert(horizon_exit_shortcut_pressed(
        KEY_BACKSPACE,
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_CAPS,
        XKB_KEY_BackSpace));
}

static void test_exit_shortcut_rejects_missing_modifiers(void) {
    assert(!horizon_exit_shortcut_pressed(
        KEY_BACKSPACE, WLR_MODIFIER_CTRL, XKB_KEY_BackSpace));
    assert(!horizon_exit_shortcut_pressed(
        KEY_BACKSPACE, WLR_MODIFIER_ALT, XKB_KEY_BackSpace));
    assert(!horizon_exit_shortcut_pressed(
        KEY_BACKSPACE, 0, XKB_KEY_BackSpace));
}

static void test_exit_shortcut_rejects_other_keys(void) {
    assert(!horizon_exit_shortcut_pressed(
        KEY_ENTER,
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
        XKB_KEY_Return));
}

int main(void) {
    test_exit_shortcut_accepts_physical_backspace();
    test_exit_shortcut_accepts_backspace_keysym();
    test_exit_shortcut_allows_other_modifiers();
    test_exit_shortcut_rejects_missing_modifiers();
    test_exit_shortcut_rejects_other_keys();
    puts("input tests: ok");
    return 0;
}
