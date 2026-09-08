#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
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
    struct wlr_session *session;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_compositor *compositor;
    struct wlr_data_device_manager *data_device_manager;
    struct wlr_xdg_shell *xdg_shell;
    struct wlr_scene *scene;
    struct wlr_seat *seat;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_manager;
    struct wlr_output_layout *output_layout;
    struct wlr_output *output;
    struct wlr_xcursor *cursor_image;
    struct wlr_scene_buffer *cursor_scene;
    struct wl_list views;
    struct horizon_xdg_toplevel *focused_view;
    struct horizon_xdg_toplevel *pointer_view;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
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
    struct horizon_server *server;
    struct wl_list link;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct horizon_keyboard {
    struct wlr_keyboard *keyboard;
    struct wl_listener key;
    struct wl_listener modifiers;
    struct wl_listener destroy;
    struct horizon_server *server;
};

struct horizon_pointer {
    struct wlr_pointer *pointer;
    struct horizon_server *server;
    struct wl_listener motion;
    struct wl_listener motion_absolute;
    struct wl_listener button;
    struct wl_listener axis;
    struct wl_listener destroy;
};

static struct horizon_xdg_toplevel *view_at(
    struct horizon_server *server, double *sx, double *sy) {
    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node, server->cursor->x, server->cursor->y,
        sx, sy);

    while (node != NULL) {
        if (node->data != NULL) {
            return node->data;
        }
        if (node->type == WLR_SCENE_NODE_BUFFER) {
            struct wlr_scene_buffer *buffer =
                wlr_scene_buffer_from_node(node);
            struct wlr_scene_surface *scene_surface =
                wlr_scene_surface_try_from_buffer(buffer);
            if (scene_surface != NULL) {
                struct wlr_xdg_toplevel *toplevel =
                    wlr_xdg_toplevel_try_from_wlr_surface(
                        scene_surface->surface);
                if (toplevel != NULL) {
                    struct horizon_xdg_toplevel *view;
                    wl_list_for_each(view, &server->views, link) {
                        if (view->toplevel == toplevel) {
                            return view;
                        }
                    }
                }
            }
        }
        node = node->parent != NULL ? &node->parent->node : NULL;
    }

    return NULL;
}

static const char *view_title(struct horizon_xdg_toplevel *view) {
    if (view == NULL || view->toplevel->title == NULL) {
        return "none";
    }
    return view->toplevel->title;
}

static bool cursor_accepts_input(struct wlr_scene_buffer *buffer,
    double *sx, double *sy) {
    (void)buffer;
    (void)sx;
    (void)sy;
    return false;
}

static void enter_keyboard_focus(struct horizon_server *server,
    struct horizon_xdg_toplevel *view) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard == NULL || view == NULL) {
        return;
    }

    wlr_seat_keyboard_notify_enter(server->seat,
        view->toplevel->base->surface,
        keyboard->keycodes, keyboard->num_keycodes,
        &keyboard->modifiers);
}

static void focus_view(struct horizon_server *server,
    struct horizon_xdg_toplevel *view, double sx, double sy) {
    if (server->focused_view == view) {
        if (view != NULL) {
            wlr_seat_pointer_notify_enter(server->seat,
                view->toplevel->base->surface, sx, sy);
        }
        return;
    }

    HORIZON_DEBUG_LOG("focus changed: %s -> %s",
        view_title(server->focused_view), view_title(view));

    if (server->focused_view != NULL) {
        wlr_xdg_toplevel_set_activated(
            server->focused_view->toplevel, false);
    }
    server->focused_view = view;

    if (view == NULL) {
        wlr_seat_keyboard_notify_clear_focus(server->seat);
        wlr_seat_pointer_notify_clear_focus(server->seat);
        return;
    }

    wlr_xdg_toplevel_set_activated(view->toplevel, true);
    enter_keyboard_focus(server, view);
    HORIZON_DEBUG_LOG("keyboard focus target: %s", view_title(view));
    wlr_seat_pointer_notify_enter(server->seat,
        view->toplevel->base->surface, sx, sy);
}

static void focus_initial_view(struct horizon_server *server,
    struct horizon_xdg_toplevel *view) {
    if (server->focused_view != NULL || view == NULL) {
        return;
    }

    server->focused_view = view;
    wlr_xdg_toplevel_set_activated(view->toplevel, true);
    enter_keyboard_focus(server, view);
    HORIZON_DEBUG_LOG("initial keyboard focus: %s", view_title(view));
}

static void update_pointer_focus(struct horizon_server *server,
    uint32_t time_msec) {
    double sx = 0, sy = 0;
    struct horizon_xdg_toplevel *view = view_at(server, &sx, &sy);

    if (view != server->pointer_view) {
        server->pointer_view = view;
        HORIZON_DEBUG_LOG("pointer target: %s", view_title(view));
        focus_view(server, view, sx, sy);
    }

    if (view != NULL) {
        wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
    }
    wlr_seat_pointer_notify_frame(server->seat);
}

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

static void update_seat_capabilities(struct horizon_server *server) {
    uint32_t capabilities = WL_SEAT_CAPABILITY_POINTER;
    if (wlr_seat_get_keyboard(server->seat) != NULL) {
        capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, capabilities);
}

static void update_cursor_scene(struct horizon_server *server) {
    if (server->cursor_scene == NULL || server->cursor_image == NULL ||
        server->cursor_image->image_count == 0) {
        return;
    }

    struct wlr_xcursor_image *image = server->cursor_image->images[0];
    wlr_scene_node_set_position(&server->cursor_scene->node,
        (int)server->cursor->x - (int)image->hotspot_x,
        (int)server->cursor->y - (int)image->hotspot_y);
    wlr_scene_node_raise_to_top(&server->cursor_scene->node);
    if (server->output != NULL) {
        wlr_output_schedule_frame(server->output);
    }
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
    (void)data;
    struct horizon_server *server =
        wl_container_of(listener, server, cursor_motion);
    update_cursor_scene(server);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    (void)data;
    struct horizon_server *server =
        wl_container_of(listener, server, cursor_motion_absolute);
    update_cursor_scene(server);
}

static void handle_pointer_motion(struct wl_listener *listener, void *data) {
    struct horizon_pointer *pointer =
        wl_container_of(listener, pointer, motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(pointer->server->cursor, &pointer->pointer->base,
        event->delta_x, event->delta_y);
    update_cursor_scene(pointer->server);
    update_pointer_focus(pointer->server, event->time_msec);
}

static void handle_pointer_motion_absolute(struct wl_listener *listener, void *data) {
    struct horizon_pointer *pointer =
        wl_container_of(listener, pointer, motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(pointer->server->cursor, &pointer->pointer->base,
        event->x, event->y);
    update_cursor_scene(pointer->server);
    update_pointer_focus(pointer->server, event->time_msec);
}

static void handle_pointer_button(struct wl_listener *listener, void *data) {
    struct horizon_pointer *pointer =
        wl_container_of(listener, pointer, button);
    struct wlr_pointer_button_event *event = data;

    wlr_seat_pointer_notify_button(pointer->server->seat,
        event->time_msec, event->button, event->state);
    wlr_seat_pointer_notify_frame(pointer->server->seat);
}

static void handle_pointer_axis(struct wl_listener *listener, void *data) {
    struct horizon_pointer *pointer =
        wl_container_of(listener, pointer, axis);
    struct wlr_pointer_axis_event *event = data;

    wlr_seat_pointer_notify_axis(pointer->server->seat,
        event->time_msec, event->orientation, event->delta,
        event->delta_discrete, event->source,
        event->relative_direction);
    wlr_seat_pointer_notify_frame(pointer->server->seat);
}

static void handle_pointer_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct horizon_pointer *pointer =
        wl_container_of(listener, pointer, destroy);
    wl_list_remove(&pointer->motion.link);
    wl_list_remove(&pointer->motion_absolute.link);
    wl_list_remove(&pointer->button.link);
    wl_list_remove(&pointer->axis.link);
    wl_list_remove(&pointer->destroy.link);
    free(pointer);
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_keyboard *keyboard =
        wl_container_of(listener, keyboard, destroy);
    if (wlr_seat_get_keyboard(keyboard->server->seat) == keyboard->keyboard) {
        wlr_seat_set_keyboard(keyboard->server->seat, NULL);
        update_seat_capabilities(keyboard->server);
    }
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->destroy.link);
    free(keyboard);
}

static void handle_keyboard_modifiers(struct wl_listener *listener, void *data) {
    (void)data;
    struct horizon_keyboard *keyboard =
        wl_container_of(listener, keyboard, modifiers);

    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
        &keyboard->keyboard->modifiers);
}

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
    struct horizon_keyboard *horizon_keyboard =
        wl_container_of(listener, horizon_keyboard, key);
    struct wlr_keyboard_key_event *event = data;

    HORIZON_DEBUG_LOG("keyboard key: keycode=%u state=%u focused=%s",
        event->keycode, event->state,
        view_title(horizon_keyboard->server->focused_view));

    if (event->state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        wlr_seat_keyboard_notify_key(horizon_keyboard->server->seat,
            event->time_msec, event->keycode, event->state);
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
        return;
    }

    wlr_seat_keyboard_notify_key(horizon_keyboard->server->seat,
        event->time_msec, event->keycode, event->state);
    unsigned vt = horizon_vt_shortcut(event->keycode, modifiers, keysym);
    if (vt == 0 || horizon_keyboard->server->session == NULL) {
        return;
    }

    if (!wlr_session_change_vt(horizon_keyboard->server->session, vt)) {
        fprintf(stderr, "horizon: failed to switch to VT%u\n", vt);
    }
}

static void handle_xdg_map(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_xdg_toplevel *view =
        wl_container_of(listener, view, map);
    wlr_xdg_toplevel_set_activated(view->toplevel, false);
    HORIZON_DEBUG_LOG("xdg map: title=%s", view->toplevel->title != NULL ?
        view->toplevel->title : "untitled");
    printf("XDG toplevel mapped: %s\n",
        view->toplevel->title != NULL ? view->toplevel->title : "untitled");
    fflush(stdout);
    update_pointer_focus(view->server, 0);
    focus_initial_view(view->server, view);
}

static void handle_xdg_unmap(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_xdg_toplevel *view =
        wl_container_of(listener, view, unmap);
    if (view->server->pointer_view == view) {
        view->server->pointer_view = NULL;
    }
    if (view->server->focused_view == view) {
        focus_view(view->server, NULL, 0, 0);
    }
    update_pointer_focus(view->server, 0);
    HORIZON_DEBUG_LOG("xdg unmap: title=%s", view_title(view));
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
        wlr_xdg_toplevel_set_activated(view->toplevel, false);
        view->configured = true;
        HORIZON_DEBUG_LOG("xdg initial configure sent: size=800x600");
    }
}

static void handle_xdg_destroy(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_xdg_toplevel *view =
        wl_container_of(listener, view, destroy);
    if (view->server->focused_view == view) {
        focus_view(view->server, NULL, 0, 0);
    }
    if (view->server->pointer_view == view) {
        view->server->pointer_view = NULL;
    }
    HORIZON_DEBUG_LOG("xdg destroy");
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->link);
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
    view->server = server;
    view->scene_tree = wlr_scene_xdg_surface_create(
        &server->scene->tree, toplevel->base);
    if (view->scene_tree == NULL) {
        fprintf(stderr, "horizon: failed to create xdg scene surface\n");
        free(view);
        return;
    }
    wlr_scene_node_set_position(&view->scene_tree->node, 80, 80);
    view->scene_tree->node.data = view;
    wl_list_insert(&server->views, &view->link);

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
    HORIZON_DEBUG_LOG("input device: type=%d name=%s",
        device->type, device->name);

    if (device->type == WLR_INPUT_DEVICE_POINTER) {
        struct horizon_pointer *pointer = calloc(1, sizeof(*pointer));
        if (pointer == NULL) {
            fprintf(stderr, "horizon: failed to allocate pointer state\n");
            return;
        }

        pointer->server = server;
        pointer->pointer = wlr_pointer_from_input_device(device);
        pointer->motion.notify = handle_pointer_motion;
        pointer->motion_absolute.notify = handle_pointer_motion_absolute;
        pointer->button.notify = handle_pointer_button;
        pointer->axis.notify = handle_pointer_axis;
        pointer->destroy.notify = handle_pointer_destroy;
        wl_signal_add(&pointer->pointer->events.motion, &pointer->motion);
        wl_signal_add(&pointer->pointer->events.motion_absolute,
            &pointer->motion_absolute);
        wl_signal_add(&pointer->pointer->events.button, &pointer->button);
        wl_signal_add(&pointer->pointer->events.axis, &pointer->axis);
        wl_signal_add(&device->events.destroy, &pointer->destroy);
        update_seat_capabilities(server);
        HORIZON_DEBUG_LOG("pointer attached: %s", device->name);
        return;
    }

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
        HORIZON_DEBUG_LOG("keyboard configure failed: %s", device->name);
        free(keyboard);
        return;
    }

    wlr_seat_set_keyboard(server->seat, keyboard->keyboard);
    update_seat_capabilities(server);
    enter_keyboard_focus(server, server->focused_view);
    HORIZON_DEBUG_LOG("keyboard attached: %s", device->name);

    keyboard->key.notify = handle_keyboard_key;
    keyboard->modifiers.notify = handle_keyboard_modifiers;
    keyboard->destroy.notify = handle_keyboard_destroy;
    wl_signal_add(&keyboard->keyboard->events.key, &keyboard->key);
    wl_signal_add(&keyboard->keyboard->events.modifiers, &keyboard->modifiers);
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
    server->output = output;

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

    if (wlr_output_layout_add_auto(server->output_layout, output) == NULL) {
        fprintf(stderr, "horizon: failed to add output %s to layout\n",
            output->name);
        return;
    }
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

    if (!wlr_xcursor_manager_load(server->cursor_manager, output->scale)) {
        fprintf(stderr, "horizon: failed to load cursor theme for output %s\n",
            output->name);
    }
    wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "left_ptr");
    server->cursor_image = wlr_xcursor_manager_get_xcursor(
        server->cursor_manager, "left_ptr", output->scale);

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
    if (server->cursor_scene == NULL && server->cursor_image != NULL &&
        server->cursor_image->image_count > 0) {
        struct wlr_buffer *buffer = wlr_xcursor_image_get_buffer(
            server->cursor_image->images[0]);
        server->cursor_scene = wlr_scene_buffer_create(
            &server->scene->tree, buffer);
        if (server->cursor_scene == NULL) {
            fprintf(stderr, "horizon: failed to create software cursor scene\n");
        } else {
            server->cursor_scene->point_accepts_input =
                cursor_accepts_input;
            update_cursor_scene(server);
            wlr_scene_node_raise_to_top(&server->cursor_scene->node);
        }
    }
    horizon_output->frame.notify = render_frame;
    horizon_output->destroy.notify = handle_output_destroy;
    wl_signal_add(&output->events.frame, &horizon_output->frame);
    wl_signal_add(&output->events.destroy, &horizon_output->destroy);

    printf("Output ready: %s (%dx%d)\n", output->name, output->width, output->height);
    fflush(stdout);
    wlr_output_schedule_frame(output);
}

static pid_t launch_command(char *const command[], const char *socket) {
    /* Keep the launcher's buffered output out of the child process. */
    fflush(NULL);

    pid_t pid = fork();
    if (pid != 0) {
        return pid;
    }

    /* A command's diagnostics belong to the same output as its regular logs. */
    if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
        dprintf(STDOUT_FILENO, "horizon: failed to route command logs: %s\n",
            strerror(errno));
        _exit(127);
    }

    if (setenv("WAYLAND_DISPLAY", socket, 1) != 0) {
        dprintf(STDOUT_FILENO, "horizon: failed to set WAYLAND_DISPLAY: %s\n",
            strerror(errno));
        _exit(127);
    }

    execvp(command[0], command);
    dprintf(STDOUT_FILENO, "horizon: failed to execute %s: %s\n",
        command[0], strerror(errno));
    _exit(127);
}

static int wait_for_command(pid_t pid) {
    int status;
    pid_t result;

    do {
        result = waitpid(pid, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);

    if (result == 0) {
        /* Ctrl+Alt+Backspace ends the compositor and its hosted program. */
        if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
            fprintf(stderr, "horizon: failed to stop command: %s\n",
                strerror(errno));
        }
        do {
            result = waitpid(pid, &status, 0);
        } while (result < 0 && errno == EINTR);
    }

    if (result < 0) {
        fprintf(stderr, "horizon: failed to wait for command: %s\n",
            strerror(errno));
        return EXIT_FAILURE;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) {
    char *const *command = NULL;
    if (argc > 1) {
        if (strcmp(argv[1], "--") != 0 || argc == 2) {
            fprintf(stderr, "Usage: %s [-- command [args...]]\n", argv[0]);
            return EXIT_FAILURE;
        }
        command = &argv[2];
    }

    struct horizon_server server = {0};
    wl_list_init(&server.views);
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

    /* Advertise wl_data_device_manager so clients can use clipboard and DND. */
    server.data_device_manager = wlr_data_device_manager_create(server.display);
    if (server.data_device_manager == NULL) {
        fprintf(stderr, "horizon: failed to create data device manager\n");
        wlr_seat_destroy(server.seat);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.cursor = wlr_cursor_create();
    server.cursor_manager = wlr_xcursor_manager_create("Adwaita", 24);
    server.output_layout = wlr_output_layout_create(server.display);
    if (server.cursor == NULL || server.cursor_manager == NULL ||
        server.output_layout == NULL) {
        fprintf(stderr, "horizon: failed to create pointer cursor\n");
        wlr_cursor_destroy(server.cursor);
        wlr_xcursor_manager_destroy(server.cursor_manager);
        wlr_output_layout_destroy(server.output_layout);
        wlr_seat_destroy(server.seat);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.backend = wlr_backend_autocreate(
        wl_display_get_event_loop(server.display), &server.session);
    if (server.backend == NULL) {
        fprintf(stderr, "horizon: failed to create wlroots backend\n");
        wlr_seat_destroy(server.seat);
        wlr_xcursor_manager_destroy(server.cursor_manager);
        wlr_cursor_destroy(server.cursor);
        wlr_output_layout_destroy(server.output_layout);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (server.renderer == NULL ||
        !wlr_renderer_init_wl_display(server.renderer, server.display)) {
        fprintf(stderr, "horizon: failed to initialize renderer\n");
        wlr_backend_destroy(server.backend);
        wlr_seat_destroy(server.seat);
        wlr_xcursor_manager_destroy(server.cursor_manager);
        wlr_cursor_destroy(server.cursor);
        wlr_output_layout_destroy(server.output_layout);
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
        wlr_xcursor_manager_destroy(server.cursor_manager);
        wlr_cursor_destroy(server.cursor);
        wlr_output_layout_destroy(server.output_layout);
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
        wlr_xcursor_manager_destroy(server.cursor_manager);
        wlr_cursor_destroy(server.cursor);
        wlr_output_layout_destroy(server.output_layout);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (server.allocator == NULL) {
        fprintf(stderr, "horizon: failed to create allocator\n");
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wlr_xcursor_manager_destroy(server.cursor_manager);
        wlr_cursor_destroy(server.cursor);
        wlr_output_layout_destroy(server.output_layout);
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
    server.cursor_motion.notify = handle_cursor_motion;
    wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
    server.cursor_motion_absolute.notify = handle_cursor_motion_absolute;
    wl_signal_add(&server.cursor->events.motion_absolute,
        &server.cursor_motion_absolute);

    if (!wlr_backend_start(server.backend)) {
        fprintf(stderr, "horizon: failed to start wlroots backend\n");
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_xcursor_manager_destroy(server.cursor_manager);
        wlr_cursor_destroy(server.cursor);
        wlr_output_layout_destroy(server.output_layout);
        wlr_seat_destroy(server.seat);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    printf("Hello from horizon, a Wayland compositor!\n");
    printf("Seat ready: seat0\n");
    printf("Listening on WAYLAND_DISPLAY=%s\n", socket);
    fflush(stdout);

    pid_t command_pid = -1;
    if (command != NULL) {
        command_pid = launch_command(command, socket);
        if (command_pid < 0) {
            fprintf(stderr, "horizon: failed to launch command: %s\n",
                strerror(errno));
            wl_display_terminate(server.display);
        }
    }
    wl_display_run(server.display);

    wl_list_remove(&server.new_input.link);
    wl_list_remove(&server.new_output.link);
    wl_list_remove(&server.new_toplevel.link);
    wl_list_remove(&server.cursor_motion.link);
    wl_list_remove(&server.cursor_motion_absolute.link);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor_manager);
    wlr_cursor_destroy(server.cursor);
    wlr_output_layout_destroy(server.output_layout);
    wlr_seat_destroy(server.seat);
    wl_display_destroy(server.display);

    if (command_pid >= 0) {
        return wait_for_command(command_pid);
    }
    return command != NULL ? EXIT_FAILURE : EXIT_SUCCESS;
}
