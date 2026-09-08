CC = cc
CFLAGS = -Wall -Wextra -std=c11
WLR_VERSION = 0.20
WLR_CFLAGS = -DWLR_USE_UNSTABLE -I/usr/include/wlroots-$(WLR_VERSION)
PIXMAN_CFLAGS = $(shell pkg-config --cflags pixman-1)
WAYLAND_LIBS = -lwlroots-$(WLR_VERSION) -lwayland-server -lpixman-1 -lxkbcommon

horizon: main.c
	$(CC) $(CFLAGS) $(WLR_CFLAGS) $(PIXMAN_CFLAGS) -o horizon main.c $(WAYLAND_LIBS)

clean:
	rm -f horizon

.PHONY: clean
