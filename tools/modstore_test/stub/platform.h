/* the platform layer's declarations that modstore.c uses, for the host test (tools/test_modstore.py), in place of platform.h
(which needs the XDK's headers) */
#ifndef MODSTORE_TEST_PLATFORM_H
#define MODSTORE_TEST_PLATFORM_H

void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
const char *platform_data_root(void);

#endif
