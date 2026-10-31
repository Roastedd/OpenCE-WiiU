/* schema_dump: the tag schemas the validator walks (port/linux/game/
tag_schema_*.c), as the build compiled them, written as JSON for the map
converter (map_convert.py) and the byte swap's tables (tag_swap_tables.py):
each group's root definition, and for each definition its size and its block,
data and structure fields with their offsets, counts, strides and child
definitions. The groups a Custom Edition map's tags are of where they are laid
out otherwise than this build's (tag_schema_custom_edition_groups: gbxmodels)
are listed apart, as "custom_edition_groups", their definitions after all the
others, so that the definitions of the Xbox maps' groups keep their places.

Built against the big-endian build's schema objects (tools/ppc-be/run.sh
build, then in the opence-ppc-be container at the OpenCE root) and run under
qemu-ppc; its symbols name each definition's variable (map_convert.py reads
them):

	clang --target=powerpc-linux-gnu -fsigned-char -O1 -g -c tools/wiiu/schema_dump.c -o build/linux-ppc/tools/schema_dump.o
	clang --target=powerpc-linux-gnu -fuse-ld=lld -no-pie -Wl,--unresolved-symbols=ignore-all \
		build/linux-ppc/tools/schema_dump.o build/linux-ppc/obj/port/linux/game/tag_schema_*.o -o build/linux-ppc/tools/schema_dump
	qemu-ppc build/linux-ppc/tools/schema_dump | gzip -9n > tools/wiiu/map_schema/tag_schema.json.gz
	llvm-nm build/linux-ppc/tools/schema_dump | gzip -9n > tools/wiiu/map_schema/schema_dump.nm.gz

The schema objects name the validator's checks and the game's functions they
call; schema_dump never calls them, so those references are left unresolved,
and the one datum is a stub. */

#include <stdio.h>
#include <string.h>

/* (the few declarations of port/linux/game/tag_schema.h this needs, without
the game's headers) */
enum
{
	_tag_schema_block,
	_tag_schema_data,
	_tag_schema_file_data,
	_tag_schema_reference,
	_tag_schema_tag_index,
	_tag_schema_block_index,
	_tag_schema_enum,
	_tag_schema_string,
	_tag_schema_reset,
	_tag_schema_struct,
	_tag_schema_check,
	_tag_schema_terminator,
};

struct tag_schema_field
{
	short type;
	short size;
	short count;
	short flags;
	long offset;
	long maximum;
	long target_level;
	long target_offset;
	void const *definition;
	void *check;
	char const *name;
};

struct tag_schema_definition
{
	char const *name;
	long size;
	struct tag_schema_field const *fields;
};

struct tag_schema_group
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	struct tag_schema_definition const *definition;
};

extern struct tag_schema_group const *const tag_schema_group_lists[];
extern struct tag_schema_group const tag_schema_custom_edition_groups[];

enum { MAXIMUM_DEFINITIONS = 2048 };

static struct tag_schema_definition const *seen[MAXIMUM_DEFINITIONS];
static int seen_count;

static int definition_index(struct tag_schema_definition const *definition)
{
	int index;

	for (index = 0; index < seen_count; index++)
	{
		if (seen[index] == definition)
			return index;
	}
	if (seen_count == MAXIMUM_DEFINITIONS)
	{
		fprintf(stderr, "schema_dump: more than %d definitions\n", MAXIMUM_DEFINITIONS);
		return -1;
	}
	seen[seen_count] = definition;
	return seen_count++;
}

static void group_tag_text(unsigned long tag, char text[5])
{
	text[0] = (char)(tag >> 24);
	text[1] = (char)(tag >> 16);
	text[2] = (char)(tag >> 8);
	text[3] = (char)tag;
	text[4] = 0;
}

/* the groups of a list (ending with a 0 group tag), each with its root's
index; first: whether nothing has been written in the JSON list yet */
static void groups_write(struct tag_schema_group const *group, int *first)
{
	for (; group->group_tag; group++)
	{
		char text[5];

		group_tag_text(group->group_tag, text);
		printf("%s\n {\"tag\": \"%s\", \"definition\": %d}", *first ? "" : ",", text,
			group->definition ? definition_index(group->definition) : -1);
		*first = 0;
	}
}

/* the definitions seen from *written on, and those found while writing them,
which are appended to seen and written in turn */
static void definitions_write(int *written)
{
	for (; *written < seen_count; (*written)++)
	{
		struct tag_schema_definition const *definition = seen[*written];
		struct tag_schema_field const *field;
		int first_field = 1;

		/* (its address names its variable, by the executable's symbols: the
		names in definitions are not unique) */
		printf("%s\n {\"index\": %d, \"name\": \"%s\", \"address\": %lu, \"size\": %ld, \"fields\": [",
			*written ? "," : "", *written, definition->name, (unsigned long)definition, definition->size);
		for (field = definition->fields; field->type != _tag_schema_terminator; field++)
		{
			char const *kind;
			int child = -1;

			switch (field->type)
			{
			case _tag_schema_block: kind = "block"; child = definition_index(field->definition); break;
			case _tag_schema_data: kind = "data"; break;
			case _tag_schema_file_data: kind = "file_data"; break;
			case _tag_schema_struct: kind = "struct"; child = definition_index(field->definition); break;
			default: continue;
			}
			printf("%s{\"kind\": \"%s\", \"name\": \"%s\", \"offset\": %ld, \"size\": %d, \"count\": %d, "
				"\"child\": %d}", first_field ? "" : ", ", kind, field->name ? field->name : "", field->offset,
				field->size, field->count, child);
			first_field = 0;
		}
		printf("]}");
	}
}

int main(void)
{
	struct tag_schema_group const *const *list;
	int first = 1;
	int written = 0;
	int first_custom_edition_definition;

	printf("{\"groups\": [");
	for (list = tag_schema_group_lists; *list; list++)
		groups_write(*list, &first);
	printf("\n],\n\"definitions\": [");
	definitions_write(&written);
	/* (the Custom Edition groups' roots, and what they reach that the others
	did not, after every other definition) */
	first_custom_edition_definition = seen_count;
	first = 1;
	{
		struct tag_schema_group const *group;

		for (group = tag_schema_custom_edition_groups; group->group_tag; group++)
		{
			if (group->definition)
				(void)definition_index(group->definition);
		}
	}
	definitions_write(&written);
	printf("\n],\n\"custom_edition_groups\": [");
	groups_write(tag_schema_custom_edition_groups, &first);
	printf("\n],\n\"first_custom_edition_definition\": %d}\n", first_custom_edition_definition);

	return 0;
}
