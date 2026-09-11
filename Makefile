CC = cc
CFLAGS = -Wall -Wextra -std=c11
DEBUG_CFLAGS = $(if $(DEBUG),-DHORIZON_DEBUG,)
WLR_VERSION = 0.20
WLR_CFLAGS = -DWLR_USE_UNSTABLE -I/usr/include/wlroots-$(WLR_VERSION)
PIXMAN_CFLAGS = $(shell pkg-config --cflags pixman-1)
WAYLAND_LIBS = -lwlroots-$(WLR_VERSION) -lwayland-server -lpixman-1 -lxkbcommon

horizon: main.c config.c config.h input.c input.h decorator.c decorator.h
	$(CC) $(CFLAGS) $(DEBUG_CFLAGS) $(WLR_CFLAGS) $(PIXMAN_CFLAGS) -o horizon main.c config.c input.c decorator.c $(WAYLAND_LIBS)

horizon-tests: tests/input_test.c config.c config.h input.c input.h
	$(CC) $(CFLAGS) $(WLR_CFLAGS) -I. -o $@ tests/input_test.c config.c input.c

test: horizon-tests
	./horizon-tests

release:
	$(MAKE) clean
	$(CC) $(CFLAGS) -Os -DNDEBUG -U HORIZON_DEBUG $(WLR_CFLAGS) \
		$(PIXMAN_CFLAGS) -o horizon main.c config.c input.c decorator.c $(WAYLAND_LIBS)
	strip --strip-unneeded horizon

clean:
	rm -f horizon horizon-tests

.PHONY: clean release test
