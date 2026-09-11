#include "input.h"

#include <wlr/types/wlr_keyboard.h>

const struct horizon_key_binding *horizon_find_key_binding(
    const struct horizon_input_config *config,
    uint32_t keycode, uint32_t modifiers, xkb_keysym_t keysym) {
    if (config == NULL || config->bindings == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < config->binding_count; i++) {
        const struct horizon_key_binding *binding = &config->bindings[i];
        bool key_matches = (binding->keysym != 0 && binding->keysym == keysym) ||
            (binding->keycode != 0 && binding->keycode == keycode);
        if ((modifiers & binding->modifiers) == binding->modifiers &&
            key_matches) {
            return binding;
        }
    }

    return NULL;
}
