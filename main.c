#include <stdio.h>
#include <stdlib.h>

#include <wayland-server-core.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>

struct horizon_server {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wl_listener new_output;
};

struct horizon_output {
    struct wlr_output *output;
    struct wl_listener frame;
};

static void render_frame(struct wl_listener *listener, void *data) {
    (void)data;

    struct horizon_output *horizon_output =
        wl_container_of(listener, horizon_output, frame);
    struct wlr_output *output = horizon_output->output;
    struct wlr_output_state state;
    wlr_output_state_init(&state);

    struct wlr_render_pass *pass =
        wlr_output_begin_render_pass(output, &state, NULL);
    if (pass == NULL) {
        wlr_output_state_finish(&state);
        return;
    }

    struct wlr_render_rect_options background = {
        .box = { .x = 0, .y = 0, .width = output->width, .height = output->height },
        .color = { .r = 0.08f, .g = 0.12f, .b = 0.20f, .a = 1.0f },
        .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
    };
    wlr_render_pass_add_rect(pass, &background);

    if (!wlr_render_pass_submit(pass) || !wlr_output_commit_state(output, &state)) {
        fprintf(stderr, "horizon: failed to commit output frame\n");
    }
    wlr_output_state_finish(&state);
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
    horizon_output->frame.notify = render_frame;
    wl_signal_add(&output->events.frame, &horizon_output->frame);

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

    server.backend = wlr_backend_autocreate(
        wl_display_get_event_loop(server.display), NULL);
    if (server.backend == NULL) {
        fprintf(stderr, "horizon: failed to create wlroots backend\n");
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (server.renderer == NULL ||
        !wlr_renderer_init_wl_display(server.renderer, server.display)) {
        fprintf(stderr, "horizon: failed to initialize renderer\n");
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (server.allocator == NULL) {
        fprintf(stderr, "horizon: failed to create allocator\n");
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    server.new_output.notify = handle_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    if (!wlr_backend_start(server.backend)) {
        fprintf(stderr, "horizon: failed to start wlroots backend\n");
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.display);
        return EXIT_FAILURE;
    }

    printf("Hello from horizon, a Wayland compositor!\n");
    printf("Listening on WAYLAND_DISPLAY=%s\n", socket);
    fflush(stdout);
    wl_display_run(server.display);

    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.display);
    return 0;
}
