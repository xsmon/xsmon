CC ?= gcc
CFLAGS += -Wall -Wextra -Werror -Wconversion -pedantic -std=c99 -O2

xsmon: main.c
	$(CC) $(CFLAGS) $^ -o $@ -lxcb
