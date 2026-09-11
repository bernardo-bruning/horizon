#include <assert.h>
#include <linux/input-event-codes.h>
#include <stdio.h>

#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "../input.h"

static const struct horizon_key_binding *binding_for(
    uint32_t keycode, uint32_t modifiers, xkb_keysym_t keysym) {
    return horizon_find_key_binding(&horizon_default_config.input,
        keycode, modifiers, keysym);
}

static void test_exit_shortcut_accepts_physical_backspace(void) {
    const struct horizon_key_binding *binding = binding_for(
        KEY_BACKSPACE, WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
        XKB_KEY_NoSymbol);
    assert(binding != NULL && binding->action == HORIZON_ACTION_EXIT);
}

static void test_exit_shortcut_accepts_backspace_keysym(void) {
    const struct horizon_key_binding *binding = binding_for(
        999, WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_BackSpace);
    assert(binding != NULL && binding->action == HORIZON_ACTION_EXIT);
}

static void test_exit_shortcut_allows_other_modifiers(void) {
    const struct horizon_key_binding *binding = binding_for(KEY_BACKSPACE,
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_CAPS,
        XKB_KEY_BackSpace);
    assert(binding != NULL && binding->action == HORIZON_ACTION_EXIT);
}

static void test_exit_shortcut_rejects_missing_modifiers(void) {
    assert(binding_for(KEY_BACKSPACE, WLR_MODIFIER_CTRL,
        XKB_KEY_BackSpace) == NULL);
    assert(binding_for(KEY_BACKSPACE, WLR_MODIFIER_ALT,
        XKB_KEY_BackSpace) == NULL);
    assert(binding_for(KEY_BACKSPACE, 0, XKB_KEY_BackSpace) == NULL);
}

static void test_exit_shortcut_rejects_other_keys(void) {
    assert(binding_for(KEY_ENTER, WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
        XKB_KEY_Return) == NULL);
}

static void test_vt_shortcuts_map_function_keys(void) {
    const struct horizon_key_binding *first = binding_for(KEY_F1,
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F1);
    const struct horizon_key_binding *last = binding_for(KEY_F12,
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F12);
    assert(first != NULL && first->argument == 1);
    assert(last != NULL && last->argument == 12);
}

static void test_vt_shortcuts_accept_extra_modifiers(void) {
    const struct horizon_key_binding *binding = binding_for(KEY_F3,
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_CAPS,
        XKB_KEY_F3);
    assert(binding != NULL && binding->argument == 3);
}

static void test_vt_shortcuts_reject_invalid_combinations(void) {
    assert(binding_for(KEY_F1, WLR_MODIFIER_CTRL, XKB_KEY_F1) == NULL);
    assert(binding_for(KEY_ENTER, WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
        XKB_KEY_Return) == NULL);
    const struct horizon_key_binding *binding = binding_for(KEY_F1,
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_NoSymbol);
    assert(binding != NULL && binding->argument == 1);
}

int main(void) {
    test_exit_shortcut_accepts_physical_backspace();
    test_exit_shortcut_accepts_backspace_keysym();
    test_exit_shortcut_allows_other_modifiers();
    test_exit_shortcut_rejects_missing_modifiers();
    test_exit_shortcut_rejects_other_keys();
    test_vt_shortcuts_map_function_keys();
    test_vt_shortcuts_accept_extra_modifiers();
    test_vt_shortcuts_reject_invalid_combinations();
    puts("input tests: ok");
    return 0;
}
