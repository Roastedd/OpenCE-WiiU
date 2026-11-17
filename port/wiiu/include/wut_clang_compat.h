#ifndef OPENCE_WUT_CLANG_COMPAT_H
#define OPENCE_WUT_CLANG_COMPAT_H

/* Current wut uses GCC's malloc(deallocator, argument) annotation. Clang
supports only the plain allocation hint; this changes no calling convention. */
#if defined(__clang__) && __has_include(<wut.h>)
#include <wut.h>
#undef WUT_FREED_BY
#define WUT_FREED_BY(function, index) __attribute__((malloc))
#endif

#endif
