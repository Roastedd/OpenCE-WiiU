/*
SOCIAL_FRONT.C

The queue, the snapshot and the saver of the friends layer (social_front.h).
*/

#include "../src/p2p_internal.h"
#include "social_front.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	COMMAND_QUEUE = 32,
	EVENT_QUEUE = 32,
};

enum
{
	_command_add,
	_command_accept,
	_command_ignore,
	_command_cancel,
	_command_remove,
	_command_block,
	_command_unblock,
	_command_nickname,
	_command_name,
	_command_share,
	_command_enable,
	_command_place,
	_command_invite,
	_command_invite_dismiss,
	_command_party,
	_command_party_token,
	_command_follow_report,
	_command_share_part,
};

struct command
{
	int type;
	unsigned int id;
	int a, b, c;
	unsigned char token[SOCIAL_TOKEN_SIZE];
	char text[80];
	struct social_place place;
};

struct social_front
{
	struct social_engine engine;
	struct social_files files;
	int has_files;
	struct social_front_options options;
	/* the lock covers the queue, the snapshot, the events and the hand-off to the saver, and nothing else */
	pthread_mutex_t lock;
	struct command commands[COMMAND_QUEUE];
	int command_first, command_count;
	struct social_snapshot snapshot;
	struct social_snapshot scratch;
	unsigned int snapshot_version;
	unsigned int snapshot_engine_version;
	unsigned int snapshot_time;
	int snapshot_made;
	struct social_api_event events[EVENT_QUEUE];
	int event_first, event_count;
	/* the saver */
	pthread_t thread;
	pthread_cond_t wake;
	int thread_running;
	int stop;
	int save_pending, save_busy;
	char *save_text;
	int save_size;
	int save_result;
	unsigned int saves, failures;
	int key_written;
	/* the leader's game to follow, as of the last pass */
	int follow_want;
	struct social_join_target follow_target;
	unsigned int follow_order;
};

struct social_engine *social_front_engine(struct social_front *front)
{
	return &front->engine;
}

/* ---------- the saver */

static void *saver_thread(void *argument)
{
	struct social_front *front = argument;

	pthread_mutex_lock(&front->lock);
	for (;;)
	{
		int result;

		while (!front->save_pending && !front->stop)
			pthread_cond_wait(&front->wake, &front->lock);
		if (!front->save_pending)
			break;
		front->save_pending = 0;
		front->save_busy = 1;
		pthread_mutex_unlock(&front->lock);
		/* (the card: as long as it takes, on a thread nothing else waits for) */
		result = social_store_save(&front->files, front->save_text, front->save_size);
		pthread_mutex_lock(&front->lock);
		front->save_busy = 0;
		front->save_result = result;
		if (result)
			front->failures++;
		else
			front->saves++;
	}
	pthread_mutex_unlock(&front->lock);
	return NULL;
}

struct social_front *social_front_create(const struct social_env *env, const struct social_files *files, const struct social_front_options *options)
{
	struct social_front *front = calloc(1, sizeof(*front));

	if (!front)
		return NULL;
	social_engine_init(&front->engine, env);
	pthread_mutex_init(&front->lock, NULL);
	pthread_cond_init(&front->wake, NULL);
	if (options)
		front->options = *options;
	if (files)
	{
		front->files = *files;
		front->has_files = 1;
		front->save_text = malloc(SOCIAL_STORE_MAXIMUM);
		if (!front->save_text)
		{
			free(front);
			return NULL;
		}
		if (front->options.saver_thread && pthread_create(&front->thread, NULL, saver_thread, front) == 0)
			front->thread_running = 1;
	}
	return front;
}

void social_front_destroy(struct social_front *front)
{
	if (!front)
		return;
	pthread_mutex_lock(&front->lock);
	front->stop = 1;
	pthread_cond_signal(&front->wake);
	pthread_mutex_unlock(&front->lock);
	if (front->thread_running)
		pthread_join(front->thread, NULL);
	pthread_mutex_destroy(&front->lock);
	pthread_cond_destroy(&front->wake);
	free(front->save_text);
	free(front);
}

void social_front_saver(struct social_front *front, unsigned int *saves, unsigned int *failures)
{
	pthread_mutex_lock(&front->lock);
	*saves = front->saves;
	*failures = front->failures;
	pthread_mutex_unlock(&front->lock);
}

/* hands the engine's text to the saver if a save is due (the engine's thread) */
static void save_update(struct social_front *front)
{
	int size, foreground;

	if (!front->has_files)
		return;
	pthread_mutex_lock(&front->lock);
	if (front->save_pending || front->save_busy)
	{
		pthread_mutex_unlock(&front->lock);
		return;
	}
	/* (a failed save is tried again: the change is marked afresh) */
	if (front->save_result)
	{
		front->save_result = 0;
		front->engine.store_dirty = 1;
		front->engine.store_dirty_time = social_now(&front->engine);
		front->engine.store_saved_time = social_now(&front->engine);
	}
	pthread_mutex_unlock(&front->lock);
	foreground = front->options.foreground ? front->options.foreground(front->options.user) : 1;
	if (!social_store_due(&front->engine, foreground))
		return;
	size = social_store_take(&front->engine, front->save_text, SOCIAL_STORE_MAXIMUM);
	if (size <= 0)
		return;
	pthread_mutex_lock(&front->lock);
	front->save_size = size;
	front->save_pending = 1;
	pthread_cond_signal(&front->wake);
	pthread_mutex_unlock(&front->lock);
	if (!front->thread_running)
		social_front_save_now(front);
}

int social_front_save_now(struct social_front *front)
{
	int result;

	if (!front->has_files)
		return 0;
	pthread_mutex_lock(&front->lock);
	if (!front->save_pending || front->thread_running)
	{
		pthread_mutex_unlock(&front->lock);
		return 0;
	}
	front->save_pending = 0;
	pthread_mutex_unlock(&front->lock);
	result = social_store_save(&front->files, front->save_text, front->save_size);
	pthread_mutex_lock(&front->lock);
	front->save_result = result;
	if (result)
		front->failures++;
	else
		front->saves++;
	pthread_mutex_unlock(&front->lock);
	return result;
}

/* ---------- starting */

int social_front_start(struct social_front *front, const char *name, const unsigned char *fresh_seed)
{
	unsigned char seed[SOCIAL_SEED_SIZE];
	char text[512];
	int size, written = 1;

	if (!front->has_files)
	{
		social_set_identity(&front->engine, fresh_seed, name, 0);
		return 0;
	}
	size = front->files.read_file(front->files.user, "friends.key", text, (int)sizeof(text) - 1);
	if (size >= 0 && social_key_parse(text, size, seed))
	{
		written = 0;
	}
	else
	{
		/* a new identity: written, and read back before it is used */
		memcpy(seed, fresh_seed, SOCIAL_SEED_SIZE);
		size = social_key_format(seed, text, (int)sizeof(text));
		if (size > 0 && !front->files.write_file(front->files.user, "friends.key", text, size))
		{
			unsigned char again[SOCIAL_SEED_SIZE];
			int read_size = front->files.read_file(front->files.user, "friends.key", text, (int)sizeof(text) - 1);

			written = !(read_size > 0 && social_key_parse(text, read_size, again) && !memcmp(again, seed, SOCIAL_SEED_SIZE));
		}
	}
	front->key_written = !written;
	social_set_identity(&front->engine, seed, name, 0);
	if (social_store_load(&front->files, &front->engine) == 0)
		social_log(&front->engine, "friends file read, %d friends", social_friend_count(&front->engine));
	return written;
}

/* ---------- the snapshot */

static void word(char *destination, int size, const char *source)
{
	snprintf(destination, (size_t)size, "%s", source);
}

static int friend_order(const void *first, const void *second)
{
	const struct social_friend_info *a = first, *b = second;
	const char *name_a = a->nickname[0] ? a->nickname : a->name, *name_b = b->nickname[0] ? b->nickname : b->name;

	if (a->online != b->online)
		return b->online - a->online;
	return strcmp(name_a, name_b);
}

static void snapshot_make(struct social_front *front, struct social_snapshot *snapshot)
{
	struct social_engine *engine = &front->engine;
	int index, count = 0;

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->enabled = engine->enabled;
	snapshot->brokers = engine->enabled ? engine->env.ready(engine->env.user) : 0;
	snapshot->share_status = engine->share_status;
	snapshot->share_details = engine->details;
	snapshot->accept_requests = engine->accept_requests;
	if (engine->has_identity)
		word(snapshot->code, sizeof(snapshot->code), engine->identity.code);
	word(snapshot->name, sizeof(snapshot->name), engine->name);
	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		const struct social_friend *friend = &engine->friends[index];
		struct social_friend_info *info = &snapshot->friends[count];
		const struct social_presence *presence = &friend->presence;
		int online;

		if (!friend->used)
			continue;
		online = social_friend_online(engine, friend);
		memset(info, 0, sizeof(*info));
		info->id = friend->id;
		word(info->name, sizeof(info->name), presence->name[0] ? presence->name : friend->code);
		word(info->nickname, sizeof(info->nickname), friend->nickname);
		word(info->code, sizeof(info->code), friend->code);
		info->online = online;
		info->keys_exchanged = friend->has_circle && friend->acknowledged;
		if (friend->heard)
		{
			unsigned int age = (social_now(engine) - friend->heard_time) / 1000;

			info->seconds_since_heard = age ? age : 1;
		}
		if (online)
		{
			info->activity = presence->activity;
			info->platform = presence->platform;
			info->network_version = presence->network_version;
			info->hosting = !!(presence->flags & SOCIAL_PRESENCE_HOSTING);
			info->joinable = !!(presence->flags & SOCIAL_PRESENCE_JOINABLE);
			info->private_game = !!(presence->flags & SOCIAL_PRESENCE_PRIVATE);
			info->in_party = !!(presence->flags & SOCIAL_PRESENCE_IN_PARTY);
			info->party_open = !!(presence->flags & SOCIAL_PRESENCE_PARTY_OPEN);
			memcpy(info->party_token, presence->party_token, SOCIAL_TOKEN_SIZE);
			word(info->map, sizeof(info->map), presence->map);
			word(info->gametype, sizeof(info->gametype), presence->gametype);
			info->players = presence->players;
			info->maximum_players = presence->maximum_players;
			info->engine_type = presence->engine_type;
			if (info->joinable)
			{
				memcpy(info->join.host_hash, presence->invite_hash, SOCIAL_HASH_SIZE);
				memcpy(info->join.token, presence->invite_token, SOCIAL_TOKEN_SIZE);
				info->join.network_version = presence->network_version;
				info->join.players = presence->players;
				info->join.maximum_players = presence->maximum_players;
				info->join.engine_type = presence->engine_type;
				word(info->join.map, sizeof(info->join.map), presence->map);
				word(info->join.gametype, sizeof(info->join.gametype), presence->gametype);
				word(info->join.name, sizeof(info->join.name), info->name);
				info->join.friend_id = friend->id;
			}
		}
		count++;
	}
	snapshot->friend_count = count;
	qsort(snapshot->friends, (size_t)count, sizeof(snapshot->friends[0]), friend_order);
	count = 0;
	for (index = 0; index < SOCIAL_MAX_REQUESTS; index++)
	{
		const struct social_request *request = &engine->requests[index];

		if (!request->used)
			continue;
		snapshot->requests[count].id = request->id;
		word(snapshot->requests[count].name, SOCIAL_NAME_SIZE + 1, request->name);
		word(snapshot->requests[count].code, SOCIAL_CODE_TEXT_SIZE, request->code);
		snapshot->requests[count].seconds_ago = (social_now(engine) - request->received) / 1000;
		count++;
	}
	snapshot->request_count = count;
	count = 0;
	for (index = 0; index < SOCIAL_MAX_SENT; index++)
	{
		const struct social_sent *sent = &engine->sent[index];

		if (!sent->used)
			continue;
		snapshot->sent[count].id = sent->id;
		word(snapshot->sent[count].code, SOCIAL_CODE_TEXT_SIZE, sent->code);
		snapshot->sent[count].seconds_ago = sent->sent_valid ? (social_now(engine) - sent->sent_time) / 1000 : 0;
		count++;
	}
	snapshot->sent_count = count;
	snapshot->blocked_count = engine->blocked_count;
	count = 0;
	for (index = 0; index < SOCIAL_MAX_INVITES_TOTAL; index++)
	{
		const struct social_pending_invite *held = &engine->invites[index];
		const struct social_friend *from = social_friend_by_id(engine, held->friend_id);
		struct social_invite_info *info = &snapshot->invites[count];

		if (!held->used)
			continue;
		memset(info, 0, sizeof(*info));
		info->id = held->id;
		info->friend_id = held->friend_id;
		word(info->name, sizeof(info->name), held->target.name);
		info->kind = held->kind;
		info->target = held->target;
		info->party_size = held->party_size;
		memcpy(info->party_token, held->party_token, SOCIAL_TOKEN_SIZE);
		info->seconds_ago = (social_now(engine) - held->received) / 1000;
		(void)from;
		count++;
	}
	snapshot->invite_count = count;
	{
		const struct social_party *party = &engine->party;
		struct social_party_info *out = &snapshot->party;
		int member_order[SOCIAL_MAX_PARTY_MEMBERS], ordered = 0, slot;

		out->active = party->active;
		out->joining = party->joining;
		out->is_leader = party->active && !party->joining && party->leader == party->self;
		out->open = party->open;
		out->auto_follow = party->auto_follow;
		out->game_present = party->game_present;
		out->game = party->game;
		out->game_order = party->game_order;
		out->serial = engine->party_serial;
		out->result = engine->party_result;
		if (party->active)
		{
			/* the leader first, then the list's order, then the rest */
			if (party->leader >= 0)
				member_order[ordered++] = party->leader;
			for (slot = 0; slot < party->roster_count; slot++)
			{
				for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
				{
					if (party->members[index].used && index != party->leader && !memcmp(party->members[index].hash, party->roster[slot], SOCIAL_BLOCK_SIZE))
						member_order[ordered++] = index;
				}
			}
			for (index = 0; index < SOCIAL_MAX_PARTY_MEMBERS; index++)
			{
				int listed = 0;

				for (slot = 0; slot < ordered; slot++)
					listed |= member_order[slot] == index;
				if (party->members[index].used && !listed)
					member_order[ordered++] = index;
			}
			for (slot = 0; slot < ordered; slot++)
			{
				const struct social_member *member = &party->members[member_order[slot]];
				struct social_party_member_info *info = &out->members[slot];

				info->id = member->id;
				word(info->name, sizeof(info->name), member->name);
				info->leader = member_order[slot] == party->leader;
				info->self = member_order[slot] == party->self;
				info->online = info->self || (member->heard && !social_elapsed(engine, member->heard_time, SOCIAL_PARTY_LEADER_SILENCE));
				info->auto_follow = info->self ? party->auto_follow : !!(member->flags & 2);
				info->hosting = info->self ? engine->place.hosting : !!(member->flags & 4);
				info->in_game = info->self ? engine->place.activity == SOCIAL_GAME : !!(member->flags & 8);
				info->follow_result = member->follow_result;
				word(info->follow_text, sizeof(info->follow_text), social_follow_text(member->follow_result));
			}
			out->member_count = ordered;
			for (slot = 0; slot < party->chat_count; slot++)
			{
				const struct social_chat_line *line = &party->chat[(party->chat_first + slot) % 8];

				word(out->chat[slot].name, sizeof(out->chat[slot].name), line->name);
				word(out->chat[slot].text, sizeof(out->chat[slot].text), line->text);
				out->chat[slot].seconds_ago = (social_now(engine) - line->time) / 1000;
			}
			out->chat_count = party->chat_count;
		}
	}
	snapshot->invite_serial = engine->invite_serial;
	snapshot->invite_result = engine->invite_result;
	snapshot->add_serial = engine->add_serial;
	snapshot->add_result = engine->add_result;
}

int social_front_enabled(struct social_front *front)
{
	int enabled;

	pthread_mutex_lock(&front->lock);
	enabled = front->snapshot_made && front->snapshot.enabled;
	pthread_mutex_unlock(&front->lock);
	return enabled;
}

void social_front_code(struct social_front *front, char *code, int size)
{
	pthread_mutex_lock(&front->lock);
	snprintf(code, (size_t)size, "%s", front->snapshot.code);
	pthread_mutex_unlock(&front->lock);
}

unsigned int social_front_snapshot(struct social_front *front, struct social_snapshot *snapshot)
{
	unsigned int version;

	pthread_mutex_lock(&front->lock);
	*snapshot = front->snapshot;
	version = front->snapshot_made ? front->snapshot_version : 0;
	pthread_mutex_unlock(&front->lock);
	return version;
}

int social_front_event(struct social_front *front, struct social_api_event *event)
{
	int found = 0;

	pthread_mutex_lock(&front->lock);
	if (front->event_count)
	{
		*event = front->events[front->event_first];
		front->event_first = (front->event_first + 1) % EVENT_QUEUE;
		front->event_count--;
		found = 1;
	}
	pthread_mutex_unlock(&front->lock);
	return found;
}

/* ---------- commands */

static int post(struct social_front *front, const struct command *command)
{
	int posted = 0;

	pthread_mutex_lock(&front->lock);
	if (front->command_count < COMMAND_QUEUE)
	{
		front->commands[(front->command_first + front->command_count) % COMMAND_QUEUE] = *command;
		front->command_count++;
		posted = 1;
	}
	pthread_mutex_unlock(&front->lock);
	return posted;
}

static void post_id(struct social_front *front, int type, unsigned int id)
{
	struct command command;

	memset(&command, 0, sizeof(command));
	command.type = type;
	command.id = id;
	post(front, &command);
}

/* the add is checked where it is asked, with the snapshot (the code's letters, the list's room), and again where it is done */
int social_front_add_code(struct social_front *front, const char *text)
{
	struct command command;
	unsigned char code_bytes[SOCIAL_CODE_BYTES];
	int result;

	pthread_mutex_lock(&front->lock);
	if (!front->snapshot_made || !front->snapshot.enabled)
	{
		pthread_mutex_unlock(&front->lock);
		return SOCIAL_ADD_RESULT_OFF;
	}
	pthread_mutex_unlock(&front->lock);
	result = social_code_parse(text, code_bytes, NULL);
	if (result == SOCIAL_CODE_BAD_TEXT)
		return SOCIAL_ADD_RESULT_BAD_TEXT;
	if (result == SOCIAL_CODE_BAD_CHECK)
		return SOCIAL_ADD_RESULT_BAD_CHECK;
	memset(&command, 0, sizeof(command));
	command.type = _command_add;
	snprintf(command.text, sizeof(command.text), "%s", text ? text : "");
	if (!post(front, &command))
		return SOCIAL_ADD_RESULT_BUSY;
	return SOCIAL_ADD_RESULT_SENT;
}

void social_front_invite(struct social_front *front, unsigned int friend_id) { post_id(front, _command_invite, friend_id); }
void social_front_invite_dismiss(struct social_front *front, unsigned int id) { post_id(front, _command_invite_dismiss, id); }
void social_front_party(struct social_front *front, int what, unsigned int id, int value, const char *text)
{
	struct command command;

	memset(&command, 0, sizeof(command));
	command.type = _command_party;
	command.a = what;
	command.id = id;
	command.b = value;
	snprintf(command.text, sizeof(command.text), "%s", text ? text : "");
	post(front, &command);
}

void social_front_party_token(struct social_front *front, const unsigned char *token)
{
	struct command command;

	memset(&command, 0, sizeof(command));
	command.type = _command_party_token;
	memcpy(command.token, token, SOCIAL_TOKEN_SIZE);
	post(front, &command);
}

int social_front_follow(struct social_front *front, struct social_join_target *target, unsigned int *order)
{
	int want;

	pthread_mutex_lock(&front->lock);
	want = front->follow_want;
	if (want)
	{
		*target = front->follow_target;
		*order = front->follow_order;
	}
	pthread_mutex_unlock(&front->lock);
	return want;
}

void social_front_follow_report(struct social_front *front, unsigned int order, int result)
{
	struct command command;

	memset(&command, 0, sizeof(command));
	command.type = _command_follow_report;
	command.id = order;
	command.a = result;
	post(front, &command);
}

void social_front_accept(struct social_front *front, unsigned int id) { post_id(front, _command_accept, id); }
void social_front_ignore(struct social_front *front, unsigned int id) { post_id(front, _command_ignore, id); }
void social_front_cancel(struct social_front *front, unsigned int id) { post_id(front, _command_cancel, id); }
void social_front_remove(struct social_front *front, unsigned int id) { post_id(front, _command_remove, id); }
void social_front_block(struct social_front *front, unsigned int id) { post_id(front, _command_block, id); }

void social_front_unblock(struct social_front *front, int index)
{
	struct command command;

	memset(&command, 0, sizeof(command));
	command.type = _command_unblock;
	command.a = index;
	post(front, &command);
}

void social_front_set_nickname(struct social_front *front, unsigned int id, const char *nickname)
{
	struct command command;

	memset(&command, 0, sizeof(command));
	command.type = _command_nickname;
	command.id = id;
	snprintf(command.text, sizeof(command.text), "%s", nickname ? nickname : "");
	post(front, &command);
}

void social_front_set_name(struct social_front *front, const char *name)
{
	struct command command;

	memset(&command, 0, sizeof(command));
	command.type = _command_name;
	snprintf(command.text, sizeof(command.text), "%s", name ? name : "");
	post(front, &command);
}

void social_front_set_share(struct social_front *front, int show_status, int details, int accept_requests)
{
	struct command command;

	memset(&command, 0, sizeof(command));
	command.type = _command_share;
	command.a = show_status;
	command.b = details;
	command.c = accept_requests;
	post(front, &command);
}

void social_front_set_share_part(struct social_front *front, int which, int value)
{
	struct command command;

	memset(&command, 0, sizeof(command));
	command.type = _command_share_part;
	command.a = which;
	command.b = value;
	post(front, &command);
}

void social_front_set_enabled(struct social_front *front, int enabled)
{
	struct command command;

	memset(&command, 0, sizeof(command));
	command.type = _command_enable;
	command.a = enabled;
	post(front, &command);
}

void social_front_set_place(struct social_front *front, const struct social_place *place)
{
	struct command command;

	memset(&command, 0, sizeof(command));
	command.type = _command_place;
	command.place = *place;
	post(front, &command);
}

static void run_command(struct social_front *front, const struct command *command)
{
	struct social_engine *engine = &front->engine;

	switch (command->type)
	{
	case _command_add:
		social_engine_add_code(engine, command->text);
		break;
	case _command_accept:
		social_engine_accept(engine, command->id);
		break;
	case _command_ignore:
		social_engine_ignore(engine, command->id);
		break;
	case _command_cancel:
		social_engine_cancel(engine, command->id);
		break;
	case _command_remove:
		social_engine_remove(engine, command->id);
		break;
	case _command_block:
		social_engine_block(engine, command->id);
		break;
	case _command_unblock:
		social_engine_unblock(engine, command->a);
		break;
	case _command_nickname:
		social_engine_set_nickname(engine, command->id, command->text);
		break;
	case _command_name:
		social_engine_set_name(engine, command->text);
		break;
	case _command_share:
		social_engine_set_share(engine, command->a, command->b, command->c);
		break;
	case _command_share_part:
		social_engine_set_share(engine, command->a == 0 ? command->b : engine->share_status, command->a == 1 ? command->b : engine->details,
			command->a == 2 ? command->b : engine->accept_requests);
		break;
	case _command_enable:
		social_set_enabled(engine, command->a);
		break;
	case _command_place:
		social_engine_set_place(engine, &command->place);
		break;
	case _command_invite:
		social_engine_invite(engine, command->id);
		break;
	case _command_party_token:
		social_engine_party_result(engine, social_engine_party_join(engine, command->token));
		break;
	case _command_follow_report:
		social_engine_party_follow_report(engine, command->id, command->a);
		break;
	case _command_party:
		switch (command->a)
		{
		case 1: social_engine_party_result(engine, social_engine_party_make(engine)); break;
		case 2: social_engine_party_result(engine, social_engine_party_invite(engine, command->id)); break;
		case 4: social_engine_party_result(engine, social_engine_party_join_friend(engine, command->id)); break;
		case 5: social_engine_party_result(engine, social_engine_party_leave(engine)); break;
		case 6: social_engine_party_result(engine, social_engine_party_kick(engine, command->id)); break;
		case 7: social_engine_party_result(engine, social_engine_party_transfer(engine, command->id)); break;
		case 8: social_engine_party_result(engine, social_engine_party_set_open(engine, command->b)); break;
		case 9: social_engine_party_result(engine, social_engine_party_set_auto_follow(engine, command->b)); break;
		case 10: social_engine_party_result(engine, social_engine_party_chat(engine, command->text)); break;
		case 11: social_engine_party_result(engine, social_engine_party_disband(engine)); break;
		}
		break;
	case _command_invite_dismiss:
		social_engine_invite_dismiss(engine, command->id);
		break;
	}
}

/* ---------- the pass */

void social_front_heard(struct social_front *front, const char *topic, const unsigned char *payload, int size)
{
	social_heard(&front->engine, topic, payload, size);
}

void social_front_tick(struct social_front *front)
{
	struct social_engine *engine = &front->engine;
	struct social_event event;

	/* commands: copied out one at a time, run outside the lock */
	for (;;)
	{
		struct command command;
		int found = 0;

		pthread_mutex_lock(&front->lock);
		if (front->command_count)
		{
			command = front->commands[front->command_first];
			front->command_first = (front->command_first + 1) % COMMAND_QUEUE;
			front->command_count--;
			found = 1;
		}
		pthread_mutex_unlock(&front->lock);
		if (!found)
			break;
		run_command(front, &command);
	}
	social_update(engine);
	/* events, to the game's queue */
	while (social_event_take(engine, &event))
	{
		struct social_api_event out;
		int type = SOCIAL_API_EVENT_NONE;

		switch (event.type)
		{
		case SOCIAL_EVENT_REQUEST: type = SOCIAL_API_EVENT_REQUEST; break;
		case SOCIAL_EVENT_FRIEND: type = SOCIAL_API_EVENT_FRIEND; break;
		case SOCIAL_EVENT_REMOVED: type = SOCIAL_API_EVENT_REMOVED; break;
		case SOCIAL_EVENT_ONLINE: type = SOCIAL_API_EVENT_ONLINE; break;
		case SOCIAL_EVENT_OFFLINE: type = SOCIAL_API_EVENT_OFFLINE; break;
		case SOCIAL_EVENT_INVITE: type = SOCIAL_API_EVENT_INVITE; break;
		case SOCIAL_EVENT_PARTY_INVITE: type = SOCIAL_API_EVENT_PARTY_INVITE; break;
		case SOCIAL_EVENT_PARTY_JOINED: type = SOCIAL_API_EVENT_PARTY_JOINED; break;
		case SOCIAL_EVENT_PARTY_LEFT: type = SOCIAL_API_EVENT_PARTY_LEFT; break;
		case SOCIAL_EVENT_PARTY_KICKED: type = SOCIAL_API_EVENT_PARTY_KICKED; break;
		case SOCIAL_EVENT_PARTY_LEADER: type = SOCIAL_API_EVENT_PARTY_LEADER; break;
		case SOCIAL_EVENT_PARTY_CHAT: type = SOCIAL_API_EVENT_PARTY_CHAT; break;
		}
		if (type == SOCIAL_API_EVENT_NONE)
			continue;
		memset(&out, 0, sizeof(out));
		out.type = type;
		out.id = event.id;
		word(out.name, sizeof(out.name), event.name);
		word(out.code, sizeof(out.code), event.code);
		word(out.text, sizeof(out.text), event.text);
		pthread_mutex_lock(&front->lock);
		if (front->event_count == EVENT_QUEUE)
		{
			front->event_first = (front->event_first + 1) % EVENT_QUEUE;
			front->event_count--;
		}
		front->events[(front->event_first + front->event_count) % EVENT_QUEUE] = out;
		front->event_count++;
		pthread_mutex_unlock(&front->lock);
	}
	/* the leader's game to follow */
	{
		struct social_join_target target;
		unsigned int order = 0;
		int want = social_party_follow_wanted(engine, &target, &order);

		pthread_mutex_lock(&front->lock);
		front->follow_want = want;
		if (want)
		{
			front->follow_target = target;
			front->follow_order = order;
		}
		pthread_mutex_unlock(&front->lock);
	}
	/* the snapshot, when anything changed or a second has gone (the ages) */
	if (!front->snapshot_made || engine->version != front->snapshot_engine_version || social_elapsed(engine, front->snapshot_time, 1000))
	{
		snapshot_make(front, &front->scratch);
		pthread_mutex_lock(&front->lock);
		front->snapshot_version++;
		front->scratch.version = front->snapshot_version;
		front->snapshot = front->scratch;
		front->snapshot_made = 1;
		pthread_mutex_unlock(&front->lock);
		front->snapshot_engine_version = engine->version;
		front->snapshot_time = social_now(engine);
	}
	save_update(front);
}
