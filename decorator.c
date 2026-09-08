#include "decorator.h"

#include <stdlib.h>

#include <wlr/types/wlr_scene.h>

struct horizon_decorator *horizon_decorator_create(
	struct wlr_scene_tree *parent, int border_width,
	const float color[static 4]) {
	if (parent == NULL || border_width <= 0) {
		return NULL;
	}

	struct horizon_decorator *decorator = calloc(1, sizeof(*decorator));
	if (decorator == NULL) {
		return NULL;
	}
	decorator->border_width = border_width;
	decorator->top = wlr_scene_rect_create(parent, 0, 0, color);
	decorator->bottom = wlr_scene_rect_create(parent, 0, 0, color);
	decorator->left = wlr_scene_rect_create(parent, 0, 0, color);
	decorator->right = wlr_scene_rect_create(parent, 0, 0, color);
	if (decorator->top == NULL || decorator->bottom == NULL ||
		decorator->left == NULL || decorator->right == NULL) {
		horizon_decorator_destroy(decorator);
		return NULL;
	}
	return decorator;
}

void horizon_decorator_update(struct horizon_decorator *decorator,
	int x, int y, int width, int height) {
	if (decorator == NULL) {
		return;
	}

	int border = decorator->border_width;
	if (width < 2 * border) {
		border = width / 2;
	}
	if (height < 2 * border) {
		border = height / 2;
	}
	if (width <= 0 || height <= 0 || border <= 0) {
		return;
	}

	wlr_scene_node_set_position(&decorator->top->node, x, y);
	wlr_scene_rect_set_size(decorator->top, width, border);
	wlr_scene_node_set_position(&decorator->bottom->node,
		x, y + height - border);
	wlr_scene_rect_set_size(decorator->bottom, width, border);
	wlr_scene_node_set_position(&decorator->left->node,
		x, y + border);
	wlr_scene_rect_set_size(decorator->left, border, height - 2 * border);
	wlr_scene_node_set_position(&decorator->right->node,
		x + width - border, y + border);
	wlr_scene_rect_set_size(decorator->right, border, height - 2 * border);
}

void horizon_decorator_destroy(struct horizon_decorator *decorator) {
	if (decorator == NULL) {
		return;
	}
	struct wlr_scene_rect *rects[] = {
		decorator->top, decorator->bottom, decorator->left, decorator->right,
	};
	for (size_t i = 0; i < sizeof(rects) / sizeof(rects[0]); i++) {
		if (rects[i] != NULL) {
			wlr_scene_node_destroy(&rects[i]->node);
		}
	}
	free(decorator);
}
