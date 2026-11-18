#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// helper fn macros
#define max(a,b)	((a)>(b)?(a):(b))
#define min(a,b)	((a)<(b)?(a):(b))

char *fgetl(FILE *); // gets a line of string data; returns a malloc-like pointer
void init_char(char **buf, int *l, int *i); // initialises a string buffer in heap.  *buf becomes a malloc-like pointer
void append_char(char **buf, int *l, int *i, char c); // adds a character to a string buffer in heap (and realloc()s if needed)

#ifdef WINDOWS
	#define NEED_STRNDUP	1
#endif

#ifdef NEED_STRNDUP
	char *strndup(const char *str, size_t len);
#endif
