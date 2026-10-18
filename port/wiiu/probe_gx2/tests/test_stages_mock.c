/*
The probe's stages on hypothetical consoles: tests/mock/mock_gx2.c stands in for the GX2 driver, with a GPU that behaves as a
scenario says (a byte order of BC1 and BC3 blocks, a channel order of the target, the _LE index types working or not, the
compiler accepting the ES dialect or not ...), and the stages run on it exactly as main.c runs them. What is checked is what
the stages *conclude*: the answers and the report, which have to name the scenario's behaviour back. Built for 32-bit
big-endian PowerPC and run under qemu-ppc: tools/wiiu/probe_gx2_tests.sh. One scenario to a process (the stages keep their
test cases in static memory): test_stages_mock <number>; no number lists them. The report of the scenario is written to
the file given as the second argument, if any.
*/

#include "../src/answers.h"
#include "../src/wiiu/stages.h"
#include "mock/mock_gx2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void crash_set_stage(const char *stage)
{
	(void)stage;
}

static uint64_t fake_now(void)
{
	static uint64_t now;

	now += 7000000; /* 7 ms to everything */
	return now;
}

static int keep_going(void)
{
	return 1;
}

static void flush_nothing(void)
{
}

static char report_storage[256 * 1024];
static report probe_report;
static answers probe_answers;
static gx_session session;
static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("check failed, line %d: %s\n", __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* the answer under key contains the text */
static int says(const char *key, const char *text)
{
	const char *answer = answers_get(&probe_answers, key);

	if (!answer || !strstr(answer, text))
	{
		printf("answer %s is \"%s\", expected to contain \"%s\"\n", key, answer ? answer : "(none)", text);
		return 0;
	}
	return 1;
}

static int report_has(const char *text)
{
	if (!strstr(report_storage, text))
	{
		printf("the report lacks \"%s\"\n", text);
		return 0;
	}
	return 1;
}

typedef struct scenario
{
	const char *name;
	void (*configure)(void);
	void (*expect)(void);
} scenario;

static void set_structured(bc_permutation *p, bc_format format, bc_order order)
{
	uint8_t identity[16];
	unsigned i;

	for (i = 0; i < 16; i++)
		identity[i] = (uint8_t)i;
	memset(p, 0, sizeof(*p));
	bc_apply_order(order, bc_block_bytes(format), identity, p->source);
}

/* ---------- scenarios */

static void expect_basics(void)
{
	CHECK(says("readback", "works"));
	CHECK(says("texture.rgba8", "works"));
	CHECK(says("index.control", "fails as modelled"));
	CHECK(says("cafeglsl.library", "v0.3.0"));
}

static void good(void)
{
	/* a GPU that wants BC1 as 16-bit swapped words and BC3 with its halves swapped by 32 bits, like a big-endian machine's loads */
	set_structured(&mock.bc1_gpu, BC_FORMAT_BC1, BC_ORDER_SWAP32);
	set_structured(&mock.bc2_gpu, BC_FORMAT_BC2, BC_ORDER_SWAP16);
	set_structured(&mock.bc3_gpu, BC_FORMAT_BC3, BC_ORDER_SWAP64);
}

static void expect_good(void)
{
	expect_basics();
	CHECK(says("texture.bc1", "draws right when uploaded: swap32"));
	CHECK(says("texture.bc1", "the GPU reads a block as: swap32"));
	CHECK(says("texture.bc2", "draws right when uploaded: swap16"));
	CHECK(says("texture.bc3", "draws right when uploaded: swap64"));
	CHECK(says("vertex.float32x4", "works with swap: none; DEFAULT behaves as 8_IN_32"));
	CHECK(says("vertex.float32x2", "works with swap: none"));
	CHECK(says("vertex.d3dcolor", "works with swap: none, 8_IN_16, 8_IN_32"));
	CHECK(says("vertex.snorm16x4", "works with swap: none; DEFAULT behaves as 8_IN_32"));
	CHECK(says("vertex.short2", "works with swap: none"));
	CHECK(says("vertex.normpacked3", "works with swap: none"));
	CHECK(says("index.u16_le", "reads the indices as written"));
	CHECK(says("index.u32_le", "reads the indices as written"));
	CHECK(says("index.u16", "reads the indices as written"));
	CHECK(says("index.u32", "reads the indices as written"));
	CHECK(says("cafeglsl.compiles", "8 of 8 shader pairs compiled"));
	CHECK(says("cafeglsl.compiles", "is accepted"));
	CHECK(says("cafeglsl.draw_runtime", "draws the triangle right"));
	CHECK(says("cafeglsl.draw_offline", "draws right"));
	CHECK(says("cafeglsl.same_as_mac", "byte for byte the same"));
	CHECK(says("cafeglsl.memory", "MEM2 free"));
	CHECK(report_has("PASS interpolants:"));
	CHECK(!strstr(report_storage, "FAIL "));
}

static void bgra_target(void)
{
	good();
	mock.target_order.source[0] = 2;
	mock.target_order.source[1] = 1;
	mock.target_order.source[2] = 0;
	mock.target_order.source[3] = 3;
}

static void expect_bgra(void)
{
	expect_good();
	CHECK(report_has("target byte order = r from byte 2, g from byte 1, b from byte 0, a from byte 3"));
}

static void exotic_bc1(void)
{
	static const uint8_t odd[8] = { 2, 3, 0, 1, 7, 6, 5, 4 };

	good();
	memcpy(mock.bc1_gpu.source, odd, 8);
}

static void expect_exotic_bc1(void)
{
	CHECK(says("texture.bc1", "none of the orders tried draws right"));
	CHECK(says("texture.bc1", "the GPU reads a block as: bytes 2,3,0,1,7,6,5,4"));
	CHECK(says("texture.bc1", "draws right"));
	CHECK(says("texture.bc3", "draws right when uploaded: swap64"));
	CHECK(report_has("PASS texture derived order:"));
}

static void le_indices_broken(void)
{
	good();
	mock.le_indices_broken = 1;
}

static void expect_le_indices_broken(void)
{
	CHECK(says("index.u16_le", "DOES NOT"));
	CHECK(says("index.u32_le", "DOES NOT"));
	CHECK(says("index.u16", "reads the indices as written"));
	CHECK(says("index.u32", "reads the indices as written"));
	CHECK(report_has("FAIL index:"));
}

static void es_refused(void)
{
	good();
	mock.fail_es_dialect = 1;
}

static void expect_es_refused(void)
{
	/* only the probe's own GLSL 330 pair compiles */
	CHECK(says("cafeglsl.compiles", "1 of 8 shader pairs compiled"));
	CHECK(says("cafeglsl.compiles", "REFUSED"));
	CHECK(says("cafeglsl.draw_runtime", "no draw"));
	CHECK(says("cafeglsl.draw_offline", "draws right"));
	CHECK(report_has("mock refuses this shader"));
}

static void upside_down(void)
{
	good();
	mock.rows_flipped = 1;
}

static void expect_upside_down(void)
{
	CHECK(says("texture.rgba8", "upside down"));
	CHECK(says("texture.bc1", "draws right when uploaded: swap32"));
	CHECK(says("texture.bc2", "draws right when uploaded: swap16"));
	CHECK(says("texture.bc3", "draws right when uploaded: swap64"));
}

static void blocks_pitch(void)
{
	good();
	mock.pitch_in_blocks = 1;
}

static void expect_blocks_pitch(void)
{
	CHECK(says("texture.bc1", "draws right when uploaded: swap32"));
	CHECK(says("texture.bc2", "draws right when uploaded: swap16"));
	CHECK(says("texture.bc3", "draws right when uploaded: swap64"));
	CHECK(report_has("texture layout = BC1: pitch 64 texels, 512 bytes a row of blocks"));
}

static void unreadable(void)
{
	good();
	mock.target_unreadable = 1;
}

static void expect_unreadable(void)
{
	CHECK(says("readback", "DOES NOT WORK"));
	CHECK(report_has("[index]\n# skipped: the target cannot be read back"));
	CHECK(answers_get(&probe_answers, "texture.bc1") == NULL);
	CHECK(says("cafeglsl.compiles", "8 of 8"));
}

static void identity_bc(void)
{
	/* a GPU that reads the Xbox's block as it is */
}

static void expect_identity_bc(void)
{
	CHECK(says("texture.bc1", "draws right when uploaded: as it is"));
	CHECK(says("texture.bc2", "draws right when uploaded: as it is"));
	CHECK(says("texture.bc3", "draws right when uploaded: as it is"));
}

static void default_is_none(void)
{
	/* DEFAULT does not swap: a little-endian stream works with it, the CPU's own floats do not (nothing drawn) */
	good();
	mock.default_swap = VTX_SWAP_NONE;
}

static void expect_default_is_none(void)
{
	/* the positions are fetched with DEFAULT: nothing is drawn, and the stage says so rather than a wrong value */
	CHECK(report_has("NOTHING DRAWN"));
}

static void different_programs(void)
{
	good();
	mock.different_programs = 1;
}

static void expect_different_programs(void)
{
	CHECK(says("cafeglsl.same_as_mac", "first difference at 0"));
	CHECK(says("cafeglsl.draw_runtime", "draws the triangle right"));
}

static const scenario scenarios[] =
{
	{ "a console that behaves (BC1 swap32, BC3 swap64, default swap 8_IN_32)", good, expect_good },
	{ "the target's bytes in the order B, G, R, A", bgra_target, expect_bgra },
	{ "a BC1 order none of the named ones is", exotic_bc1, expect_exotic_bc1 },
	{ "the _LE index types read as if they swapped", le_indices_broken, expect_le_indices_broken },
	{ "the compiler refuses the ES dialect", es_refused, expect_es_refused },
	{ "textures sampled upside down", upside_down, expect_upside_down },
	{ "a compressed surface's pitch counts blocks", blocks_pitch, expect_blocks_pitch },
	{ "the CPU never sees what the GPU wrote", unreadable, expect_unreadable },
	{ "a GPU that reads blocks as they are", identity_bc, expect_identity_bc },
	{ "DEFAULT does not swap", default_is_none, expect_default_is_none },
	{ "the runtime-compiled program differs from the Mac's", different_programs, expect_different_programs },
};
#define SCENARIO_COUNT (sizeof(scenarios) / sizeof(scenarios[0]))

/* a page as text: the grid, with the rectangles of the pictures marked where there is no text (# green frame, + none, ! red) */
static void dump_page(FILE *f, const char *what, const page *p)
{
	unsigned row, col, tile;

	fprintf(f, "==== %s ====\n", what);
	for (row = 0; row < PAGE_ROWS; row++)
	{
		char line[PAGE_COLS + 1];

		memcpy(line, p->grid[row], PAGE_COLS);
		line[PAGE_COLS] = 0;
		for (tile = 0; tile < p->tile_count; tile++)
		{
			const page_tile *t = &p->tiles[tile];

			if (row < t->row || row >= t->row + t->rows)
				continue;
			for (col = t->col; col < t->col + t->cols && col < PAGE_COLS; col++)
				if (line[col] == ' ')
					line[col] = (char)(t->border == STYLE_GOOD ? '#' : t->border == STYLE_BAD ? '!' : '+');
		}
		for (col = PAGE_COLS; col > 0 && line[col - 1] == ' '; col--)
			;
		line[col] = 0;
		fprintf(f, "%2u|%s\n", row, line);
	}
}

int main(int argc, char **argv)
{
	unsigned n;

	if (argc < 2)
	{
		for (n = 0; n < SCENARIO_COUNT; n++)
			printf("%u %s\n", n, scenarios[n].name);
		return 0;
	}
	n = (unsigned)atoi(argv[1]);
	if (n >= SCENARIO_COUNT)
		return 2;
	mock_reset();
	scenarios[n].configure();

	report_init(&probe_report, report_storage, sizeof(report_storage), NULL, NULL);
	answers_init(&probe_answers);
	session.report = &probe_report;
	session.answers = &probe_answers;
	session.now_ns = fake_now;
	session.keep_going = keep_going;
	session.flush = flush_nothing;
	session_set_current(&session);

	stage_system(&session);
	stage_control(&session);
	stage_index(&session);
	stage_vertex(&session);
	stage_textures(&session);
	stage_glsl(&session);
	answers_report(&probe_answers, &probe_report);
	report_summary(&probe_report);

	if (argc > 2)
	{
		FILE *f = fopen(argv[2], "w");

		if (f)
		{
			fputs(report_storage, f);
			fclose(f);
		}
	}
	{
		static page overview;
		FILE *f = argc > 3 ? fopen(argv[3], "w") : NULL;
		unsigned page_index;

		session_overview(&session, &overview, "mock", "test");
		CHECK(strstr(overview.grid[2], "readback") != NULL);
		if (f)
		{
			dump_page(f, "overview", &overview);
			for (page_index = 0; page_index < session.page_count; page_index++)
				dump_page(f, "page", &session.pages[page_index]);
			fclose(f);
		}
	}
	CHECK(probe_report.truncated == 0);
	CHECK(session.page_count >= 1);
	{
		unsigned page_index;

		for (page_index = 0; page_index < session.page_count; page_index++)
			CHECK(session.pages[page_index].dropped == 0);
	}
	{
		/* every page is drawn, as the viewer does (the picture callbacks run; nothing is checked of the pixels) */
		unsigned page_index, before = mock_draws;

		for (page_index = 0; page_index < session.page_count; page_index++)
		{
			pages_dirty();
			pages_show(&session.pages[page_index]);
		}
		CHECK(mock_draws > before);
	}
	scenarios[n].expect();
	printf("scenario %u (%s): %s\n", n, scenarios[n].name, failures ? "FAILED" : "ok");
	return failures ? 1 : 0;
}
