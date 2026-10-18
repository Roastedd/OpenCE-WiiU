#include "stage.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int stopping(const stage_context *context)
{
	return context->keep_going && !context->keep_going();
}

static int runtime_big_endian(void)
{
	const union
	{
		uint32_t word;
		uint8_t bytes[4];
	} probe = {0x01020304u};

	return probe.bytes[0] == 1;
}

/* the first byte of a structure whose only member is a 3-bit field holding 1 */
static unsigned bit_field_first_byte(void)
{
	struct
	{
		unsigned a : 3;
	} field;
	unsigned char byte;

	memset(&field, 0, sizeof(field));
	field.a = 1;
	byte = *(const unsigned char *)&field;
	return byte;
}

void stage_abi(stage_context *context, const stage_abi_expectation *expected)
{
	report *r = context->report;
	struct alignment_probe
	{
		char c;
		double d;
	};
	struct alignment_probe_long
	{
		char c;
		long long l;
	};
	const unsigned bits = bit_field_first_byte();
	const unsigned expected_bits = expected->big_endian ? 0x20u : 0x01u;
	const int char_signed = (char)0xFF < 0;
	const unsigned wchar = (unsigned)sizeof(L'a');

	if (stopping(context))
		return;
	report_section(r, "abi");
#if defined(__clang__)
	report_value(r, "compiler", "clang %s", __clang_version__);
#elif defined(__GNUC__)
	report_value(r, "compiler", "gcc %s", __VERSION__);
#else
	report_value(r, "compiler", "unknown");
#endif
	report_check(r, runtime_big_endian() == expected->big_endian, "byte order", "%s, expected %s",
		runtime_big_endian() ? "big-endian" : "little-endian", expected->big_endian ? "big-endian" : "little-endian");
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
	report_check(r, (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) == runtime_big_endian(), "byte order macro",
		"the compiler's macro agrees with the machine");
#endif
	report_check(r, sizeof(void *) == expected->pointer_size, "pointer size", "%u, expected %u",
		(unsigned)sizeof(void *), expected->pointer_size);
	report_check(r, sizeof(long) == expected->long_size, "long size", "%u, expected %u", (unsigned)sizeof(long),
		expected->long_size);
	report_check(r, sizeof(long long) == 8, "long long size", "%u, expected 8", (unsigned)sizeof(long long));
	report_check(r, wchar == expected->wchar_size, "wchar_t size", "%u, expected %u", wchar, expected->wchar_size);
	report_check(r, char_signed == expected->char_signed, "char signedness", "%s, expected %s",
		char_signed ? "signed" : "unsigned", expected->char_signed ? "signed" : "unsigned");
	report_check(r, offsetof(struct alignment_probe, d) == expected->double_alignment, "double alignment",
		"%u, expected %u", (unsigned)offsetof(struct alignment_probe, d), expected->double_alignment);
	report_check(r, offsetof(struct alignment_probe_long, l) == expected->double_alignment, "long long alignment",
		"%u, expected %u", (unsigned)offsetof(struct alignment_probe_long, l), expected->double_alignment);
	report_check(r, bits == expected_bits, "bit-field order", "a 3-bit field holding 1 is the byte %02X, expected %02X",
		bits, expected_bits);
#if defined(__FLT_EVAL_METHOD__)
	report_check(r, __FLT_EVAL_METHOD__ == 0, "float evaluation", "FLT_EVAL_METHOD %d, expected 0 (results rounded to their type)",
		(int)__FLT_EVAL_METHOD__);
#endif
	report_value(r, "sizeof long double", "%u", (unsigned)sizeof(long double));
	report_value(r, "sizeof size_t", "%u", (unsigned)sizeof(size_t));
#if defined(__OPTIMIZE__)
	report_value(r, "optimised", "yes");
#else
	report_value(r, "optimised", "no");
#endif
}
