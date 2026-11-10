/*
MENU_STORE.INL (included by menu_functions.c, after menu_screens.inl, whose helpers it uses)

The mod store's screen (tools/port_settings.py's _mod_store, in main_menu.mods.xml): the Mods page's STORE opens a list of the
mods of the catalog the player's config.toml names (port/linux/src/modstore.c does the fetching, the checking and the installing
on a thread of its own; this only asks it for the state each frame and starts its jobs). The rows are the catalog's mods (name,
author, size and state), the line under them the description of the row chosen, or what the last job said; A on a row downloads
or updates the mod, X removes a mod the store installed, Y asks for the catalog again, B goes back (which stops a download in
the middle, kept to go on from).

It does not depend on the social screens being on (screens.enable): it uses their helpers and their words' fit only.
*/

/* modstore.c (src/modstore.h: keep the two the same) */
int modstore_configured(void);
int modstore_phase(void);
int modstore_message(char *text, unsigned long size);
void modstore_open(void);
void modstore_refresh(void);
void modstore_close(void);
long modstore_count(void);
int modstore_state(long index);
unsigned long modstore_size(long index);
int modstore_field(long index, int field, char *text, unsigned long size);
int modstore_job(void);
long modstore_job_index(void);
void modstore_progress(unsigned long *done, unsigned long *total);
void modstore_cancel(void);
int modstore_install(long index);
int modstore_remove(long index);
int modstore_changed(void);
long port_mods_scan(void);

/* (modstore.h's, the ones used here) */
enum
{
	_store_phase_off,
	_store_phase_idle,
	_store_phase_loading,
	_store_phase_ready,
	_store_phase_failed,
};

enum
{
	_store_mod_available,
	_store_mod_installed,
	_store_mod_update,
	_store_mod_local,
	_store_mod_unplayable,
};

enum
{
	_store_field_name = 1,
	_store_field_author,
	_store_field_version,
	_store_field_installed,
	_store_field_description,
	_store_field_kind,
	_store_field_licence,
	_store_field_why,
};

enum
{
	_store_job_none,
	_store_job_catalog,
	_store_job_install,
	_store_job_remove,
};

#define STORE_PREFIX "store_item_"
#define STORE_ROWS 9
#define STORE_CELLS SCREENS_STORE_CELLS

/* (the cells' widths, their inset and the words of the rows are screens.h's SCREENS_STORE_* and screens.c's screens_store_*: the preview draws the same functions) */
enum
{
	_store_cell_name,
	_store_cell_author,
	_store_cell_size,
	_store_cell_status,
};

static struct
{
	/* the catalog's mod at the top row, and the mod chosen last (the rows' focus, kept while a button has it) */
	short first;
	long chosen;
	boolean fresh;
	/* the job seen last frame, the mod chosen when it ended, and whether the line under the rows says what the job (or the press) said */
	int last_job;
	long last_item;
	boolean show_note;
	/* what the screen itself said (a press that could not do anything), else the store's message is said */
	char note[SCR_LONG];
	/* the shade of each cell (the SCREENS_COLOR_ of it) */
	unsigned char colors[STORE_ROWS][STORE_CELLS];
	int last_phase;
} store_ui;

/* what the store changed in the mods folder (a mod put in place or taken away, perhaps after the screen had gone): the mods found again and
played, as the Mods page's OK does, and the Mods page's rows made again from them */
static void store_sync(void)
{
	short row;

	if (!modstore_changed())
		return;
	mods_apply();
	mods_screen.shown[0] = 0;
	for (row = 0; row < MOD_ROWS; row++)
		mods_screen.row_mods[row] = NONE;
}

/* the percent of the job's bytes done, or -1 */
static int store_percent(void)
{
	unsigned long done, total;

	modstore_progress(&done, &total);
	return total ? (int)MIN(100UL, done * 100 / total) : -1;
}

/* a mod's row: its cells' texts (cut to their columns), and their shades */
static void store_cells(long index, char cells[STORE_CELLS][SCR_LONG], unsigned char *colors)
{
	struct screens_cell made[SCREENS_STORE_CELLS];
	char name[SCR_LONG], author[SCR_LONG];
	int working = 0, cell;

	if (modstore_job() != _store_job_none && modstore_job_index() == index)
		working = modstore_job() == _store_job_remove ? 2 : 1;
	modstore_field(index, _store_field_name, name, sizeof(name));
	modstore_field(index, _store_field_author, author, sizeof(author));
	screens_store_cells(name, author, modstore_size(index), modstore_state(index), working, store_percent(), made);
	for (cell = 0; cell < STORE_CELLS; cell++)
	{
		snprintf(cells[cell], SCR_LONG, "%s", made[cell].text);
		colors[cell] = made[cell].color;
	}
}

/* a cell's shade (pc_menu_text_color) */
static boolean store_text_color(struct widget_instance *widget, real_argb_color *color)
{
	static char const *const cells[STORE_CELLS] = { "name", "author", "size", "status" };
	short row;
	int index;
	unsigned long rgb;

	if (!widget->name || strncmp(widget->name, "store_cell_", 11) || !widget->parent)
		return FALSE;
	row = scr_row_index(widget->parent, STORE_PREFIX);
	for (index = 0; index < STORE_CELLS; index++)
	{
		if (!strcmp(widget->name + 11, cells[index]))
			break;
	}
	if (row < 0 || row >= STORE_ROWS || index >= STORE_CELLS)
		return FALSE;
	rgb = screens_color_rgb(store_ui.colors[row][index]);
	color->red = (real)((rgb >> 16) & 0xFF) / 255.0f;
	color->green = (real)((rgb >> 8) & 0xFF) / 255.0f;
	color->blue = (real)(rgb & 0xFF) / 255.0f;
	return TRUE;
}

/* "port store update" (the list's data) */
static void store_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list), *row;
	int phase = modstore_phase(), job = modstore_job(), state = NONE;
	long count = phase == _store_phase_ready ? modstore_count() : 0, chosen;
	boolean busy, ready = phase == _store_phase_ready;
	short shown, item, index;
	int buttons;
	char text[SCR_LONG * 2], detail[SCR_LONG * 2], footer[SCR_LONG], store_said[SCR_LONG];

	store_sync();
	item = scr_scroll(list, STORE_PREFIX, &store_ui.first, (short)MIN(count, 30000), STORE_ROWS);
	if (store_ui.first > MAX(0, count - STORE_ROWS))
		store_ui.first = (short)MAX(0, count - STORE_ROWS);
	shown = (short)MIN(count, STORE_ROWS);
	if (store_ui.fresh)
	{
		store_ui.fresh = FALSE;
		scr_focus(list, STORE_PREFIX, shown > 0 ? 0 : NONE);
	}
	for (row = list->child; row; row = row->next)
	{
		char cells[STORE_CELLS][SCR_LONG];

		index = scr_row_index(row, STORE_PREFIX);
		if (index == NONE)
			continue;
		row->visible = index < shown;
		if (index >= shown)
			continue;
		store_cells(store_ui.first + index, cells, store_ui.colors[index]);
		scr_text(row, "store_cell_name", cells[_store_cell_name]);
		scr_text(row, "store_cell_author", cells[_store_cell_author]);
		scr_text(row, "store_cell_size", cells[_store_cell_size]);
		scr_text(row, "store_cell_status", cells[_store_cell_status]);
	}
	scr_focus_clamp(list, STORE_PREFIX, shown);
	item = scr_focused(list, STORE_PREFIX);
	if (item != NONE && store_ui.first + item < count)
		store_ui.chosen = store_ui.first + item;
	chosen = ready && store_ui.chosen >= 0 && store_ui.chosen < count ? store_ui.chosen : NONE;
	if (chosen != NONE)
		state = modstore_state(chosen);
	busy = job == _store_job_install || job == _store_job_remove;
	/* (what the line under the rows says: the store's message while a job runs and after it, until another mod is chosen) */
	if (job != store_ui.last_job)
	{
		store_ui.last_job = job;
		store_ui.last_item = chosen;
		if (job == _store_job_none || busy)
			store_ui.show_note = TRUE;
	}
	if (!busy && store_ui.show_note && chosen != store_ui.last_item)
	{
		store_ui.show_note = FALSE;
		store_ui.note[0] = 0;
	}
	scr_text(screen, "store_title", "MOD STORE");
	if (ready)
		snprintf(text, sizeof(text), "%ld MOD%s", count, count == 1 ? "" : "S");
	else
		text[0] = 0;
	scr_text(screen, "store_head", text);
	/* (the footer's counter "4 OF 14" and the arrows at the list's edge, as the Players and Friends pages have them; this clears the empty words, set here if there is no list) */
	scr_list_hints(screen, "store", item, store_ui.first, shown, (short)MIN(count, 30000));
	store_said[0] = 0;
	if (phase == _store_phase_failed)
		modstore_message(store_said, sizeof(store_said));
	if (screens_store_empty(phase, count, store_said, text, (int)sizeof(text)))
		scr_text(screen, "store_empty", text);
	detail[0] = 0;
	if (ready && chosen != NONE)
	{
		char author[SCR_LONG], kind[SCR_LONG], licence[SCR_LONG], version[SCR_LONG], held[SCR_LONG];

		if (store_ui.show_note && (store_ui.note[0] || busy || job == _store_job_none))
		{
			if (store_ui.note[0])
				snprintf(detail, sizeof(detail), "%s", store_ui.note);
			else
				modstore_message(detail, sizeof(detail));
			if (busy && job == _store_job_install && store_percent() >= 0)
			{
				char said[SCR_LONG * 2];

				snprintf(said, sizeof(said), "%s", detail);
				screens_store_job_line(said, store_percent(), detail, (int)sizeof(detail));
			}
		}
		else if (state == _store_mod_local || state == _store_mod_unplayable)
			modstore_field(chosen, _store_field_why, detail, sizeof(detail));
		else
			modstore_field(chosen, _store_field_description, detail, sizeof(detail));
		scr_sentence(detail, (int)sizeof(detail));
		/* (the footer: who made it, what it changes, its licence and version) */
		modstore_field(chosen, _store_field_author, author, sizeof(author));
		modstore_field(chosen, _store_field_kind, kind, sizeof(kind));
		modstore_field(chosen, _store_field_licence, licence, sizeof(licence));
		modstore_field(chosen, _store_field_version, version, sizeof(version));
		modstore_field(chosen, _store_field_installed, held, sizeof(held));
		screens_store_footer(author, kind, licence, version, held, state, footer, (int)sizeof(footer));
		scr_text(screen, "store_footer", footer);
	}
	else
	{
		/* (no mod chosen: the job's message alone, if there is one) */
		if (ready && store_ui.show_note && (busy || store_ui.note[0]))
		{
			if (store_ui.note[0])
				snprintf(detail, sizeof(detail), "%s", store_ui.note);
			else
				modstore_message(detail, sizeof(detail));
			scr_sentence(detail, (int)sizeof(detail));
		}
		scr_text(screen, "store_footer", "");
	}
	scr_text(screen, "store_detail", detail);
	/* the buttons that apply */
	buttons = screens_store_buttons(phase, busy, state);
	visible_set(named(list, "store_button_get", 0), (buttons & SCREENS_STORE_BUTTON_GET) != 0);
	visible_set(named(list, "store_button_update", 0), (buttons & SCREENS_STORE_BUTTON_UPDATE) != 0);
	visible_set(named(list, "store_button_stop", 0), (buttons & SCREENS_STORE_BUTTON_STOP) != 0);
	visible_set(named(list, "store_button_remove", 0), (buttons & SCREENS_STORE_BUTTON_REMOVE) != 0);
	visible_set(named(list, "store_button_refresh", 0), (buttons & SCREENS_STORE_BUTTON_REFRESH) != 0);
	focus_off_hidden(named(list, "store_button_bar", 0));
	if (phase != store_ui.last_phase)
	{
		store_ui.last_phase = phase;
		platform_log("mod store: screen: phase %d, %ld mods", phase, count);
	}
}

/* the screen's created: the catalog asked for (the first time, or again if it failed) */
static boolean store_open(void)
{
	store_ui.first = 0;
	store_ui.chosen = 0;
	store_ui.fresh = TRUE;
	store_ui.last_job = _store_job_none;
	store_ui.last_item = NONE;
	store_ui.show_note = FALSE;
	store_ui.note[0] = 0;
	store_ui.last_phase = -1;
	modstore_open();
	return TRUE;
}

/* a mod chosen (the row's, or the last chosen when a button is pressed): put on the line under the rows what a press that cannot do anything is told */
static boolean store_refuse(char const *words)
{
	snprintf(store_ui.note, sizeof(store_ui.note), "%s", words);
	store_ui.show_note = TRUE;
	store_ui.last_item = store_ui.chosen;
	return campaign_fail();
}

static boolean store_get(long index)
{
	int state;

	if (modstore_phase() != _store_phase_ready || index < 0 || index >= modstore_count())
		return campaign_fail();
	if (modstore_job() != _store_job_none)
		return store_refuse("WAIT FOR THE LAST JOB TO END, OR PRESS STOP.");
	state = modstore_state(index);
	if (state == _store_mod_installed)
		return store_refuse("THAT VERSION IS ALREADY INSTALLED. PRESS X TO REMOVE IT.");
	if (state == _store_mod_local || state == _store_mod_unplayable)
	{
		char why[SCR_LONG];

		modstore_field(index, _store_field_why, why, sizeof(why));
		return store_refuse(why);
	}
	store_ui.note[0] = 0;
	store_ui.show_note = TRUE;
	store_ui.last_item = index;
	return modstore_install(index) ? TRUE : campaign_fail();
}

static boolean store_remove(void)
{
	long index = store_ui.chosen;
	int state;

	if (modstore_phase() != _store_phase_ready || index < 0 || index >= modstore_count())
		return campaign_fail();
	state = modstore_state(index);
	if (modstore_job() != _store_job_none)
		return store_refuse("WAIT FOR THE LAST JOB TO END, OR PRESS STOP.");
	if (state != _store_mod_installed && state != _store_mod_update)
		return store_refuse(state == _store_mod_local ? "THAT MOD IS YOURS, NOT THE STORE'S: DELETE ITS FOLDER YOURSELF." : "THAT MOD IS NOT INSTALLED.");
	store_ui.note[0] = 0;
	store_ui.show_note = TRUE;
	store_ui.last_item = index;
	return modstore_remove(index) ? TRUE : campaign_fail();
}

/* the store's functions (menu_functions.c's event dispatch); *mine says the name was one of them */
static boolean store_event(char const *name, struct widget_instance *widget, boolean *mine)
{
	*mine = TRUE;
	if (!strcmp(name, "port mods store"))
	{
		/* (the Mods page's order and what is on, kept as they stand: the store finds the mods again when it changes the folder) */
		port_mods_save();
		return TRUE;
	}
	if (!strcmp(name, "port store open"))
		return store_open();
	if (!strcmp(name, "port store close"))
	{
		modstore_close();
		return TRUE;
	}
	if (!strcmp(name, "port store row"))
	{
		short index = scr_row_index(widget, STORE_PREFIX);

		return index >= 0 ? store_get(store_ui.first + index) : campaign_fail();
	}
	if (!strcmp(name, "port store get"))
		return store_get(store_ui.chosen);
	if (!strcmp(name, "port store remove"))
		return store_remove();
	if (!strcmp(name, "port store stop"))
	{
		modstore_cancel();
		snprintf(store_ui.note, sizeof(store_ui.note), "STOPPING AFTER THIS PART. WHAT IS DOWNLOADED IS KEPT.");
		store_ui.show_note = TRUE;
		return TRUE;
	}
	if (!strcmp(name, "port store refresh"))
	{
		if (modstore_phase() == _store_phase_off || modstore_phase() == _store_phase_loading || modstore_job() != _store_job_none)
			return campaign_fail();
		store_ui.first = 0;
		store_ui.chosen = 0;
		store_ui.fresh = TRUE;
		store_ui.show_note = FALSE;
		store_ui.note[0] = 0;
		modstore_refresh();
		return TRUE;
	}
	*mine = FALSE;
	return FALSE;
}

static boolean store_data(char const *name, struct widget_instance *widget)
{
	if (strcmp(name, "port store update"))
		return FALSE;
	store_update(widget);
	return TRUE;
}
