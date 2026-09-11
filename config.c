#include "config.h"

#include <linux/input-event-codes.h>

#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon-keysyms.h>

static char *const foot_command[] = { "foot", NULL };
static char *const chromium_command[] = { "chromium", NULL };
static char *const wofi_command[] = { "wofi", "--show", "drun", NULL };

static const struct horizon_key_binding default_bindings[] = {
	{
		.modifiers = WLR_MODIFIER_LOGO,
		.keysym = XKB_KEY_d,
		.keycode = KEY_D,
		.action = HORIZON_ACTION_LAUNCH_COMMAND,
		.command = wofi_command,
	},
	{
		.modifiers = WLR_MODIFIER_LOGO,
		.keysym = XKB_KEY_q,
		.keycode = KEY_Q,
		.action = HORIZON_ACTION_CLOSE_WINDOW,
	},
	{
		.modifiers = WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT,
		.keysym = XKB_KEY_Return,
		.keycode = KEY_ENTER,
		.action = HORIZON_ACTION_LAUNCH_COMMAND,
		.command = chromium_command,
	},
	{
		.modifiers = WLR_MODIFIER_LOGO,
		.keysym = XKB_KEY_Return,
		.keycode = KEY_ENTER,
		.action = HORIZON_ACTION_LAUNCH_COMMAND,
		.command = foot_command,
	},
	{
		.modifiers = WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_f,
		.keycode = KEY_F,
		.action = HORIZON_ACTION_TOGGLE_MAXIMIZE,
	},
	{
		.modifiers = WLR_MODIFIER_LOGO,
		.keysym = XKB_KEY_f,
		.keycode = KEY_F,
		.action = HORIZON_ACTION_FULLSCREEN,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_BackSpace,
		.keycode = KEY_BACKSPACE,
		.action = HORIZON_ACTION_EXIT,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F1,
		.keycode = KEY_F1,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 1,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F2,
		.keycode = KEY_F2,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 2,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F3,
		.keycode = KEY_F3,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 3,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F4,
		.keycode = KEY_F4,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 4,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F5,
		.keycode = KEY_F5,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 5,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F6,
		.keycode = KEY_F6,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 6,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F7,
		.keycode = KEY_F7,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 7,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F8,
		.keycode = KEY_F8,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 8,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F9,
		.keycode = KEY_F9,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 9,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F10,
		.keycode = KEY_F10,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 10,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F11,
		.keycode = KEY_F11,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 11,
	},
	{
		.modifiers = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
		.keysym = XKB_KEY_F12,
		.keycode = KEY_F12,
		.action = HORIZON_ACTION_SWITCH_VT,
		.argument = 12,
	},
};

const struct horizon_config horizon_default_config = {
	.seat_name = "seat0",
	.window = {
		.tile_on_start = true,
		.maximize_on_start = false,
		.accept_client_fullscreen = true,
		.default_width = 800,
		.default_height = 600,
		.default_x = 80,
		.default_y = 80,
	},
	.decoration = {
		.enabled = true,
		.border_width = 2,
		.border_color = { 0.25f, 0.55f, 0.95f, 1.0f },
	},
	.input = {
		.repeat_rate = 25,
		.repeat_delay = 600,
		.bindings = default_bindings,
		.binding_count = sizeof(default_bindings) / sizeof(default_bindings[0]),
	},
	.output = {
		.background_color = { 0.08f, 0.12f, 0.20f, 1.0f },
		.cursor_theme = "Adwaita",
		.cursor_size = 24,
		.cursor_name = "left_ptr",
	},
};
