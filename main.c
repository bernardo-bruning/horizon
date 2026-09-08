#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <wayland-server-core.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "input.h"

#ifdef HORIZON_DEBUG
#define HORIZON_DEBUG_LOG(...) do { \
    fprintf(stderr, "[DEBUG] "); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
} while (0)
#else
#define HORIZON_DEBUG_LOG(...) do { } while (0)
#endif

struct horizon_server {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_compositor *compositor;
    struct wlr_xdg_shell *xdg_shell;
    struct wlr_scene *scene;
    struct wlr_seat *seat;
    struct wl_listener new_output;
    struct wl_listener new_input;
    struct wl_listener new_toplevel;
};

struct horizon_output {
    struct wlr_output *output;
    struct wlr_scene_output *scene_output;
    struct wl_listener frame;
    struct wl_listener destroy;
};

struct horizon_xdg_toplevel {
    struct wlr_xdg_toplevel *toplevel;
    struct wlr_scene_tree *scene_tree;
    bool configured;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct horizon_keyboard {
    struct wlr_keyboard *keyboard;
    struct wl_listener key;
    struct wl_listener destroy;
    struct horizon_server *server;
};

static bool configure_keyboard(struct wlr_keyboard *keyboard) {
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context == NULL) {
        fprintf(stderr, "horizon: failed to create XKB context\n");
        return false;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_names(
        context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == NULL) {
        fprintf(stderr, "horizon: failed to create XKB keymap\n");
        xkb_context_unref(context);
        return false;
    }

    bool configured = wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    if (!configured) {
        fprintf(stderr, "horizon: failed to set keyboard keymap\n");
        return false;
    }

    wlr_keyboard_set_repeat_info(keyboard, 25, 600);
    return true;
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_keyboard *keyboard =
        wl_container_of(listener, keyboard, destroy);
    if (wlr_seat_get_keyboard(keyboard->server->seat) == keyboard->keyboard) {
        wlr_seat_set_keyboard(keyboard->server->seat, NULL);
        wlr_seat_set_capabilities(keyboard->server->seat, 0);
    }
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    free(keyboard);
}

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
    struct horizon_keyboard *horizon_keyboard =
        wl_container_of(listener, horizon_keyboard, key);
    struct wlr_keyboard_key_event *event = data;

    if (event->state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        return;
    }

    uint32_t modifiers = wlr_keyboard_get_modifiers(horizon_keyboard->keyboard);
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;
    if (horizon_keyboard->keyboard->xkb_state != NULL) {
        keysym = xkb_state_key_get_one_sym(
            horizon_keyboard->keyboard->xkb_state, event->keycode + 8);
    }
    if (horizon_exit_shortcut_pressed(event->keycode, modifiers, keysym)) {
        printf("Exiting horizon (Ctrl+Alt+Backspace)\n");
        fflush(stdout);
        wl_display_terminate(horizon_keyboard->server->display);
    }
}

static void handle_xdg_map(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_xdg_toplevel *view =
        wl_container_of(listener, view, map);
    wlr_xdg_toplevel_set_activated(view->toplevel, true);
    HORIZON_DEBUG_LOG("xdg map: title=%s", view->toplevel->title != NULL ?
        view->toplevel->title : "untitled");
    printf("XDG toplevel mapped: %s\n",
        view->toplevel->title != NULL ? view->toplevel->title : "untitled");
    fflush(stdout);
}

static void handle_xdg_unmap(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_xdg_toplevel *view =
        wl_container_of(listener, view, unmap);
    HORIZON_DEBUG_LOG("xdg unmap: title=%s", view->toplevel->title != NULL ?
        view->toplevel->title : "untitled");
}

static void handle_xdg_commit(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_xdg_toplevel *view =
        wl_container_of(listener, view, commit);
    HORIZON_DEBUG_LOG("xdg commit: initialized=%d initial=%d mapped=%d",
        view->toplevel->base->initialized,
        view->toplevel->base->initial_commit,
        view->toplevel->base->surface->mapped);

    if (!view->configured && view->toplevel->base->initialized) {
        wlr_xdg_toplevel_set_size(view->toplevel, 800, 600);
        wlr_xdg_toplevel_set_activated(view->toplevel, true);
        view->configured = true;
        HORIZON_DEBUG_LOG("xdg initial configure sent: size=800x600");
    }
}

static void handle_xdg_destroy(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_xdg_toplevel *view =
        wl_container_of(listener, view, destroy);
    HORIZON_DEBUG_LOG("xdg destroy");
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->destroy.link);
    free(view);
}

static void handle_new_toplevel(struct wl_listener *listener, void *data) {
    struct horizon_server *server =
        wl_container_of(listener, server, new_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;

    struct horizon_xdg_toplevel *view = calloc(1, sizeof(*view));
    if (view == NULL) {
        fprintf(stderr, "horizon: failed to allocate xdg toplevel\n");
        return;
    }

    view->toplevel = toplevel;
    view->scene_tree = wlr_scene_xdg_surface_create(
        &server->scene->tree, toplevel->base);
    if (view->scene_tree == NULL) {
        fprintf(stderr, "horizon: failed to create xdg scene surface\n");
        free(view);
        return;
    }
    wlr_scene_node_set_position(&view->scene_tree->node, 80, 80);

    view->map.notify = handle_xdg_map;
    view->unmap.notify = handle_xdg_unmap;
    view->commit.notify = handle_xdg_commit;
    view->destroy.notify = handle_xdg_destroy;
    wl_signal_add(&toplevel->base->surface->events.map, &view->map);
    wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);
    wl_signal_add(&toplevel->base->surface->events.commit, &view->commit);
    wl_signal_add(&toplevel->base->events.destroy, &view->destroy);

    printf("XDG toplevel ready: %s\n",
        toplevel->title != NULL ? toplevel->title : "untitled");
    fflush(stdout);
}

static void handle_new_input(struct wl_listener *listener, void *data) {
    struct horizon_server *server =
        wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    if (device->type != WLR_INPUT_DEVICE_KEYBOARD) {
        return;
    }

    struct horizon_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    if (keyboard == NULL) {
        fprintf(stderr, "horizon: failed to allocate keyboard state\n");
        return;
    }

    keyboard->server = server;
    keyboard->keyboard = wlr_keyboard_from_input_device(device);
    if (!configure_keyboard(keyboard->keyboard)) {
        free(keyboard);
        return;
    }

    wlr_seat_set_keyboard(server->seat, keyboard->keyboard);
    wlr_seat_set_capabilities(server->seat, WL_SEAT_CAPABILITY_KEYBOARD);

    keyboard->key.notify = handle_keyboard_key;
    keyboard->destroy.notify = handle_keyboard_destroy;
    wl_signal_add(&keyboard->keyboard->events.key, &keyboard->key);
    wl_signal_add(&device->events.destroy, &keyboard->destroy);
}

static void render_frame(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_output *horizon_output =
        wl_container_of(listener, horizon_output, frame);
    if (!wlr_scene_output_commit(horizon_output->scene_output, NULL)) {
        fprintf(stderr, "horizon: failed to commit output frame\n");
    }
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_output *horizon_output =
        wl_container_of(listener, horizon_output, destroy);
    wlr_scene_output_destroy(horizon_output->scene_output);
    wl_list_remove(&horizon_output->frame.link);
    wl_list_remove(&horizon_output->destroy.link);
    free(horizon_output);
}

static void handle_new_output(struct wl_listener *listener, void *data) {
    struct horizon_server *server =
        wl_container_of(listener, server, new_output);
    struct wlr_output *output = data;

    if (!wlr_output_init_render(output, server->allocator, server->renderer)) {
        fprintf(stderr, "horizon: failed to initialize output renderer\n");
        return;
    }

    struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (mode != NULL) {
        wlr_output_state_set_mode(&state, mode);
    }

    if (!wlr_output_commit_state(output, &state)) {
        fprintf(stderr, "horizon: failed to configure output %s\n", output->name);
        wlr_output_state_finish(&state);
        return;
    }
    wlr_output_state_finish(&state);

    wlr_output_create_global(output, server->display);

    struct horizon_output *horizon_output = calloc(1, sizeof(*horizon_output));
    if (horizon_output == NULL) {
        fprintf(stderr, "horizon: failed to allocate output state\n");
        return;
    }
    horizon_output->output = output;
    horizon_output->scene_output = wlr_scene_output_create(server->scene, output);
    if (horizon_output->scene_output == NULL) {
        fprintf(stderr, "horizon: failed to create scene output\n");
        free(horizon_output);
        return;
    }

    const float background_color[] = { 0.08f, 0.12f, 0.20f, 1.0f };
    wlr_scene_rect_create(&server->scene->tree, output->width,
        output->height, background_color);
    horizon_output->frame.notify = render_frame;
    horizon_output->destroy.notify = handle_output_destroy;
    wl_signal_add(&output->events.frame, &horizon_output->frame);
    wl_signal_add(&output->events.destroy, &horizon_output->destroy);

    printf("Output ready: %s (%dx%d)\n", output->name, output->width, output->height);
    fflush(stdout);
    wlr_output_schedule_frame(output);
}

int main(void) {
    struct horizon_server server = {0};
    server.display = wl_display_create();
    if (server.display == NULL) {
        fprintf(stderr, "horizon: failed to create Wayland display\n");
        return EXIT_FAILURE;
    }

    const char *socket = wl_display_add_socket_auto(server.display);
    if (socket == NULL) {
        fprintf(stderr, "horizon: failed to create Wayland socket\n");
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.seat = wlr_seat_create(server.display, "seat0");
    if (server.seat == NULL) {
        fprintf(stderr, "horizon: failed to create seat\n");
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.backend = wlr_backend_autocreate(
        wl_display_get_event_loop(server.display), NULL);
    if (server.backend == NULL) {
        fprintf(stderr, "horizon: failed to create wlroots backend\n");
        wlr_seat_destroy(server.seat);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (server.renderer == NULL ||
        !wlr_renderer_init_wl_display(server.renderer, server.display)) {
        fprintf(stderr, "horizon: failed to initialize renderer\n");
        wlr_backend_destroy(server.backend);
        wlr_seat_destroy(server.seat);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.compositor = wlr_compositor_create(server.display, 6, server.renderer);
    if (server.compositor == NULL ||
        wlr_subcompositor_create(server.display) == NULL) {
        fprintf(stderr, "horizon: failed to create compositor globals\n");
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wlr_seat_destroy(server.seat);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.xdg_shell = wlr_xdg_shell_create(server.display, 3);
    server.scene = wlr_scene_create();
    if (server.xdg_shell == NULL || server.scene == NULL) {
        fprintf(stderr, "horizon: failed to create xdg-shell scene\n");
        if (server.scene != NULL) {
            wlr_scene_node_destroy(&server.scene->tree.node);
        }
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wlr_seat_destroy(server.seat);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (server.allocator == NULL) {
        fprintf(stderr, "horizon: failed to create allocator\n");
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wlr_seat_destroy(server.seat);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.new_output.notify = handle_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);
    server.new_input.notify = handle_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);
    server.new_toplevel.notify = handle_new_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_toplevel);

    if (!wlr_backend_start(server.backend)) {
        fprintf(stderr, "horizon: failed to start wlroots backend\n");
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_seat_destroy(server.seat);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    printf("Hello from horizon, a Wayland compositor!\n");
    printf("Seat ready: seat0\n");
    printf("Listening on WAYLAND_DISPLAY=%s\n", socket);
    fflush(stdout);
    wl_display_run(server.display);

    wl_list_remove(&server.new_input.link);
    wl_list_remove(&server.new_output.link);
    wl_list_remove(&server.new_toplevel.link);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_seat_destroy(server.seat);
    wl_display_destroy(server.display);
    return 0;
}
