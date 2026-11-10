#include "harness.h"
#include <limits.h>
#include <math.h>
/* A remote player's input as the host relays it, the relayed actions message
(port/linux/game/network_distributed.c): the host's update, then each
player's entry, written and read value by value. The wire is little-endian,
as every build sends it (protocol 41): a big-endian machine turns a
message's values as it sends it and as it receives it (network_wire_order.c).
The host's side is distributed_host_send_players's (its packer, the update
first, then the entries), the client's network_distributed_handle_message's
(a copy put in this machine's order, then distributed_handle_actions), and
what the client keeps is player_queues_new.c's: the input of an update newer
than the last it took, which drives the player until the next. Player 3 is
another machine's, relayed to this one. */
typedef unsigned short word;
typedef word message_header;
typedef struct { real yaw, pitch; } real_euler_angles2d;
typedef struct { real i, j; } real_vector2d;
#define csmemcpy memcpy
#define csmemset memset
#define MIN(a,b) ((a)>(b)?(b):(a))
#define CEILING(n,ceiling) ((n)>(ceiling)?(ceiling):(n))
#define PIN(n,floor,ceiling) ((n)<(floor) ? (floor) : CEILING((n),(ceiling)))
#define SET_FLAG(f, b, v) ((v) ? ((f)|=(unsigned)FLAG(b)) : ((f)&=(unsigned)~FLAG(b)))
#define _pi ((real)3.14159265358979323846f)
#define MAXIMUM_NUMBER_OF_PLAYERS 16
#define MAXIMUM_WEAPONS_PER_UNIT 4
#define NUMBER_OF_UNIT_GRENADE_TYPES 2
#include "types.inc"
static long now = 900;
static long game_time_get(void) { return now; }
static long config_integer(char const *name) { (void)name; return 0; }
static void platform_log(char const *format, ...) { (void)format; }
#define build_message_header(header, size, priority, flags) (*(header) = (message_header)(size))
static struct { long sent; } distributed_statistics;
/* (a batch with room for the message; the message goes to the test's wire) */
#define distributed_batch_room(sender, minimum) ((word)DATAGRAM_MAXIMUM_SIZE)
static void distributed_batch_add(short sender, void const *message, word size, word entry_size);
/* (the other kinds' entries, none of them here) */
#define network_objects_wire_order(type, entries, count, size) ((void)(entries))
#define network_damage_wire_order(type, entries, count, size) ((void)(entries))
#define network_actors_wire_order(type, entries, count, size) ((void)(entries))
#define network_coop_wire_order(type, entries, count, size) ((void)(entries))
#include "under_test.inc"

enum { REMOTE_PLAYER = 3 };

/* the message as it went on the wire */
static byte wire[sizeof(struct distributed_message_header) + DATAGRAM_MAXIMUM_SIZE];
static word wire_size;

/* distributed_batch_add's first step: the message in the wire's order */
static void distributed_batch_add(short sender, void const *message, word size, word entry_size)
{
 message = distributed_wire_message_to_send(message, size);
 CHECK(size <= sizeof(wire), "a message of %u bytes", (unsigned)size);
 memcpy(wire, message, size);
 wire_size = size;
 (void)sender; (void)entry_size;
}

static struct distributed_packer distributed_packer;
static long distributed_host_update_number;

/* the host: the player's input of the update (the same buttons the ticks
before it), relayed as distributed_host_send_players relays it */
static void host_sends(long update_number, real yaw, real pitch, unsigned long buttons)
{
 static struct player_action actions[DISTRIBUTED_INPUT_HISTORY][MAXIMUM_NUMBER_OF_PLAYERS];
 struct player_action const *recent[DISTRIBUTED_INPUT_HISTORY];
 short counts[DISTRIBUTED_INPUT_HISTORY];
 struct distributed_relayed_action relayed;
 byte entry[DISTRIBUTED_RELAYED_ACTION_MAXIMUM_SIZE];
 short history;

 memset(actions, 0, sizeof(actions));
 for (history = 0; history < DISTRIBUTED_INPUT_HISTORY; history++)
 {
  struct player_action *action = &actions[history][REMOTE_PLAYER];

  action->control_flags = buttons;
  action->desired_facing.yaw = yaw;
  action->desired_facing.pitch = pitch;
  action->throttle.i = 1.0f;
  action->throttle.j = 0.0f;
  action->primary_trigger = 1.0f;
  action->desired_weapon_index = NONE;
  action->desired_grenade_index = NONE;
  action->desired_zoom_level = NONE;
  recent[history] = actions[history];
  counts[history] = MAXIMUM_NUMBER_OF_PLAYERS;
 }
 distributed_relayed_action_from(REMOTE_PLAYER, recent, counts, &relayed);
 distributed_host_update_number = update_number;
 distributed_packer_begin(&distributed_packer, 1, _distributed_message_relayed_actions,
  &distributed_host_update_number, sizeof(distributed_host_update_number));
 distributed_packer_add(&distributed_packer, entry, distributed_relayed_action_write(&relayed, entry));
 distributed_packer_flush(&distributed_packer);
}

/* the client: a message as network_distributed_handle_message takes it, in a
copy put in this machine's order, handed to distributed_handle_actions */
static long received_copy[(USHRT_MAX + 1) / sizeof(long)];
static void client_takes(byte const *message, word size)
{
 struct distributed_message_header header;

 memcpy(received_copy, message, size);
#ifdef __BIG_ENDIAN__
 distributed_wire_message(received_copy, size);
#endif
 memcpy(&header, received_copy, sizeof(header));
 CHECK(header.type == _distributed_message_relayed_actions && header.count == 1, "the message, type %d count %d",
  header.type, header.count);
 CHECK(header.game_time == now, "the host's tick %ld, not %ld", now, (long)header.game_time);
 distributed_handle_actions((byte const *)received_copy + sizeof(header), (byte const *)received_copy + size,
  header.count);
}

/* (a turn's 65536th: what the packing keeps of an angle) */
#define ANGLE_STEP (2.0f * _pi / 65536.0f)

/* what the client keeps of the player: its input, the update's (it drives
the player until a newer update's comes) */
static void check_input(long update_number, real yaw, real pitch, unsigned long buttons)
{
 struct player_action const *action = &update_client_relayed_actions[REMOTE_PLAYER].action;

 CHECK(update_client_relayed_actions[REMOTE_PLAYER].valid, "the player's input taken");
 CHECK(fabsf(action->desired_facing.yaw - yaw) <= ANGLE_STEP, "the player aims as update %ld has it, yaw %f, not %f "
  "(the client has update 0x%lx)", update_number, yaw, action->desired_facing.yaw,
  update_client_relayed_actions[REMOTE_PLAYER].update_number);
 CHECK(fabsf(action->desired_facing.pitch - pitch) <= ANGLE_STEP, "pitch %f, not %f", pitch,
  action->desired_facing.pitch);
 CHECK(action->throttle.i == 1.0f && action->throttle.j == 0.0f, "running ahead, (%f %f)", action->throttle.i,
  action->throttle.j);
 CHECK(action->primary_trigger == 1.0f, "the trigger held, %f", action->primary_trigger);
 CHECK(action->control_flags == buttons, "the buttons 0x%lx, not 0x%lx", buttons, action->control_flags);
 CHECK((update_client_relayed_actions[REMOTE_PLAYER].pending_control_flags & buttons) == buttons,
  "the buttons pending for the next tick, 0x%lx", update_client_relayed_actions[REMOTE_PLAYER].pending_control_flags);
 CHECK(action->desired_weapon_index == NONE && action->desired_grenade_index == NONE &&
  action->desired_zoom_level == NONE, "no choices");
}

/* ... and the update it is of, the host's number */
static void check_update(long update_number)
{
 CHECK(update_client_relayed_actions[REMOTE_PLAYER].update_number == update_number, "update %ld (0x%lx), not 0x%lx",
  update_number, update_number, update_client_relayed_actions[REMOTE_PLAYER].update_number);
}

/* the update 0x12381 (its low byte past 127, a negative long turned) as an
x86 host sends it, byte by byte: player 3 aims 10430/65536 of a turn round
(1 radian) and 62928/65536 (-0.25 radians: pitch) at tick 900 (0x384),
running ahead, firing, buttons 0x0105. (Its first two bytes are not on the
wire: a batch's message has the batch's.) */
static byte const x86_message[] = {
 0x00, 0x00, _distributed_message_relayed_actions, 1, 0x84, 0x03, 0x00, 0x00,
 0x81, 0x23, 0x01, 0x00,
 REMOTE_PLAYER, 0x00, 0xFF, 0xFF, 0xFF, 0xBE, 0x28, 0xD0, 0xF5, 0x7F, 0x00, 0xFF, 0x05, 0x01,
};

int main(int argc, char **argv)
{
 const char *case_name = argc > 1 ? argv[1] : "";

 CASE("wire-bytes") {
  /* the host's message is the x86 host's, byte for byte (but the header's
  size, which the batch's replaces) */
  int index;

  host_sends(0x12381, 1.0f, -0.25f, 0x0105);
  CHECK(wire_size == sizeof(x86_message), "%u bytes, not %u", (unsigned)wire_size, (unsigned)sizeof(x86_message));
  for (index = 2; index < (int)sizeof(x86_message); index++)
   CHECK(wire[index] == x86_message[index], "byte %d is 0x%02X, not 0x%02X", index, wire[index], x86_message[index]);
 } else CASE("x86-host") {
  client_takes(x86_message, sizeof(x86_message));
  check_input(0x12381, 1.0f, -0.25f, 0x0105);
  check_update(0x12381);
 } else CASE("round-trip") {
  host_sends(0x12381, 2.5f, 0.3f, 0x0041);
  client_takes(wire, wire_size);
  check_input(0x12381, 2.5f, 0.3f, 0x0041);
  check_update(0x12381);
 } else CASE("newer-updates") {
  /* a far player's input, relayed every third tick, for half a minute: the
  update's low byte past 127 and through 255 (an update turned twice is
  negative past 127 there, and in the next 256 less than the last taken) */
  long update_number;

  for (update_number = 90; update_number < 990; update_number += 3)
  {
   real yaw = (real)(update_number % 360) * (2.0f * _pi / 360.0f);
   real pitch = (real)(update_number % 61 - 30) * (_pi / 180.0f);

   host_sends(update_number, yaw, pitch, (unsigned long)(update_number & 0x0F));
   client_takes(wire, wire_size);
   check_input(update_number, yaw, pitch, (unsigned long)(update_number & 0x0F));
  }
 } else return 2;
 return 0;
}
