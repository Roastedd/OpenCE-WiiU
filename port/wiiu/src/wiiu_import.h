#ifndef WIIU_IMPORT_H
#define WIIU_IMPORT_H

/* Called on the process thread after WHBProcInit, before game initialization.
Returns one when existing or newly imported maps are ready; zero on exit. */
int wiiu_import_startup(void);

#endif
