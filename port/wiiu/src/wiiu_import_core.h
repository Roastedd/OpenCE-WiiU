#ifndef XBOX_MAP_IMPORT_H
#define XBOX_MAP_IMPORT_H

/* progress returns nonzero to cancel. Destination is installed only after a
complete conversion; no source file is modified. */
typedef int (*xbox_map_import_progress)(void *context, char const *stage,
	unsigned long done, unsigned long total);
int xbox_map_import_file(char const *source, char const *destination,
	xbox_map_import_progress progress, void *context, char *error, int error_size);

#endif
