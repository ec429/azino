CC := gcc
CFLAGS := -Wall -Wextra -Werror -pedantic --std=gnu99 -O3 -mavx2 -mfma -g
PREFIX := /usr/local

SDL = `sdl-config --cflags --libs` -lSDL_ttf -lSDL_gfx

all: azino

install: $(PREFIX)/bin/azino

$(PREFIX)/bin/azino: azino
	install -D azino $(PREFIX)/bin/azino

azino: azino.c bits.o bits.h
	$(CC) $(CFLAGS) azino.c -lm bits.o $(SDL) -o azino

%.o: %.c %.h
	$(CC) $(CFLAGS) -o $@ -c $<
