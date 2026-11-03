/*
HALO_STARTUP.H

The Wii U's start-up timeline (port/linux/game/startup_timeline.c): where the first seconds of a run go, said in one block of log lines once the
main menu is live, so that a console run shows what a change to the start-up gained. Marks name the points of the game thread's start-up in
order; a timer adds up a kind of work (the PNGs decoded, the files read) whichever thread does it. Only the Wii U build (HALO_WIIU, and the
digest harness's) has any of this; every other build is untouched, the macros being nothing there.
*/

#ifndef HALO_STARTUP_H
#define HALO_STARTUP_H

/* the start-up changes, each of which the card's halo/startup.txt (a number; no file: all) can switch off by its bit, to compare a run with the
build's old start-up: */
enum
{
	/* the menus' XML is read by a thread of its own from the start, not by the game thread when the menus' map has loaded (menu_files.c) */
	STARTUP_PRELOAD_MENUS = 2,
	/* and that thread then makes the images of the pictures the main menu draws, as the game thread would when it first draws them (xbox_textures_gx2.c) */
	STARTUP_PREWARM_MENU_ART = 4,
	/* the shader pack is read by a thread of its own from the start, not by the GX2 thread at the first draw (gx2_shaders.c) */
	STARTUP_PREFETCH_SHADER_PACK = 8,
};

#ifdef HALO_WIIU

/* nonzero once the report is made: the timers are nothing then */
extern volatile int startup_timeline_finished;
void startup_timeline_mark(const char *name);
void startup_timeline_note(const char *name);
long long startup_timeline_now(void);
void startup_timeline_add(const char *name, long long started, unsigned long items, unsigned long bytes);
void startup_timeline_frame(void);
int startup_timeline_running(void);
int startup_option(unsigned long bit);


/* the game thread reached this point of its start-up (the segments of the one-line breakdown are the gaps between marks) */
#define STARTUP_MARK(name) startup_timeline_mark(name)
/* another thread's event, said with its time since the start */
#define STARTUP_NOTE(name) startup_timeline_note(name)
/* a piece of work to add up under a name: STARTUP_TIMER(t); ...; STARTUP_ADD("png", t, 1, bytes) */
#define STARTUP_TIMER(variable) long long variable = startup_timeline_finished ? 0 : startup_timeline_now()
#define STARTUP_ADD(name, variable, items, bytes) do { if (variable) startup_timeline_add((name), (variable), (unsigned long)(items), (unsigned long)(bytes)); } while (0)
#define STARTUP_FRAME() startup_timeline_frame()
#define STARTUP_OPTION(bit) startup_option(bit)

#else

#define STARTUP_MARK(name) ((void)0)
#define STARTUP_NOTE(name) ((void)0)
#define STARTUP_TIMER(variable) long long variable = 0
#define STARTUP_ADD(name, variable, items, bytes) ((void)(variable))
#define STARTUP_FRAME() ((void)0)
#define STARTUP_OPTION(bit) 0

#endif

#endif
