/*
The Wii U GX2 probe: the renderer's questions of plan phase 0, answered on the console (plan section 6.4).

 - Does CafeGLSL, the library that compiles GLSL to GX2 shaders on the console, accept and run the shaders OpenCE's
   generator writes?
 - In which byte order does the GPU read BC1 and BC3 (DXT) textures?
 - Do little-endian vertex streams work through GX2AttribStream's endianSwap?
 - Do GX2_INDEX_TYPE_U16_LE and U32_LE index buffers draw right?

It runs its stages once, one after the other, and writes what it finds as text:

 - to /vol/external01/wiiu/apps/opence-probe-gx2/report-<compiler>.txt on the SD card, after every stage and before
   every risky step, so that the report ends where the console stopped;
 - to crash-<compiler>.txt, if the system tells us about a fault;
 - on the TV and the GamePad, as pages of pictures and text (A and B turn the pages): every test is drawn.

The stages that only draw come first; the compiler, which is someone else's 22 MB, comes last.
*/

#include "../answers.h"
#include "card.h"
#include "clock.h"
#include "crash.h"
#include "stages.h"

#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <stdio.h>
#include <string.h>
#include <vpad/input.h>
#include <whb/proc.h>

#define REPORT_CAPACITY (160 * 1024)

#ifndef PROBE_BUILD_ID
#define PROBE_BUILD_ID "unknown"
#endif

static char report_storage[REPORT_CAPACITY];
static report probe_report;
static answers probe_answers;
static gx_session session;
static page overview, status;
static int card_error;
static int card_started;
static int leaving;

/* Writes what the report holds that the card does not. The first write makes the file, the rest add to it. */
static void flush_report(void)
{
	size_t size;
	const char *pending = report_pending(&probe_report, &size);
	int error;

	if (!size)
		return;
	error = card_write(PROBE_REPORT_FILE, pending, size, card_started);
	if (error)
	{
		card_error = error;
		return; /* what could not be written is tried again with the next stage */
	}
	card_started = 1;
	report_mark_flushed(&probe_report, size);
}

/* Asks the system whether the user has asked to leave (HOME, then closing the app). WHBProcIsRunning does not come back
while the app is in the background, and nothing is asked of it after it said no. */
static int keep_going(void)
{
	if (!leaving && !WHBProcIsRunning())
		leaving = 1;
	return !leaving;
}

/* ---------- what is on the screen between the stages */

static void show_status(const char *running)
{
	unsigned i;

	if (!ui_ready())
		return;
	page_clear(&status);
	page_text(&status, 1, 0, STYLE_HEAD, "OpenCE Wii U GX2 probe (%s, build %s)", PROBE_FLAVOUR, PROBE_BUILD_ID);
	page_text(&status, 1, 2, STYLE_NORMAL, "running: %s ...", running);
	page_text(&status, 1, 3, STYLE_DIM, "If this stays for a minute, hold the power button: the report on the card ends at the step that did not return.");
	for (i = 0; i < probe_answers.count && i < 32; i++)
		page_text(&status, 1, 5 + i, STYLE_DIM, "%s = %.80s", probe_answers.key[i], probe_answers.text[i]);
	pages_dirty();
	pages_show(&status);
}

static void run_stage(const char *name, void (*stage)(gx_session *))
{
	if (!keep_going())
		return;
	crash_set_stage(name);
	report_value(&probe_report, "stage", "%s started", name);
	flush_report();
	show_status(name);
	stage(&session);
	flush_report();
}

int main(void)
{
	int gx_ok;
	unsigned shown = 0, total;

	WHBProcInit();
	crash_install();
	card_prepare();
	crash_set_stage("starting");

	report_init(&probe_report, report_storage, sizeof(report_storage), NULL, NULL);
	answers_init(&probe_answers);
	memset(&session, 0, sizeof(session));
	session.report = &probe_report;
	session.answers = &probe_answers;
	session.now_ns = clock_now_ns;
	session.keep_going = keep_going;
	session.flush = flush_report;
	session_set_current(&session);

	report_section(&probe_report, "start");
	report_value(&probe_report, "program", "OpenCE Wii U GX2 probe");
	report_value(&probe_report, "compiler", "%s", PROBE_FLAVOUR);
	report_value(&probe_report, "build", "%s", PROBE_BUILD_ID);
	report_note(&probe_report, "stages: system, control, index, vertex, textures, glsl (the compiler last: it is 22 MB of someone else's code)");
	flush_report();

	crash_set_stage("GX2 init");
	gx_ok = WHBGfxInit();
	report_check(&probe_report, gx_ok, "GX2 initialisation", "WHBGfxInit %s", gx_ok ? "succeeded" : "failed");
	flush_report();

	if (gx_ok)
	{
		run_stage("system", stage_system);
		run_stage("control", stage_control);
		run_stage("index", stage_index);
		run_stage("vertex", stage_vertex);
		run_stage("textures", stage_textures);
		run_stage("glsl", stage_glsl);
	}

	crash_set_stage("finished");
	if (leaving)
		report_note(&probe_report, "stopped early: the user left");
	answers_report(&probe_answers, &probe_report);
	report_summary(&probe_report);
	flush_report();

	if (gx_ok && ui_ready())
	{
		total = session.page_count + 1;
		session_overview(&session, &overview, PROBE_FLAVOUR, PROBE_BUILD_ID);
		if (card_error)
			page_text(&overview, 1, 37, STYLE_BAD, "the report could not be written to the card (error %d): photograph these pages", card_error);
		pages_dirty();
		while (keep_going())
		{
			VPADStatus vpad;
			VPADReadError error;

			if (VPADRead(VPAD_CHAN_0, &vpad, 1, &error) > 0 && error == VPAD_READ_SUCCESS)
			{
				if (vpad.trigger & (VPAD_BUTTON_A | VPAD_BUTTON_RIGHT | VPAD_BUTTON_R | VPAD_BUTTON_ZR))
					shown = (shown + 1) % total;
				if (vpad.trigger & (VPAD_BUTTON_B | VPAD_BUTTON_LEFT | VPAD_BUTTON_L | VPAD_BUTTON_ZL))
					shown = (shown + total - 1) % total;
			}
			pages_show(shown == 0 ? &overview : &session.pages[shown - 1]);
		}
	}
	else
	{
		/* nothing to draw with: wait to be told to leave, so that the card's report can be taken */
		while (keep_going())
			OSSleepTicks(OSMillisecondsToTicks(100));
	}

	WHBGfxShutdown();
	WHBProcShutdown();
	return 0;
}
