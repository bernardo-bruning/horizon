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

static void test_application_shortcuts(void) {
	const struct horizon_key_binding *wofi = binding_for(KEY_D,
	    WLR_MODIFIER_LOGO, XKB_KEY_d);
	const struct horizon_key_binding *foot = binding_for(KEY_ENTER,
	    WLR_MODIFIER_LOGO, XKB_KEY_Return);
	const struct horizon_key_binding *chromium = binding_for(KEY_ENTER,
	    WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT, XKB_KEY_Return);
    const struct horizon_key_binding *maximize = binding_for(KEY_F,
        WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT, XKB_KEY_f);
    const struct horizon_key_binding *fullscreen = binding_for(KEY_F,
        WLR_MODIFIER_LOGO, XKB_KEY_f);
    const struct horizon_key_binding *close = binding_for(KEY_Q,
        WLR_MODIFIER_LOGO, XKB_KEY_q);

	assert(wofi != NULL && wofi->action == HORIZON_ACTION_LAUNCH_COMMAND);
	assert(wofi->command != NULL && wofi->command[0][0] == 'w');
	assert(wofi->command[1] != NULL && wofi->command[1][0] == '-');
	assert(foot != NULL && foot->action == HORIZON_ACTION_LAUNCH_COMMAND);
	assert(foot->command != NULL && foot->command[0][0] == 'f');
    assert(chromium != NULL &&
        chromium->action == HORIZON_ACTION_LAUNCH_COMMAND);
    assert(chromium->command != NULL && chromium->command[0][0] == 'c');
    assert(maximize != NULL &&
        maximize->action == HORIZON_ACTION_TOGGLE_MAXIMIZE);
    assert(fullscreen != NULL &&
        fullscreen->action == HORIZON_ACTION_FULLSCREEN);
    assert(close != NULL &&
        close->action == HORIZON_ACTION_CLOSE_WINDOW);
}

static void test_focus_direction_shortcuts(void) {
    const struct horizon_key_binding *left = binding_for(KEY_H,
        WLR_MODIFIER_LOGO, XKB_KEY_h);
    const struct horizon_key_binding *left_arrow = binding_for(KEY_LEFT,
        WLR_MODIFIER_LOGO, XKB_KEY_Left);
    const struct horizon_key_binding *down = binding_for(KEY_J,
        WLR_MODIFIER_LOGO, XKB_KEY_j);
    const struct horizon_key_binding *down_arrow = binding_for(KEY_DOWN,
        WLR_MODIFIER_LOGO, XKB_KEY_Down);
    const struct horizon_key_binding *up = binding_for(KEY_K,
        WLR_MODIFIER_LOGO, XKB_KEY_k);
    const struct horizon_key_binding *up_arrow = binding_for(KEY_UP,
        WLR_MODIFIER_LOGO, XKB_KEY_Up);
    const struct horizon_key_binding *right = binding_for(KEY_L,
        WLR_MODIFIER_LOGO, XKB_KEY_l);
    const struct horizon_key_binding *right_arrow = binding_for(KEY_RIGHT,
        WLR_MODIFIER_LOGO, XKB_KEY_Right);

    assert(left != NULL && left->action == HORIZON_ACTION_FOCUS_DIRECTION &&
        left->argument == HORIZON_DIRECTION_LEFT);
    assert(left_arrow != NULL &&
        left_arrow->action == HORIZON_ACTION_FOCUS_DIRECTION &&
        left_arrow->argument == HORIZON_DIRECTION_LEFT);
    assert(down != NULL && down->action == HORIZON_ACTION_FOCUS_DIRECTION &&
        down->argument == HORIZON_DIRECTION_DOWN);
    assert(down_arrow != NULL &&
        down_arrow->action == HORIZON_ACTION_FOCUS_DIRECTION &&
        down_arrow->argument == HORIZON_DIRECTION_DOWN);
    assert(up != NULL && up->action == HORIZON_ACTION_FOCUS_DIRECTION &&
        up->argument == HORIZON_DIRECTION_UP);
    assert(up_arrow != NULL &&
        up_arrow->action == HORIZON_ACTION_FOCUS_DIRECTION &&
        up_arrow->argument == HORIZON_DIRECTION_UP);
    assert(right != NULL && right->action == HORIZON_ACTION_FOCUS_DIRECTION &&
        right->argument == HORIZON_DIRECTION_RIGHT);
    assert(right_arrow != NULL &&
        right_arrow->action == HORIZON_ACTION_FOCUS_DIRECTION &&
        right_arrow->argument == HORIZON_DIRECTION_RIGHT);
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
    test_application_shortcuts();
    test_focus_direction_shortcuts();
    puts("input tests: ok");
    return 0;
}
