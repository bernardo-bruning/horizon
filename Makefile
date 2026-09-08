CC = cc
CFLAGS = -Wall -Wextra -std=c11

horizon: main.c
	$(CC) $(CFLAGS) -o horizon main.c

clean:
	rm -f horizon

.PHONY: clean
