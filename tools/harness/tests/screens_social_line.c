#include "harness.h"

/* What a line typed on the GamePad's keyboard does (port/linux/game/screens_social.c, keyboard_destination and screens_social_line): the tab shown picks the destination (the party's chat,
a friend's code, the game's chat), the line goes to the provider's party_chat or friend_add, and its answer comes back as a toast for the next frame. Stand-ins for the provider and the chat. */

#define SCREENS_LINE_SIZE 72
enum { HALO_SOCIAL_TAB_FRIENDS, HALO_SOCIAL_TAB_PARTY, HALO_SOCIAL_TAB_BOTS };
enum { NETWORK_CHAT_TO_GAME, NETWORK_CHAT_TO_PARTY, NETWORK_CHAT_TO_FRIEND_CODE };
enum { SCREENS_TOAST_JOIN_FAILED, SCREENS_TOAST_NOTICE };

struct screens_toast
{
	long id;
	unsigned char kind;
	char text[SCREENS_LINE_SIZE];
	char detail[SCREENS_LINE_SIZE];
};
struct screens_provider
{
	int (*friend_add)(char const *code, char *answer, int size);
	int (*party_chat)(char const *text, char *answer, int size);
};

static struct
{
	struct screens_toast line_toast;
	int line_toast_pending;
} social;

static char party_said[80], code_added[80];
static int party_result = 1, add_result = 1;
static struct screens_provider provider;
static struct screens_provider const *screens_provider(void) { return &provider; }
static void platform_log(char const *format, ...) { (void)format; }
static void copy(char *destination, int size, char const *source) { snprintf(destination, (size_t)size, "%s", source ? source : ""); }
static int party_chat(char const *text, char *answer, int size)
{
	snprintf(party_said, sizeof(party_said), "%s", text);
	snprintf(answer, (size_t)size, "%s", party_result ? "SENT TO THE PARTY." : "YOU ARE NOT IN A PARTY.");
	return party_result;
}
static int friend_add(char const *code, char *answer, int size)
{
	snprintf(code_added, sizeof(code_added), "%s", code);
	snprintf(answer, (size_t)size, "%s", add_result ? "REQUEST SENT." : "NOT A CODE.");
	return add_result;
}

#include "social_line_code.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	provider.friend_add = friend_add;
	provider.party_chat = party_chat;
	CASE("destination")
	{
		CHECK(keyboard_destination(HALO_SOCIAL_TAB_PARTY) == NETWORK_CHAT_TO_PARTY, "party tab");
		CHECK(keyboard_destination(HALO_SOCIAL_TAB_FRIENDS) == NETWORK_CHAT_TO_FRIEND_CODE, "friends tab");
		CHECK(keyboard_destination(HALO_SOCIAL_TAB_BOTS) == NETWORK_CHAT_TO_GAME, "bots tab");
	}
	else CASE("lines")
	{
		CHECK(screens_social_line(NETWORK_CHAT_TO_PARTY, "hello") == 1 && !strcmp(party_said, "hello") && code_added[0] == 0, "party chat: '%s'", party_said);
		CHECK(social.line_toast_pending && social.line_toast.kind == SCREENS_TOAST_NOTICE && !strcmp(social.line_toast.text, "SENT TO THE PARTY."), "its answer: '%s'", social.line_toast.text);
		social.line_toast_pending = 0;
		party_result = 0;
		CHECK(screens_social_line(NETWORK_CHAT_TO_PARTY, "again") == 1 && social.line_toast.kind == SCREENS_TOAST_JOIN_FAILED && !strcmp(social.line_toast.text, "YOU ARE NOT IN A PARTY."), "refused: '%s'", social.line_toast.text);
		social.line_toast_pending = 0;
		CHECK(screens_social_line(NETWORK_CHAT_TO_FRIEND_CODE, "abcd efgh jklm n") == 1 && !strcmp(code_added, "abcd efgh jklm n") && social.line_toast.kind == SCREENS_TOAST_NOTICE, "a code");
		/* a line for the game is not taken */
		social.line_toast_pending = 0;
		party_said[0] = 0;
		CHECK(screens_social_line(NETWORK_CHAT_TO_GAME, "gg") == 0 && !party_said[0] && !social.line_toast_pending, "the game's chat is the game's");
		provider.party_chat = NULL;
		CHECK(screens_social_line(NETWORK_CHAT_TO_PARTY, "x") == 0, "no party chat in this provider");
	}
	else
		return 2;
	return 0;
}
