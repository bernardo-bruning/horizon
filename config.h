#ifndef HORIZON_CONFIG_H
#define HORIZON_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

enum horizon_action {
	HORIZON_ACTION_EXIT,
	HORIZON_ACTION_SWITCH_VT,
};

struct horizon_key_binding {
	uint32_t modifiers;
	xkb_keysym_t keysym;
	uint32_t keycode;
	enum horizon_action action;
	unsigned argument;
};

struct horizon_window_config {
	bool maximize_on_start;
	bool accept_client_fullscreen;
	int default_width;
	int default_height;
	int default_x;
	int default_y;
};

struct horizon_decoration_config {
	bool enabled;
	int border_width;
	float border_color[4];
};

struct horizon_input_config {
	int repeat_rate;
	int repeat_delay;
	const struct horizon_key_binding *bindings;
	size_t binding_count;
};

struct horizon_output_config {
	float background_color[4];
	const char *cursor_theme;
	int cursor_size;
	const char *cursor_name;
};

struct horizon_config {
	const char *seat_name;
	struct horizon_window_config window;
	struct horizon_decoration_config decoration;
	struct horizon_input_config input;
	struct horizon_output_config output;
};

extern const struct horizon_config horizon_default_config;

#endif
