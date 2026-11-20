/*
SAVE_BACKUP.H

A rolling backup of the files that hold a player's progress (the saved game and the profiles), so that a freeze or a pulled card during a write does not cost the
level of progress they held. Before such a file is written over, the last good copy of it is kept beside it as NAME.bak; if the file fails its checksum at load, the
backup is tried once. For the Wii U only (HALO_WIIU; the digest harness's builds too).

Switch (halo/<name> on the console, HALO_<NAME> in the environment of the host runs):
  save_backup.txt        1 or absent: on; 0: no backup is kept or used, as it was
*/

#ifndef __SAVE_BACKUP_H
#define __SAVE_BACKUP_H

#ifdef HALO_WIIU

/* the switch, read once */
int save_backup_enabled(void);

/* about to overwrite path: if it is a good copy (it is expected_size bytes if that is not 0, and good(head, got) agrees about its first head_bytes (at most 4096) bytes; with
no good(), any byte of them that is not 0 will do: the saved game's header is zeroed while its write is under way and written last), it is kept as path.bak, by a rename
(by_rename: the file is then to be made again by whoever writes it) or a copy. A file that is not good leaves a backup that is there alone. The old backup is removed only
when the file replacing it is good. */
void save_backup_keep(const char *path, int by_rename, unsigned long expected_size, unsigned long head_bytes,
	int (*good)(const unsigned char *head, unsigned long got), const char *what);

/* the first bytes of path.bak into buffer: 1 if it is there and holds bytes bytes (the caller checks them as it checks the file) */
int save_backup_read(const char *path, void *buffer, unsigned long bytes, const char *what);

/* a file that failed its checksum put away and its backup put in its place (path -> path.bad, path.bak -> path): 1 if that was done and the caller can read path again.
save_backup_swap_out(path, ok) then removes path.bad if the read was good, and if it was not puts everything back as it was. */
int save_backup_swap_in(const char *path, const char *what);
void save_backup_swap_out(const char *path, int ok, const char *what);

#endif

#endif
