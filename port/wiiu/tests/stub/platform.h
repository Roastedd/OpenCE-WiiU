/* A stand-in for the port's platform.h: only what memory_watch_wiiu.c uses, so the file can be tested on this machine. */
#ifndef STUB_PLATFORM_H
#define STUB_PLATFORM_H
#include <stdint.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define PLATFORM_CONTIGUOUS_BASE 0x10000000UL
#define PLATFORM_CONTIGUOUS_SIZE 0x00100000UL
void memory_watch_initialize(void);
void memory_watch_protect(unsigned long address, unsigned long size);
unsigned long memory_watch_generation(unsigned long address, unsigned long size);
unsigned long memory_watch_serial(void);
void memory_watch_prepare_write(void *address, unsigned long size);
void memory_watch_forget(void *address, unsigned long size);
void memory_watch_begin_frame(void);
#endif
