/*
The system stage: what the console says about its GPU path, and everything the later stages draw with. GX2 itself is
started by main (the screen has to exist to say what is running); this takes the rest from there: the shaders, the
pages, the target the tests read back.
*/

#include "../gsh_check.h"
#include "stages.h"

#include <gx2/display.h>
#include <string.h>
#include <vpad/input.h>

static void check_embedded_files(gx_session *s)
{
	unsigned i;

	for (i = 0; i < GSH_COUNT; i++)
	{
		gsh_summary summary;
		const char *problem = gsh_check(gsh_files[i].data, gsh_files[i].size, &summary);

		report_check(s->report, problem == NULL, "embedded shader file", "%s: %lu bytes, GFD %u.%u%s%s", gsh_files[i].name, gsh_files[i].size,
			summary.major, summary.minor, problem ? ", " : "", problem ? problem : "");
	}
}

void stage_system(gx_session *s)
{
	report *r = s->report;
	char error[200];
	const GX2ColorBuffer *tv = WHBGfxGetTVColourBuffer(), *drc = WHBGfxGetDRCColourBuffer();

	report_section(r, "system");
	report_value(r, "tv color buffer", "%ux%u, format %#x, tile mode %d", (unsigned)tv->surface.width, (unsigned)tv->surface.height,
		(unsigned)tv->surface.format, (int)tv->surface.tileMode);
	report_value(r, "gamepad color buffer", "%ux%u, format %#x, tile mode %d", (unsigned)drc->surface.width, (unsigned)drc->surface.height,
		(unsigned)drc->surface.format, (int)drc->surface.tileMode);
	report_value(r, "system tv aspect", "%d (0 4:3, 1 16:9)", (int)GX2GetSystemTVAspectRatio());
	report_value(r, "system tv scan mode", "%d", (int)GX2GetSystemTVScanMode());
	report_value(r, "system gamepad mode", "%d", (int)GX2GetSystemDRCMode());
	report_value(r, "gx2 main core", "%d", (int)GX2GetMainCoreId());

	VPADInit();
	check_embedded_files(s);

	if (!ui_init(error, sizeof(error)))
	{
		report_check(r, 0, "ui shaders", "%s", error);
		return;
	}
	report_check(r, 1, "ui shaders", "loaded by the system's GFD loader, fetch shader built");
	report_shader_pair(r, "ui", ui_vertex_shader(), ui_pixel_shader());
	if (!pages_init(error, sizeof(error)))
	{
		report_check(r, 0, "pages", "%s", error);
		return;
	}
	report_check(r, 1, "pages", "text layers and their textures for the TV and the GamePad");
	if (!gx_target_create(&s->scene, SCENE_SIZE, SCENE_SIZE, error, sizeof(error)))
	{
		report_check(r, 0, "scene target", "%s", error);
		return;
	}
	report_check(r, 1, "scene target", "%ux%u RGBA8, linear aligned, pitch %u bytes, %u bytes", SCENE_SIZE, SCENE_SIZE, s->scene.pitch_bytes,
		(unsigned)s->scene.color.surface.imageSize);

	if (!gx_pipeline_load(&s->attr, &gsh_files[GSH_ATTR]) || !gx_pipeline_load(&s->packed, &gsh_files[GSH_PACKED]) ||
		!gx_pipeline_load(&s->generated, &gsh_files[GSH_GENERATED]))
	{
		report_check(r, 0, "test shaders", "the system's loader refused one of the embedded GSH files");
		return;
	}
	report_check(r, 1, "test shaders", "attr, packed and generated pairs loaded");
	report_shader_pair(r, "attr", s->attr.group.vertexShader, s->attr.group.pixelShader);
	report_shader_pair(r, "packed", s->packed.group.vertexShader, s->packed.group.pixelShader);
	report_shader_pair(r, "generated (vs00 + diffuse, compiled offline)", s->generated.group.vertexShader, s->generated.group.pixelShader);
	s->ready = 1;
	session_set_current(s);
}
