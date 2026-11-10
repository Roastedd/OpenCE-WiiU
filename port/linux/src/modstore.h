/*
MODSTORE.H

The mod store (modstore.c): a catalog of mods at an HTTPS address the player's
config.toml names (mods.store_url), signed with an Ed25519 key, and the
download of a mod from it into the mods folder (port_mods.h). Nothing here
contacts a server until mods.store_url is set, and then only when the store
screen is opened.

Everything crosses this header as integers and strings, as port_mods.h does
(the game's side declares what it uses for itself: keep the two the same).
The work is done on a thread of its own; the screen asks for the state each
frame (modstore_generation moves when anything it shows has changed) and
never waits.
*/

#ifndef MODSTORE_H
#define MODSTORE_H

/* ---------- the store's state */

enum
{
	/* no store is named, or the build cannot reach one: nothing is ever fetched */
	_modstore_off,
	/* a store is named; its catalog has not been fetched */
	_modstore_idle,
	/* the catalog is being fetched and checked */
	_modstore_loading,
	/* the catalog is here: modstore_count mods */
	_modstore_ready,
	/* the catalog could not be fetched or is not the store's (modstore_message says why) */
	_modstore_failed,
};

/* a mod of the catalog, against the mods folder */
enum
{
	_modstore_mod_available,
	/* the version the catalog lists is installed */
	_modstore_mod_installed,
	/* installed, and the catalog lists another version */
	_modstore_mod_update,
	/* a folder of that name is in the mods folder and did not come from the store: never touched */
	_modstore_mod_local,
	/* this build cannot play it (made for a later mods format or build) */
	_modstore_mod_unplayable,
};

/* what a mod of the catalog says of itself (modstore_field) */
enum
{
	_modstore_field_id,
	_modstore_field_name,
	_modstore_field_author,
	_modstore_field_version,
	/* the version installed, or empty */
	_modstore_field_installed,
	_modstore_field_description,
	/* what its tag files are ("sounds, bitmaps") */
	_modstore_field_kind,
	_modstore_field_licence,
	/* why it cannot be installed (a state of local or unplayable), or empty */
	_modstore_field_why,
	_modstore_fields,
};

/* the job running, if any */
enum
{
	_modstore_job_none,
	_modstore_job_catalog,
	_modstore_job_install,
	_modstore_job_remove,
};

/* ---------- prototypes/MODSTORE.C */

/* whether a store and its key are set (the screen says so in words when not) */
int modstore_configured(void);
/* the store's state, and a line in words about it or about the last job (upper case ASCII) */
int modstore_phase(void);
int modstore_message(char *text, unsigned long size);
/* a counter that moves whenever the state, the list, a progress or the message has changed */
unsigned long modstore_generation(void);

/* opens the store: recovers what an install cut short left behind, and fetches the catalog (once; refresh asks again) */
void modstore_open(void);
void modstore_refresh(void);
/* leaves the store: whatever runs is asked to stop, and the catalog kept for the next opening */
void modstore_close(void);

/* the mods of the catalog (in its order), their states and what they say */
long modstore_count(void);
int modstore_state(long index);
unsigned long modstore_size(long index);
int modstore_field(long index, int field, char *text, unsigned long size);

/* the job: its kind, which mod (-1 if none), bytes done of total (of the whole mod) */
int modstore_job(void);
long modstore_job_index(void);
void modstore_progress(unsigned long *done, unsigned long *total);
/* asks the job to stop at the next piece (a half-made install is kept, to go on from) */
void modstore_cancel(void);

/* starts installing or updating a mod, or removing one the store installed: 1 if the job began, else 0 and the
reason in the message */
int modstore_install(long index);
int modstore_remove(long index);

/* 1 once since the mods folder last changed (an install, an update, a removal), for the caller to port_mods_scan */
int modstore_changed(void);

#endif
