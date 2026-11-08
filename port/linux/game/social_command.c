/*
SOCIAL_COMMAND.C

The friends layer as lines of text (social_command.h).
*/

#include "../src/p2p_internal.h"
#include "social_core.h"
#include "social_command.h"
#include "social_join.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct output
{
	char *text;
	int size, length;
};

static void say(struct output *out, const char *format, ...)
{
	va_list arguments;
	int written;

	if (out->length >= out->size - 1)
		return;
	va_start(arguments, format);
	written = vsnprintf(out->text + out->length, (size_t)(out->size - out->length), format, arguments);
	va_end(arguments);
	if (written < 0)
		return;
	out->length += written < out->size - out->length ? written : out->size - out->length - 1;
}

static struct social_snapshot snapshot;

static const char *activity_word(int activity)
{
	static const char *const words[] = { "offline", "menus", "lobby", "game", "solo", "busy" };

	return activity >= 0 && activity < 6 ? words[activity] : "?";
}

static const char *platform_word(int platform)
{
	static const char *const words[] = { "?", "Wii U", "Windows", "Linux", "macOS", "Android", "web" };

	return platform >= 0 && platform < 7 ? words[platform] : "?";
}

static int number(const char *text, int limit)
{
	int value = text ? atoi(text) : 0;

	return value >= 1 && value <= limit ? value : 0;
}

static void show_friends(struct output *out)
{
	int index;

	say(out, "%d friends\n", snapshot.friend_count);
	for (index = 0; index < snapshot.friend_count; index++)
	{
		const struct social_friend_info *f = &snapshot.friends[index];

		say(out, "%d %s%s%s %s %s", index + 1, f->name, f->nickname[0] ? " aka " : "", f->nickname, f->code, f->online ? "online" : "offline");
		if (f->online)
		{
			say(out, " %s on %s v%d", activity_word(f->activity), platform_word(f->platform), f->network_version);
			if (f->map[0])
				say(out, " %s %s %d/%d", f->map, f->gametype, f->players, f->maximum_players);
			if (f->joinable)
				say(out, " JOINABLE");
			else if (f->hosting)
				say(out, " hosting");
			if (f->private_game)
				say(out, " private");
			if (f->in_party)
				say(out, f->party_open ? " party(open)" : " party");
		}
		else if (f->seconds_since_heard)
			say(out, " last heard %us ago", f->seconds_since_heard);
		if (!f->keys_exchanged)
			say(out, " (keys not exchanged yet)");
		say(out, "\n");
	}
}

static void show_party(struct output *out)
{
	const struct social_party_info *p = &snapshot.party;
	int index;

	if (!p->active)
	{
		say(out, "no party\n");
		return;
	}
	say(out, "party of %d%s%s%s\n", p->member_count, p->is_leader ? ", you lead" : "", p->joining ? ", joining" : "", p->open ? ", open" : "");
	for (index = 0; index < p->member_count; index++)
	{
		const struct social_party_member_info *m = &p->members[index];

		say(out, "%d %s%s%s%s%s %s\n", index + 1, m->name, m->leader ? " (leader)" : "", m->self ? " (you)" : "", m->online ? "" : " silent", m->in_game ? " in game" : "",
			m->follow_text);
	}
	if (p->game_present)
		say(out, "leader's game: %s %s %d/%d (order %u)\n", p->game.map, p->game.gametype, p->game.players, p->game.maximum_players, p->game_order);
	for (index = 0; index < p->chat_count; index++)
		say(out, "chat %s: %s\n", p->chat[index].name, p->chat[index].text);
}

static void timing(struct output *out)
{
	unsigned char seed[SOCIAL_SEED_SIZE], key[SOCIAL_KEY_SIZE], secret[SOCIAL_KEY_SIZE], signature[SOCIAL_SIGNATURE_SIZE], shared[SOCIAL_KEY_SIZE], message[64];
	unsigned long started;
	int index, valid = 0;
	const int rounds = 50;

	memset(seed, 7, sizeof(seed));
	memset(message, 9, sizeof(message));
	p2p_ed25519_public(seed, key, secret);
	started = p2p_now();
	for (index = 0; index < rounds; index++)
	{
		message[0] = (unsigned char)index;
		p2p_ed25519_sign(seed, key, message, (int)sizeof(message), signature);
	}
	say(out, "ed25519 sign: %lu ms for %d (%.2f ms each)\n", p2p_now() - started, rounds, (double)(p2p_now() - started) / rounds);
	started = p2p_now();
	for (index = 0; index < rounds; index++)
		valid += p2p_ed25519_verify(key, message, (int)sizeof(message), signature);
	say(out, "ed25519 verify: %lu ms for %d (%.2f ms each; %d valid)\n", p2p_now() - started, rounds, (double)(p2p_now() - started) / rounds, valid);
	started = p2p_now();
	for (index = 0; index < rounds; index++)
		p2p_x25519(shared, secret, key);
	say(out, "x25519: %lu ms for %d (%.2f ms each)\n", p2p_now() - started, rounds, (double)(p2p_now() - started) / rounds);
	say(out, "the budgets assume a signature check each %d ms and a burst of %d\n", SOCIAL_VERIFY_INTERVAL, SOCIAL_VERIFY_BURST);
}

static void show_requests(struct output *out)
{
	int index;

	for (index = 0; index < snapshot.request_count; index++)
		say(out, "%d request from %s %s (%us ago)\n", index + 1, snapshot.requests[index].name, snapshot.requests[index].code, snapshot.requests[index].seconds_ago);
	if (!snapshot.request_count)
		say(out, "no requests\n");
}

static void show_sent(struct output *out)
{
	int index;

	for (index = 0; index < snapshot.sent_count; index++)
		say(out, "%d waiting for %s\n", index + 1, snapshot.sent[index].code);
	if (!snapshot.sent_count)
		say(out, "no requests waiting\n");
}

static void show_invites(struct output *out)
{
	int index;

	for (index = 0; index < snapshot.invite_count; index++)
	{
		const struct social_invite_info *invite = &snapshot.invites[index];

		if (invite->kind == 1)
			say(out, "%d %s invites you to %s %s %d/%d v%d (%us ago)\n", index + 1, invite->name, invite->target.map, invite->target.gametype, invite->target.players,
				invite->target.maximum_players, invite->target.network_version, invite->seconds_ago);
		else
			say(out, "%d %s invites you to a party of %d (%us ago)\n", index + 1, invite->name, invite->party_size, invite->seconds_ago);
	}
	if (!snapshot.invite_count)
		say(out, "no invites\n");
}

static void show_events(struct output *out)
{
	struct social_api_event event;
	static const char *const names[] = { "none", "request", "friend", "removed", "online", "offline", "invite", "party invite", "party joined", "party left", "party kicked",
		"party leader", "party chat" };
	int count = 0;

	while (count < 20 && social_event_next(&event))
	{
		say(out, "event %s %s %s %s\n", event.type >= 0 && event.type < 13 ? names[event.type] : "?", event.name, event.code, event.text);
		count++;
	}
	if (!count)
		say(out, "no events\n");
}

static void show_join(struct output *out)
{
	struct social_join_status status;
	static const char *const states[] = { "idle", "working", "ready", "failed" };

	social_join_status_get(&status);
	say(out, "join %s: %s%s%s\n", status.state >= 0 && status.state < 4 ? states[status.state] : "?", status.text, status.state == SOCIAL_JOIN_FAILED ? " (" : "",
		status.state == SOCIAL_JOIN_FAILED ? social_join_reason_word(status.reason) : "");
	if (status.state == SOCIAL_JOIN_FAILED)
		say(out, ")\n");
}

int social_command(const char *line, char *reply, int size)
{
	struct output out;
	char copy[256];
	char *word, *rest;
	int count, value;

	out.text = reply;
	out.size = size;
	out.length = 0;
	if (size > 0)
		reply[0] = 0;
	snprintf(copy, sizeof(copy), "%s", line ? line : "");
	word = copy;
	while (*word == ' ')
		word++;
	rest = strchr(word, ' ');
	if (rest)
	{
		*rest++ = 0;
		while (*rest == ' ')
			rest++;
	}
	else
		rest = word + strlen(word);
	social_snapshot_get(&snapshot);
	if (!snapshot.enabled && strcmp(word, "help") && strcmp(word, "timing"))
	{
		say(&out, "friends are off (social.enabled, or halo/social.txt with a 1); or not started yet\n");
		return 1;
	}
	if (!*word || !strcmp(word, "help"))
		say(&out, "help code status list requests sent invites events last add accept ignore cancel remove block unblock nick invite join joininvite joinstatus joincancel "
			"joindone party name share details requests activity timing\n");
	else if (!strcmp(word, "code"))
		say(&out, "your code %s, as %s\n", snapshot.code, snapshot.name);
	else if (!strcmp(word, "status"))
		say(&out, "on, %d brokers, %s, details %s, requests %s, %d friends, %d requests, %d waiting, %d invites, %d blocked, party %s\n", snapshot.brokers,
			snapshot.share_status ? "visible" : "appearing offline", snapshot.share_details == 0 ? "none" : snapshot.share_details == 1 ? "basic" : "full",
			snapshot.accept_requests ? "taken" : "refused", snapshot.friend_count, snapshot.request_count, snapshot.sent_count, snapshot.invite_count, snapshot.blocked_count,
			snapshot.party.active ? "yes" : "no");
	else if (!strcmp(word, "list"))
		show_friends(&out);
	else if (!strcmp(word, "requests") && !*rest)
		show_requests(&out);
	else if (!strcmp(word, "sent"))
		show_sent(&out);
	else if (!strcmp(word, "invites"))
		show_invites(&out);
	else if (!strcmp(word, "events"))
		show_events(&out);
	else if (!strcmp(word, "last"))
		say(&out, "last add: %s; last invite: %s; last party call: %s\n", social_add_result_text(snapshot.add_result), social_invite_result_text(snapshot.invite_result),
			social_party_result_text(snapshot.party.result));
	else if (!strcmp(word, "add"))
	{
		int result = social_add_code(rest);

		say(&out, "%s\n", social_add_result_text(result));
	}
	else if (!strcmp(word, "accept") && (value = number(rest, snapshot.request_count)))
	{
		social_accept(snapshot.requests[value - 1].id);
		say(&out, "accepted %s\n", snapshot.requests[value - 1].code);
	}
	else if (!strcmp(word, "ignore") && (value = number(rest, snapshot.request_count)))
	{
		social_ignore(snapshot.requests[value - 1].id);
		say(&out, "turned away %s\n", snapshot.requests[value - 1].code);
	}
	else if (!strcmp(word, "cancel") && (value = number(rest, snapshot.sent_count)))
	{
		social_cancel(snapshot.sent[value - 1].id);
		say(&out, "forgot the request to %s\n", snapshot.sent[value - 1].code);
	}
	else if (!strcmp(word, "remove") && (value = number(rest, snapshot.friend_count)))
	{
		social_remove(snapshot.friends[value - 1].id);
		say(&out, "removed %s\n", snapshot.friends[value - 1].name);
	}
	else if (!strcmp(word, "block") && (value = number(rest, snapshot.friend_count)))
	{
		social_block(snapshot.friends[value - 1].id);
		say(&out, "blocked %s\n", snapshot.friends[value - 1].name);
	}
	else if (!strcmp(word, "unblock") && (value = number(rest, snapshot.blocked_count)))
	{
		social_unblock(value - 1);
		say(&out, "unblocked\n");
	}
	else if (!strcmp(word, "nick") && (value = number(rest, snapshot.friend_count)))
	{
		char *text = strchr(rest, ' ');

		social_set_nickname(snapshot.friends[value - 1].id, text ? text + 1 : "");
		say(&out, "done\n");
	}
	else if (!strcmp(word, "invite") && (value = number(rest, snapshot.friend_count)))
	{
		social_invite_friend(snapshot.friends[value - 1].id);
		say(&out, "asked; `last` says how it went\n");
	}
	else if (!strcmp(word, "join") && (value = number(rest, snapshot.friend_count)))
	{
		int reason = social_join_friend(snapshot.friends[value - 1].id);

		say(&out, "%s (%s)\n", social_join_reason_text(reason, snapshot.friends[value - 1].map), social_join_reason_word(reason));
	}
	else if (!strcmp(word, "joininvite") && (value = number(rest, snapshot.invite_count)))
	{
		int reason = social_join_invite(snapshot.invites[value - 1].id);

		say(&out, "%s (%s)\n", social_join_reason_text(reason, snapshot.invites[value - 1].target.map), social_join_reason_word(reason));
	}
	else if (!strcmp(word, "joinstatus"))
		show_join(&out);
	else if (!strcmp(word, "joincancel"))
	{
		social_join_cancel();
		show_join(&out);
	}
	else if (!strcmp(word, "joindone"))
	{
		social_join_done();
		say(&out, "done\n");
	}
	else if (!strcmp(word, "party"))
	{
		char *argument = strchr(rest, ' ');

		if (argument)
			*argument++ = 0;
		else
			argument = rest + strlen(rest);
		if (!*rest)
			show_party(&out);
		else if (!strcmp(rest, "make"))
			social_party_make();
		else if (!strcmp(rest, "invite") && (value = number(argument, snapshot.friend_count)))
			social_party_invite(snapshot.friends[value - 1].id);
		else if (!strcmp(rest, "join") && (value = number(argument, snapshot.invite_count)))
			social_party_join_invite(snapshot.invites[value - 1].id);
		else if (!strcmp(rest, "joinfriend") && (value = number(argument, snapshot.friend_count)))
			social_party_join_friend(snapshot.friends[value - 1].id);
		else if (!strcmp(rest, "leave"))
			social_party_leave();
		else if (!strcmp(rest, "kick") && (value = number(argument, snapshot.party.member_count)))
			social_party_kick(snapshot.party.members[value - 1].id);
		else if (!strcmp(rest, "transfer") && (value = number(argument, snapshot.party.member_count)))
			social_party_transfer(snapshot.party.members[value - 1].id);
		else if (!strcmp(rest, "chat"))
			social_party_chat(argument);
		else if (!strcmp(rest, "open") || !strcmp(rest, "close"))
			social_party_set_open(!strcmp(rest, "open"));
		else if (!strcmp(rest, "follow") || !strcmp(rest, "unfollow"))
			social_party_set_auto_follow(!strcmp(rest, "follow"));
		else
		{
			say(&out, "party make|invite n|join n|joinfriend n|leave|kick m|transfer m|chat text|open|close|follow|unfollow\n");
			return 1;
		}
		if (*rest)
			say(&out, "asked; `last` says how it went, `party` shows it\n");
	}
	else if (!strcmp(word, "name"))
	{
		social_set_name(rest);
		say(&out, "done\n");
	}
	else if (!strcmp(word, "share") && (!strcmp(rest, "online") || !strcmp(rest, "offline")))
	{
		social_set_show_status(!strcmp(rest, "online"));
		say(&out, "done\n");
	}
	else if (!strcmp(word, "details") && (!strcmp(rest, "none") || !strcmp(rest, "basic") || !strcmp(rest, "full")))
	{
		social_set_details(!strcmp(rest, "none") ? 0 : !strcmp(rest, "basic") ? 1 : 2);
		say(&out, "done\n");
	}
	else if (!strcmp(word, "requests") && (!strcmp(rest, "on") || !strcmp(rest, "off")))
	{
		social_set_accept_requests(!strcmp(rest, "on"));
		say(&out, "done\n");
	}
	else if (!strcmp(word, "activity"))
	{
		static const char *const words[] = { "menus", "lobby", "game", "solo", "busy" };
		static const int codes[] = { SOCIAL_ACTIVITY_MENUS, SOCIAL_ACTIVITY_LOBBY, SOCIAL_ACTIVITY_GAME, SOCIAL_ACTIVITY_SOLO, SOCIAL_ACTIVITY_BUSY };

		for (count = 0; count < 5; count++)
		{
			if (!strcmp(rest, words[count]))
			{
				social_set_activity(codes[count]);
				say(&out, "done\n");
				return 1;
			}
		}
		say(&out, "activity menus|lobby|game|solo|busy\n");
	}
	else if (!strcmp(word, "timing"))
		timing(&out);
	else
		say(&out, "no such command; `help`\n");
	return 1;
}
