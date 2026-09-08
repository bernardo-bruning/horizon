#ifndef HORIZON_DECORATOR_H
#define HORIZON_DECORATOR_H

#include <stdbool.h>

struct wlr_scene_rect;
struct wlr_scene_tree;

struct horizon_decorator {
	struct wlr_scene_rect *top;
	struct wlr_scene_rect *bottom;
	struct wlr_scene_rect *left;
	struct wlr_scene_rect *right;
	int border_width;
};

struct horizon_decorator *horizon_decorator_create(
	struct wlr_scene_tree *parent, int border_width,
	const float color[static 4]);
void horizon_decorator_update(struct horizon_decorator *decorator,
	int x, int y, int width, int height);
void horizon_decorator_destroy(struct horizon_decorator *decorator);

#endif
