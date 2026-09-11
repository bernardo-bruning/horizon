#ifndef HORIZON_INPUT_H
#define HORIZON_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

#include "config.h"

const struct horizon_key_binding *horizon_find_key_binding(
	const struct horizon_input_config *config,
	uint32_t keycode, uint32_t modifiers, xkb_keysym_t keysym);

#endif
