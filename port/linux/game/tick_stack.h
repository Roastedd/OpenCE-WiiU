/*
TICK_STACK.H

Which thread is running, found from its stack: the drawing thread's stack is one range of memory (its OS thread's), and every other thread's, the tick
thread's among them, is elsewhere. The test is the stack pointer less the range's low end, against its size: no call and no thread-local lookup, which on
the Wii U are both a call into the C library (an emulated __thread variable is a call, and so is the OS's core number), and the ticks ask this on every
object they read while a frame is drawn (the render view's accessors, the markers of the collision tests and the object searches, the game clock).

The range is set once, by the drawing thread, when its main loop starts (tick_overlap_note_render_thread). Until then it is empty, and no thread is the drawing thread.

Compiled for the Wii U only (HALO_WIIU).
*/

#ifndef __TICK_STACK_H
#define __TICK_STACK_H

#ifdef HALO_WIIU

extern unsigned long tick_overlap_stack_low;
extern unsigned long tick_overlap_stack_size;

#if defined(__powerpc__) || defined(__powerpc)
#define TICK_STACK_POINTER() ({ unsigned long tick_stack_pointer_; __asm__("mr %0, 1" : "=r"(tick_stack_pointer_)); tick_stack_pointer_; })
#else
#define TICK_STACK_POINTER() ((unsigned long)__builtin_frame_address(0))
#endif

/* the calling thread is the drawing thread */
#define TICK_OVERLAP_ON_RENDER_STACK() (TICK_STACK_POINTER() - tick_overlap_stack_low < tick_overlap_stack_size)

#endif

#endif
