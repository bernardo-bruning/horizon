#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

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
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "input.h"
#include "config.h"
#include "decorator.h"

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
    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
    struct wlr_scene *scene;
    struct wlr_seat *seat;
    struct wlr_keyboard_group *keyboard_group;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_manager;
    struct wlr_output_layout *output_layout;
    struct wlr_output *output;
    struct wlr_xcursor *cursor_image;
    struct wlr_scene_buffer *cursor_scene;
    struct wl_list views;
    struct horizon_xdg_toplevel *focused_view;
    struct horizon_xdg_toplevel *pointer_view;
    struct wl_listener keyboard_key;
    struct wl_listener keyboard_modifiers;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener new_output;
    struct wl_listener new_input;
    struct wl_listener new_toplevel;
    struct wl_listener new_toplevel_decoration;
    const struct horizon_config *config;
    const char *socket;
    struct horizon_xdg_toplevel *drag_view;
    int drag_offset_x;
    int drag_offset_y;
    struct horizon_xdg_toplevel *resize_view;
    int resize_start_x;
    int resize_start_y;
    int resize_start_width;
    int resize_start_height;
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
    struct horizon_decorator *decorator;
    bool configured;
    bool fullscreen;
    bool maximized;
    int x;
    int y;
    struct horizon_server *server;
    struct wl_list link;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener request_fullscreen;
    struct wl_listener destroy;
};

static void update_view_layout(struct horizon_xdg_toplevel *view) {
    const struct horizon_window_config *config = &view->server->config->window;
    int width = config->default_width;
    int height = config->default_height;
    int x = view->x;
    int y = view->y;

    if ((view->fullscreen || view->maximized) &&
        view->server->output != NULL) {
        width = view->server->output->width;
        height = view->server->output->height;
        x = 0;
        y = 0;
    } else if (view->toplevel->current.width > 0 &&
        view->toplevel->current.height > 0) {
        width = view->toplevel->current.width;
        height = view->toplevel->current.height;
    }

    wlr_scene_node_set_position(&view->scene_tree->node, x, y);
    horizon_decorator_update(view->decorator, x, y, width, height);
}

static void configure_view_decoration(struct horizon_xdg_toplevel *view) {
    if (!view->server->config->decoration.enabled ||
        view->server->xdg_decoration_manager == NULL ||
        !view->toplevel->base->initialized) {
        return;
    }

    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    wl_list_for_each(decoration,
        &view->server->xdg_decoration_manager->decorations, link) {
        if (decoration->toplevel == view->toplevel) {
            wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
                WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
    }
}

struct horizon_keyboard {
    struct wlr_keyboard *keyboard;
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

static bool configure_keyboard(struct wlr_keyboard *keyboard,
    const struct horizon_input_config *config) {
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

    wlr_keyboard_set_repeat_info(keyboard,
        config->repeat_rate, config->repeat_delay);
    return true;
}

static void update_seat_capabilities(struct horizon_server *server) {
    uint32_t capabilities = WL_SEAT_CAPABILITY_POINTER;
    if (wlr_seat_get_keyboard(server->seat) != NULL) {
        capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, capabilities);
}

static void destroy_keyboard_group(struct horizon_server *server) {
    if (server->keyboard_group == NULL) {
        return;
    }

    if (wlr_seat_get_keyboard(server->seat) ==
        &server->keyboard_group->keyboard) {
        wlr_seat_set_keyboard(server->seat, NULL);
        update_seat_capabilities(server);
    }
    wl_list_remove(&server->keyboard_key.link);
    wl_list_remove(&server->keyboard_modifiers.link);
    wlr_keyboard_group_destroy(server->keyboard_group);
    server->keyboard_group = NULL;
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

static void resize_view_at_cursor(struct horizon_server *server) {
    struct horizon_xdg_toplevel *view = server->resize_view;
    if (view == NULL) {
        return;
    }

    int width = server->resize_start_width +
        (int)server->cursor->x - server->resize_start_x;
    int height = server->resize_start_height +
        (int)server->cursor->y - server->resize_start_y;
    if (view->toplevel->current.min_width > 0 &&
        width < view->toplevel->current.min_width) {
        width = view->toplevel->current.min_width;
    }
    if (view->toplevel->current.min_height > 0 &&
        height < view->toplevel->current.min_height) {
        height = view->toplevel->current.min_height;
    }
    if (view->toplevel->current.max_width > 0 &&
        width > view->toplevel->current.max_width) {
        width = view->toplevel->current.max_width;
    }
    if (view->toplevel->current.max_height > 0 &&
        height > view->toplevel->current.max_height) {
        height = view->toplevel->current.max_height;
    }
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }

    wlr_xdg_toplevel_set_size(view->toplevel, width, height);
    horizon_decorator_update(view->decorator, view->x, view->y,
        width, height);
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
    if (pointer->server->drag_view != NULL) {
        struct horizon_xdg_toplevel *view = pointer->server->drag_view;
        int x = (int)pointer->server->cursor->x -
            pointer->server->drag_offset_x;
        int y = (int)pointer->server->cursor->y -
            pointer->server->drag_offset_y;
        view->x = x;
        view->y = y;
        wlr_scene_node_set_position(&view->scene_tree->node, x, y);
        horizon_decorator_update(view->decorator, x, y,
            view->toplevel->current.width > 0 ? view->toplevel->current.width :
                view->server->config->window.default_width,
            view->toplevel->current.height > 0 ? view->toplevel->current.height :
                view->server->config->window.default_height);
    }
    resize_view_at_cursor(pointer->server);
    update_cursor_scene(pointer->server);
    update_pointer_focus(pointer->server, event->time_msec);
}

static void handle_pointer_motion_absolute(struct wl_listener *listener, void *data) {
    struct horizon_pointer *pointer =
        wl_container_of(listener, pointer, motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(pointer->server->cursor, &pointer->pointer->base,
        event->x, event->y);
    if (pointer->server->drag_view != NULL) {
        struct horizon_xdg_toplevel *view = pointer->server->drag_view;
        int x = (int)pointer->server->cursor->x -
            pointer->server->drag_offset_x;
        int y = (int)pointer->server->cursor->y -
            pointer->server->drag_offset_y;
        view->x = x;
        view->y = y;
        wlr_scene_node_set_position(&view->scene_tree->node, x, y);
        horizon_decorator_update(view->decorator, x, y,
            view->toplevel->current.width > 0 ? view->toplevel->current.width :
                view->server->config->window.default_width,
            view->toplevel->current.height > 0 ? view->toplevel->current.height :
                view->server->config->window.default_height);
    }
    resize_view_at_cursor(pointer->server);
    update_cursor_scene(pointer->server);
    update_pointer_focus(pointer->server, event->time_msec);
}

static void handle_pointer_button(struct wl_listener *listener, void *data) {
    struct horizon_pointer *pointer =
        wl_container_of(listener, pointer, button);
    struct wlr_pointer_button_event *event = data;

    bool left_button = event->button == BTN_LEFT;
    bool right_button = event->button == BTN_RIGHT;
    bool logo_pressed = pointer->server->keyboard_group != NULL &&
        (wlr_keyboard_get_modifiers(
            &pointer->server->keyboard_group->keyboard) & WLR_MODIFIER_LOGO);
    if (left_button && event->state == WL_POINTER_BUTTON_STATE_PRESSED &&
        logo_pressed) {
        double sx, sy;
        struct horizon_xdg_toplevel *view = view_at(
            pointer->server, &sx, &sy);
        if (view != NULL && !view->maximized && !view->fullscreen &&
            wlr_scene_node_coords(&view->scene_tree->node,
                &pointer->server->drag_offset_x,
                &pointer->server->drag_offset_y)) {
            pointer->server->drag_view = view;
            pointer->server->drag_offset_x =
                (int)pointer->server->cursor->x -
                pointer->server->drag_offset_x;
            pointer->server->drag_offset_y =
                (int)pointer->server->cursor->y -
                pointer->server->drag_offset_y;
            return;
        }
    }
    if (right_button && event->state == WL_POINTER_BUTTON_STATE_PRESSED &&
        logo_pressed) {
        double sx, sy;
        struct horizon_xdg_toplevel *view = view_at(
            pointer->server, &sx, &sy);
        if (view != NULL && !view->maximized && !view->fullscreen) {
            pointer->server->resize_view = view;
            pointer->server->resize_start_x =
                (int)pointer->server->cursor->x;
            pointer->server->resize_start_y =
                (int)pointer->server->cursor->y;
            pointer->server->resize_start_width =
                view->toplevel->current.width > 0 ?
                view->toplevel->current.width :
                view->server->config->window.default_width;
            pointer->server->resize_start_height =
                view->toplevel->current.height > 0 ?
                view->toplevel->current.height :
                view->server->config->window.default_height;
            wlr_xdg_toplevel_set_resizing(view->toplevel, true);
            return;
        }
    }
    if (left_button && event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
        pointer->server->drag_view != NULL) {
        pointer->server->drag_view = NULL;
        return;
    }
    if (right_button && event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
        pointer->server->resize_view != NULL) {
        wlr_xdg_toplevel_set_resizing(
            pointer->server->resize_view->toplevel, false);
        pointer->server->resize_view = NULL;
        return;
    }

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
    struct wlr_keyboard_group *group = keyboard->server->keyboard_group;
    if (group != NULL && keyboard->keyboard->group == group) {
        wlr_keyboard_group_remove_keyboard(group, keyboard->keyboard);
    }
    if (group != NULL && wl_list_empty(&group->devices) &&
        wlr_seat_get_keyboard(keyboard->server->seat) == &group->keyboard) {
        wlr_seat_set_keyboard(keyboard->server->seat, NULL);
        update_seat_capabilities(keyboard->server);
    }
    wl_list_remove(&keyboard->destroy.link);
    free(keyboard);
}

static void handle_keyboard_modifiers(struct wl_listener *listener, void *data) {
    (void)data;
    struct horizon_server *server =
        wl_container_of(listener, server, keyboard_modifiers);

    wlr_seat_keyboard_notify_modifiers(server->seat,
        &server->keyboard_group->keyboard.modifiers);
}

static void set_view_state(struct horizon_xdg_toplevel *view,
    bool maximized, bool fullscreen) {
    view->maximized = maximized;
    view->fullscreen = fullscreen;
    wlr_xdg_toplevel_set_maximized(view->toplevel, maximized);
    wlr_xdg_toplevel_set_fullscreen(view->toplevel, fullscreen);
    if ((maximized || fullscreen) && view->server->output != NULL) {
        wlr_xdg_toplevel_set_size(view->toplevel,
            view->server->output->width, view->server->output->height);
    }
    update_view_layout(view);
}

static void launch_detached_command(char *const command[], const char *socket) {
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "horizon: failed to launch %s: %s\n",
            command[0], strerror(errno));
        return;
    }
    if (pid == 0) {
        pid_t child = fork();
        if (child < 0) {
            _exit(127);
        }
        if (child > 0) {
            _exit(0);
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
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
}

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
    struct horizon_server *server =
        wl_container_of(listener, server, keyboard_key);
    struct wlr_keyboard_key_event *event = data;
    struct wlr_keyboard *keyboard = &server->keyboard_group->keyboard;

    HORIZON_DEBUG_LOG(
        "keyboard group event: keycode=%u state=%u modifiers=0x%x focused=%s",
        event->keycode, event->state,
        wlr_keyboard_get_modifiers(keyboard),
        view_title(server->focused_view));

    if (event->state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        wlr_seat_keyboard_notify_key(server->seat,
            event->time_msec, event->keycode, event->state);
        return;
    }

    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard);
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;
    if (keyboard->xkb_state != NULL) {
        keysym = xkb_state_key_get_one_sym(
            keyboard->xkb_state, event->keycode + 8);
    }
    char keysym_name[64] = "unknown";
    char utf8[8] = {0};
    xkb_keysym_get_name(keysym, keysym_name, sizeof(keysym_name));
    xkb_keysym_to_utf8(keysym, utf8, sizeof(utf8));
    HORIZON_DEBUG_LOG(
        "keyboard translation: keycode=%u keysym=0x%x name=%s utf8=%s",
        event->keycode, keysym, keysym_name, utf8);
    const struct horizon_key_binding *binding = horizon_find_key_binding(
        &server->config->input, event->keycode, modifiers, keysym);
    if (binding != NULL && binding->action == HORIZON_ACTION_EXIT) {
        printf("Exiting horizon\n");
        fflush(stdout);
        wl_display_terminate(server->display);
        return;
    }

    wlr_seat_keyboard_notify_key(server->seat,
        event->time_msec, event->keycode, event->state);
    if (binding == NULL) {
        return;
    }

    switch (binding->action) {
    case HORIZON_ACTION_SWITCH_VT:
        if (server->session != NULL &&
            !wlr_session_change_vt(server->session, binding->argument)) {
            fprintf(stderr, "horizon: failed to switch to VT%u\n",
                binding->argument);
        }
        break;
    case HORIZON_ACTION_LAUNCH_COMMAND:
        if (binding->command != NULL) {
            launch_detached_command(binding->command, server->socket);
        }
        break;
    case HORIZON_ACTION_TOGGLE_MAXIMIZE:
        if (server->focused_view != NULL) {
            set_view_state(server->focused_view,
                !server->focused_view->maximized, false);
        }
        break;
    case HORIZON_ACTION_FULLSCREEN:
        if (server->focused_view != NULL) {
            set_view_state(server->focused_view, false, true);
        }
        break;
    case HORIZON_ACTION_EXIT:
        break;
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

static void handle_new_toplevel_decoration(struct wl_listener *listener,
    void *data) {
    struct horizon_server *server =
        wl_container_of(listener, server, new_toplevel_decoration);
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
    if (server->config->decoration.enabled &&
        decoration->toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
}

static void handle_xdg_unmap(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_xdg_toplevel *view =
        wl_container_of(listener, view, unmap);
    if (view->server->pointer_view == view) {
        view->server->pointer_view = NULL;
    }
    if (view->server->drag_view == view) {
        view->server->drag_view = NULL;
    }
    if (view->server->resize_view == view) {
        view->server->resize_view = NULL;
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
        configure_view_decoration(view);

        /* Apply the configured initial window policy. */
        view->maximized = view->server->config->window.maximize_on_start;
        wlr_xdg_toplevel_set_maximized(view->toplevel, view->maximized);

        if (view->server->config->window.accept_client_fullscreen &&
            view->toplevel->requested.fullscreen) {
            view->fullscreen = true;
            wlr_xdg_toplevel_set_fullscreen(view->toplevel, true);
        } else if (view->toplevel->requested.fullscreen) {
            wlr_xdg_toplevel_set_fullscreen(view->toplevel, false);
        }
        wlr_xdg_toplevel_set_size(view->toplevel,
            (view->fullscreen || view->maximized) &&
                view->server->output != NULL ?
                view->server->output->width :
                view->server->config->window.default_width,
            (view->fullscreen || view->maximized) &&
                view->server->output != NULL ?
                view->server->output->height :
                view->server->config->window.default_height);
        wlr_xdg_toplevel_set_activated(view->toplevel, false);
        view->configured = true;
        HORIZON_DEBUG_LOG("xdg initial configure sent: fullscreen=%d maximized=%d size=%dx%d",
            view->fullscreen,
            view->maximized,
            (view->fullscreen || view->maximized) &&
                view->server->output != NULL ?
                view->server->output->width :
                view->server->config->window.default_width,
            (view->fullscreen || view->maximized) &&
                view->server->output != NULL ?
                view->server->output->height :
                view->server->config->window.default_height);
    }
    update_view_layout(view);
}

static void handle_xdg_request_fullscreen(struct wl_listener *listener,
    void *data) {
    (void)data;
    struct horizon_xdg_toplevel *view =
        wl_container_of(listener, view, request_fullscreen);
    view->fullscreen = view->server->config->window.accept_client_fullscreen &&
        view->toplevel->requested.fullscreen;
    view->maximized = true;
    wlr_xdg_toplevel_set_maximized(view->toplevel, true);
    wlr_xdg_toplevel_set_fullscreen(view->toplevel, view->fullscreen);
    if ((view->fullscreen || view->maximized) &&
        view->server->output != NULL) {
        wlr_xdg_toplevel_set_size(view->toplevel,
            view->server->output->width, view->server->output->height);
    }
    update_view_layout(view);
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
    if (view->server->resize_view == view) {
        view->server->resize_view = NULL;
    }
    HORIZON_DEBUG_LOG("xdg destroy");
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->link);
    horizon_decorator_destroy(view->decorator);
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
    if (server->config->decoration.enabled) {
        view->decorator = horizon_decorator_create(&server->scene->tree,
            server->config->decoration.border_width,
            server->config->decoration.border_color);
    }
    if (server->config->decoration.enabled && view->decorator == NULL) {
        fprintf(stderr, "horizon: failed to create window decorator\n");
        wlr_scene_node_destroy(&view->scene_tree->node);
        free(view);
        return;
    }
    view->fullscreen = server->config->window.accept_client_fullscreen &&
        toplevel->requested.fullscreen;
    view->maximized = server->config->window.maximize_on_start;
    view->x = server->config->window.default_x;
    view->y = server->config->window.default_y;
    update_view_layout(view);
    view->scene_tree->node.data = view;
    wl_list_insert(&server->views, &view->link);

    view->map.notify = handle_xdg_map;
    view->unmap.notify = handle_xdg_unmap;
    view->commit.notify = handle_xdg_commit;
    view->request_fullscreen.notify = handle_xdg_request_fullscreen;
    view->destroy.notify = handle_xdg_destroy;
    wl_signal_add(&toplevel->base->surface->events.map, &view->map);
    wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);
    wl_signal_add(&toplevel->base->surface->events.commit, &view->commit);
    wl_signal_add(&toplevel->events.request_fullscreen,
        &view->request_fullscreen);
    wl_signal_add(&toplevel->events.destroy, &view->destroy);

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
    if (!configure_keyboard(keyboard->keyboard, &server->config->input)) {
        HORIZON_DEBUG_LOG("keyboard configure failed: %s", device->name);
        free(keyboard);
        return;
    }

    if (server->keyboard_group == NULL) {
        server->keyboard_group = wlr_keyboard_group_create();
        if (server->keyboard_group == NULL) {
            fprintf(stderr, "horizon: failed to create keyboard group\n");
            free(keyboard);
            return;
        }
        server->keyboard_key.notify = handle_keyboard_key;
        server->keyboard_modifiers.notify = handle_keyboard_modifiers;
        wl_signal_add(&server->keyboard_group->keyboard.events.key,
            &server->keyboard_key);
        wl_signal_add(&server->keyboard_group->keyboard.events.modifiers,
            &server->keyboard_modifiers);
    }

    if (server->keyboard_group->keyboard.keymap == NULL) {
        if (!wlr_keyboard_set_keymap(
                &server->keyboard_group->keyboard,
                keyboard->keyboard->keymap)) {
            fprintf(stderr, "horizon: failed to set keyboard group keymap\n");
            free(keyboard);
            return;
        }
        wlr_keyboard_set_repeat_info(
            &server->keyboard_group->keyboard,
            keyboard->keyboard->repeat_info.rate,
            keyboard->keyboard->repeat_info.delay);
        HORIZON_DEBUG_LOG("keyboard group initialized from: %s",
            device->name);
    }

    if (!wlr_keyboard_group_add_keyboard(
            server->keyboard_group, keyboard->keyboard)) {
        fprintf(stderr, "horizon: failed to add keyboard to group: %s\n",
            device->name);
        free(keyboard);
        return;
    }

    if (wlr_seat_get_keyboard(server->seat) == NULL) {
        wlr_seat_set_keyboard(server->seat,
            &server->keyboard_group->keyboard);
        update_seat_capabilities(server);
        enter_keyboard_focus(server, server->focused_view);
    }

    HORIZON_DEBUG_LOG("keyboard added to group: %s", device->name);

    keyboard->destroy.notify = handle_keyboard_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);
}

static void render_frame(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_output *horizon_output =
        wl_container_of(listener, horizon_output, frame);
    HORIZON_DEBUG_LOG(
        "[DEBUG-render] output frame: name=%s size=%dx%d",
        horizon_output->output->name,
        horizon_output->output->width,
        horizon_output->output->height);

    bool committed = wlr_scene_output_commit(
        horizon_output->scene_output, NULL);
    HORIZON_DEBUG_LOG(
        "[DEBUG-render] output frame commit: name=%s result=%d",
        horizon_output->output->name, committed);
    if (!committed) {
        fprintf(stderr, "horizon: failed to commit output frame\n");
        return;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(horizon_output->scene_output, &now);
    HORIZON_DEBUG_LOG(
        "[DEBUG-render] frame done sent: name=%s",
        horizon_output->output->name);
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
    wlr_cursor_set_xcursor(server->cursor, server->cursor_manager,
        server->config->output.cursor_name);
    server->cursor_image = wlr_xcursor_manager_get_xcursor(
        server->cursor_manager, server->config->output.cursor_name,
        output->scale);

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

    wlr_scene_rect_create(&server->scene->tree, output->width,
        output->height, server->config->output.background_color);
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

    server.config = &horizon_default_config;
    server.socket = socket;
    server.seat = wlr_seat_create(server.display, server.config->seat_name);
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
    server.cursor_manager = wlr_xcursor_manager_create(
        server.config->output.cursor_theme,
        server.config->output.cursor_size);
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
    server.xdg_decoration_manager =
        wlr_xdg_decoration_manager_v1_create(server.display);
    server.scene = wlr_scene_create();
    if (server.xdg_shell == NULL ||
        server.xdg_decoration_manager == NULL || server.scene == NULL) {
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
    server.new_toplevel_decoration.notify = handle_new_toplevel_decoration;
    wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration,
        &server.new_toplevel_decoration);
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
        destroy_keyboard_group(&server);
        wlr_xcursor_manager_destroy(server.cursor_manager);
        wlr_cursor_destroy(server.cursor);
        wlr_output_layout_destroy(server.output_layout);
        wlr_seat_destroy(server.seat);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    printf("Hello from horizon, a Wayland compositor!\n");
    printf("Seat ready: %s\n", server.config->seat_name);
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
    wl_list_remove(&server.new_toplevel_decoration.link);
    wl_list_remove(&server.cursor_motion.link);
    wl_list_remove(&server.cursor_motion_absolute.link);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wlr_scene_node_destroy(&server.scene->tree.node);
    destroy_keyboard_group(&server);
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
