#include "../stage.h"

#include <coreinit/core.h>
#include <coreinit/systeminfo.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <sys/stat.h>

#ifndef PROBE_BUILD_ID
#define PROBE_BUILD_ID "unknown"
#endif

void stage_system(stage_context *context)
{
	report *r = context->report;
	const OSSystemInfo *info = OSGetSystemInfo();
	struct stat card;

	report_section(r, "system");
	report_value(r, "probe build", "%s", PROBE_BUILD_ID);
	report_value(r, "title id", "%016llX", (unsigned long long)OSGetTitleID());
	report_value(r, "os id", "%016llX", (unsigned long long)OSGetOSID());
	report_value(r, "process sdk version", "%u", (unsigned)__OSGetProcessSDKVersion());
	report_value(r, "core clock", "%u Hz", (unsigned)info->coreClockSpeed);
	report_value(r, "bus clock", "%u Hz", (unsigned)info->busClockSpeed);
	report_value(r, "timer clock", "%u Hz", (unsigned)OSTimerClockSpeed);
	report_value(r, "main thread on core", "%u", (unsigned)OSGetCoreId());
	report_value(r, "home button menu", "%s", OSIsHomeButtonMenuEnabled() ? "enabled" : "disabled");
	report_check(r, OSGetCoreCount() == 3, "cores", "%u, expected 3", (unsigned)OSGetCoreCount());
	/* Espresso: 1.243 GHz core, 248.625 MHz bus (a Wii U running at the speed of the Wii is not this) */
	report_check(r, info->coreClockSpeed == 1243125000u, "core clock", "%u Hz, expected 1243125000", (unsigned)info->coreClockSpeed);
	report_check(r, info->busClockSpeed == 248625000u, "bus clock", "%u Hz, expected 248625000", (unsigned)info->busClockSpeed);
	report_check(r, stat("/vol/external01", &card) == 0, "sd card", "/vol/external01 is there");
}
