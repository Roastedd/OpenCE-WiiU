/* the pieces of the game's cseries.h that source/cseries/debug_memory.c uses, for test_debug_memory.c */
#ifndef __DEBUG_MEMORY_STUB_CSERIES_H
#define __DEBUG_MEMORY_STUB_CSERIES_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char boolean;
typedef unsigned char byte;
typedef unsigned short word;
typedef float real;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define __cdecl

enum { _error_silent = 0 };
void error(int severity, char const *format, ...);

extern __thread char temporary[256];
char *csprintf(char *buffer, char const *format, ...);
void *csmemset(void *destination, int value, size_t size);
unsigned long local_random(void);

/* (the failure of a check is the program's: it says which and stops) */
#define MATCH_FILE(file) (file)
#define MATCH_LINE(line) (line)
void debug_memory_test_failed(char const *file, long line, char const *expression);
#define match_assert(file, line, expression) do { if (!(expression)) debug_memory_test_failed(file, line, #expression); } while (0)
#define match_vassert(file, line, expression, message) do { if (!(expression)) debug_memory_test_failed(file, line, #expression); } while (0)

void *debug_malloc(unsigned int size, boolean clear, const char *file, long line);
void debug_free(void *pointer, const char *file, long line);
void *debug_realloc(void *pointer, unsigned int size, const char *file, long line);
void debug_check_memory(const char *file, long line);
#define system_malloc malloc
#define system_free free
#define system_realloc realloc

typedef struct { unsigned long dwAvailPhys; } MEMORYSTATUS;
void GlobalMemoryStatus(MEMORYSTATUS *status);

#endif
