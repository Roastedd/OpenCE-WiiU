/*
The CafeGLSL stage of the build without the compiler (make NOCOMPILER=1). If the full program does not start on a console --
the library is 22 MB of C and C++ whose constructors run before main -- this one does, and answers everything but the
compiler's questions.
*/

#include "../stages.h"

void stage_glsl(gx_session *s)
{
	report_section(s->report, "glsl");
	report_note(s->report, "this build does not link CafeGLSL (the build without the compiler): the compiler's questions are not asked");
	answers_set(s->answers, "cafeglsl.library", "NOT LINKED in this build: run the full one for the compiler's answers");
}
