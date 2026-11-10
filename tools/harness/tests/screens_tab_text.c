#include "harness.h"

/* The Friends screen's four tabs (port/linux/game/menu_screens.inl, scr_friends_tab_labels) on a widget tree made from the screen's own XML: a tab has no words of its own, so the words
the game sets in it are the only ones it shows. Every text widget of the list with a bitmap (a tab, a button) must show a non-empty text: its own (an XML text) or the one set in it. */
#include <wchar.h>
#include "text_safe.h"
#include "screens.h"

struct widget_instance
{
	char const *name;
	struct widget_instance *child, *next, *focused_child;
	int is_text, has_bitmap, has_static_text;
	union
	{
		struct
		{
			wchar_t *text;
		} text_box;
	} parameters;
};

#define SCR_LONG 96
#define NUMBEROF(array) ((int)(sizeof(array) / sizeof((array)[0])))

static long config_integer_value = 1;

long config_integer(const char *name)
{
	(void)name;
	return config_integer_value;
}

static void *ui_widget_realloc(void *pointer, size_t size, char const *file, int line)
{
	(void)file;
	(void)line;
	return realloc(pointer, size);
}

static wchar_t *ustrncpy(wchar_t *to, wchar_t const *from, size_t count)
{
	size_t index;

	for (index = 0; index < count && from[index]; index++)
		to[index] = from[index];
	for (; index < count; index++)
		to[index] = 0;
	return to;
}

#include "text_safe_code.inc"
#include "screens_code.inc"
#include "screens_mock_code.inc"
#include "menu_text_code.inc"
#include "tree.inc"

static void narrow(wchar_t const *wide, char *text, int size)
{
	int index;

	for (index = 0; wide && wide[index] && index < size - 1; index++)
		text[index] = (char)wide[index];
	text[index < size ? index : size - 1] = 0;
}

static struct widget_instance *find(struct widget_instance *widget, char const *name)
{
	struct widget_instance *child, *found;

	if (!strcmp(widget->name, name))
		return widget;
	for (child = widget->child; child; child = child->next)
	{
		found = find(child, name);
		if (found)
			return found;
	}
	return NULL;
}

/* every text widget with a bitmap under the widget: how many show nothing */
static int silent(struct widget_instance *widget, char *which, int size)
{
	struct widget_instance *child;
	int count = 0;

	if (widget->is_text && widget->has_bitmap && !widget->has_static_text && (!widget->parameters.text_box.text || !widget->parameters.text_box.text[0]))
	{
		count++;
		if (which[0] == 0)
			snprintf(which, (size_t)size, "%s", widget->name);
	}
	for (child = widget->child; child; child = child->next)
		count += silent(child, which, size);
	return count;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct screens_friends_summary summary;
	struct screens_party_summary party;
	int tab, index;
	char which[64] = "", text[SCR_LONG], expected[SCR_LONG];

	memset(&summary, 0, sizeof(summary));
	memset(&party, 0, sizeof(party));
	summary.counts[0] = 12;
	summary.counts[1] = 3;
	summary.counts[3] = 2;
	party.present = 1;
	party.size = 5;
	party.maximum = 8;
	CASE("before")
	{
		/* the tabs have no words until the game sets them: the check sees that (so it can fail) */
		CHECK(silent(&tree[0], which, sizeof(which)) == SCREENS_TABS, "the tabs show nothing before the labels: %d (%s)", silent(&tree[0], which, sizeof(which)), which);
	}
	else CASE("labels")
	{
		for (tab = 0; tab < SCREENS_TABS; tab++)
		{
			scr_friends_tab_labels(&tree[0], (short)tab, &summary, &party);
			which[0] = 0;
			CHECK(silent(&tree[0], which, sizeof(which)) == 0, "a button shows nothing with tab %d chosen: %s", tab, which);
			for (index = 0; index < SCREENS_TABS; index++)
			{
				char name[24];
				struct widget_instance *widget;

				snprintf(name, sizeof(name), "friends_tab_%d", index);
				widget = find(&tree[0], name);
				CHECK(widget, "no widget %s", name);
				narrow(widget->parameters.text_box.text, text, sizeof(text));
				screens_tab_label(index, index == tab, &summary, &party, expected, (int)sizeof(expected));
				CHECK(!strcmp(text, expected), "tab %d with tab %d chosen reads '%s', not '%s'", index, tab, text, expected);
			}
		}
	}
	else
		return 2;
	return 0;
}
