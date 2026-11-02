/*
TICK_PRESENTATION.C

The render thread's queue of what the ticks make and delete among the particles, particle systems, contrails and decals
(tick_presentation.h).
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "math/real_math.h"
#include "tick_owners.h"
#include "tick_presentation.h"

#ifdef HALO_WIIU

#include "effects/particles.h"
#include "objects/objects.h"

struct decal_editor_geometry;

/* ---------- prototypes */

/* each module's own, made when the render thread's turn comes */
void particle_new_now(struct new_particle_data const *data);
void particles_stop_on_first_person_weapon_now(short local_player_index);
long particle_system_new_attached_now(long definition_index, long object_index, short attachment_index);
void particle_system_orphan_if_alive(long system_index);
long contrail_new_now(long definition_index, long object_index, short attachment_index);
void contrail_delete_if_alive(long contrail_index);
void contrail_owner_collision_if_alive(long contrail_index, unsigned char object_dying, real dt);
void decal_new_now(long definition_index, real_point3d const *origin, real_vector3d const *velocity, real radius_modifier,
	boolean permanent, short forced_sequence_index, struct decal_editor_geometry *editor_geometry);

/* ---------- constants */

enum
{
	TICK_PRESENTATION_QUEUE_CAPACITY = 1024,
	TICK_PRESENTATION_TICKET_COUNT = 4096,
};

enum presentation_request_kind
{
	_presentation_particle_new,
	_presentation_particles_stop_on_first_person_weapon,
	_presentation_particle_system_new_attached,
	_presentation_particle_system_orphan,
	_presentation_contrail_new,
	_presentation_contrail_delete,
	_presentation_contrail_owner_collision,
	_presentation_decal_new,
};

enum presentation_ticket_state
{
	_presentation_ticket_free,
	_presentation_ticket_pending,
	_presentation_ticket_made,
	_presentation_ticket_failed,
};

/* ---------- structures */

struct presentation_request
{
	short kind;
	/* (a make's ticket, or the ticket a delete names) */
	long ticket;
	long definition_index;
	long object_index;
	short attachment_index;
	short local_player_index;
	boolean flag;
	real amount;
	real radius_modifier;
	short forced_sequence_index;
	real_point3d origin;
	real_vector3d velocity;
	struct new_particle_data particle;
};

struct presentation_ticket
{
	long ticket;
	long index;
	long state;
};

/* ---------- prototypes */

static void presentation_request_apply(
	void const *record);

/* ---------- globals */

TICK_QUEUE_DEFINE(presentation_queue, "presentation", TICK_OWNER_RENDER, struct presentation_request,
	TICK_PRESENTATION_QUEUE_CAPACITY, presentation_request_apply);

static struct presentation_ticket presentation_tickets[TICK_PRESENTATION_TICKET_COUNT];
static long presentation_ticket_counter = 0;

/* ---------- private code */

static long presentation_ticket_new(
	void)
{
	long ticket = __atomic_add_fetch(&presentation_ticket_counter, 1, __ATOMIC_RELAXED) & 0x7FFFFFFF;

	if (!ticket)
		ticket = __atomic_add_fetch(&presentation_ticket_counter, 1, __ATOMIC_RELAXED) & 0x7FFFFFFF;
	presentation_tickets[ticket % TICK_PRESENTATION_TICKET_COUNT].ticket = ticket;
	presentation_tickets[ticket % TICK_PRESENTATION_TICKET_COUNT].index = NONE;
	__atomic_store_n(&presentation_tickets[ticket % TICK_PRESENTATION_TICKET_COUNT].state, _presentation_ticket_pending,
		__ATOMIC_RELEASE);

	return ticket;
}

/* what a ticket became: its datum's index, or NONE if it is not (yet, or no more) a datum */
static long presentation_ticket_index(
	long ticket)
{
	struct presentation_ticket *entry;

	if (ticket <= 0)
		return NONE;
	entry = &presentation_tickets[ticket % TICK_PRESENTATION_TICKET_COUNT];

	return entry->ticket == ticket && __atomic_load_n(&entry->state, __ATOMIC_ACQUIRE) == _presentation_ticket_made ?
		entry->index : NONE;
}

static void presentation_ticket_set(
	long ticket,
	long index)
{
	struct presentation_ticket *entry = &presentation_tickets[ticket % TICK_PRESENTATION_TICKET_COUNT];

	if (entry->ticket == ticket)
	{
		entry->index = index;
		__atomic_store_n(&entry->state, index == NONE ? _presentation_ticket_failed : _presentation_ticket_made, __ATOMIC_RELEASE);
	}
}

static void presentation_request_push(
	struct presentation_request *request)
{
	tick_queue_push(&presentation_queue, request);
}

static void presentation_request_apply(
	void const *record)
{
	struct presentation_request const *request = (struct presentation_request const *)record;

	/* (what a request names may be gone by its turn: an object that a tick made a contrail or particles for, and deleted in the
	same pass, which is why the direct calls were made at once. Nothing is made for it.) */
	switch (request->kind)
	{
	case _presentation_particle_new:
		if (request->particle.object_index == NONE || request->particle.attached_to_local_player ||
			object_header_try_and_get(request->particle.object_index))
		{
			particle_new_now(&request->particle);
		}
		break;
	case _presentation_particles_stop_on_first_person_weapon:
		particles_stop_on_first_person_weapon_now(request->local_player_index);
		break;
	case _presentation_particle_system_new_attached:
		presentation_ticket_set(request->ticket,
			object_header_try_and_get(request->object_index) ?
				particle_system_new_attached_now(request->definition_index, request->object_index, request->attachment_index) : NONE);
		break;
	case _presentation_particle_system_orphan:
	{
		long index = presentation_ticket_index(request->ticket);

		if (index != NONE)
			particle_system_orphan_if_alive(index);
		break;
	}
	case _presentation_contrail_new:
		presentation_ticket_set(request->ticket,
			object_header_try_and_get(request->object_index) ?
				contrail_new_now(request->definition_index, request->object_index, request->attachment_index) : NONE);
		break;
	case _presentation_contrail_delete:
	{
		long index = presentation_ticket_index(request->ticket);

		if (index != NONE)
		{
			contrail_delete_if_alive(index);
			presentation_ticket_set(request->ticket, NONE);
		}
		break;
	}
	case _presentation_contrail_owner_collision:
	{
		long index = presentation_ticket_index(request->ticket);

		if (index != NONE)
			contrail_owner_collision_if_alive(index, request->flag, request->amount);
		break;
	}
	case _presentation_decal_new:
		decal_new_now(request->definition_index, &request->origin, &request->velocity, request->radius_modifier,
			request->flag, request->forced_sequence_index, NULL);
		break;
	}

	return;
}

/* ---------- public code */

void tick_presentation_particle_new(
	struct new_particle_data const *data)
{
	struct presentation_request request;

	csmemset(&request, 0, sizeof(request));
	request.kind = _presentation_particle_new;
	request.particle = *data;
	presentation_request_push(&request);
}

void tick_presentation_particles_stop_on_first_person_weapon(
	short local_player_index)
{
	struct presentation_request request;

	csmemset(&request, 0, sizeof(request));
	request.kind = _presentation_particles_stop_on_first_person_weapon;
	request.local_player_index = local_player_index;
	presentation_request_push(&request);
}

long tick_presentation_particle_system_new_attached(
	long definition_index,
	long object_index,
	short attachment_index)
{
	struct presentation_request request;

	csmemset(&request, 0, sizeof(request));
	request.kind = _presentation_particle_system_new_attached;
	request.ticket = presentation_ticket_new();
	request.definition_index = definition_index;
	request.object_index = object_index;
	request.attachment_index = attachment_index;
	presentation_request_push(&request);

	return request.ticket;
}

void tick_presentation_particle_system_orphan(
	long ticket)
{
	struct presentation_request request;

	csmemset(&request, 0, sizeof(request));
	request.kind = _presentation_particle_system_orphan;
	request.ticket = ticket;
	presentation_request_push(&request);
}

long tick_presentation_contrail_new(
	long definition_index,
	long object_index,
	short attachment_index)
{
	struct presentation_request request;

	csmemset(&request, 0, sizeof(request));
	request.kind = _presentation_contrail_new;
	request.ticket = presentation_ticket_new();
	request.definition_index = definition_index;
	request.object_index = object_index;
	request.attachment_index = attachment_index;
	presentation_request_push(&request);

	return request.ticket;
}

void tick_presentation_contrail_delete(
	long ticket)
{
	struct presentation_request request;

	csmemset(&request, 0, sizeof(request));
	request.kind = _presentation_contrail_delete;
	request.ticket = ticket;
	presentation_request_push(&request);
}

void tick_presentation_contrail_owner_collision(
	long ticket,
	unsigned char object_dying,
	real dt)
{
	struct presentation_request request;

	csmemset(&request, 0, sizeof(request));
	request.kind = _presentation_contrail_owner_collision;
	request.ticket = ticket;
	request.flag = object_dying;
	request.amount = dt;
	presentation_request_push(&request);
}

void tick_presentation_decal_new(
	long definition_index,
	real_point3d const *origin,
	real_vector3d const *velocity,
	real radius_modifier,
	boolean permanent,
	short forced_sequence_index)
{
	struct presentation_request request;

	csmemset(&request, 0, sizeof(request));
	request.kind = _presentation_decal_new;
	request.definition_index = definition_index;
	request.origin = *origin;
	request.velocity = *velocity;
	request.radius_modifier = radius_modifier;
	request.flag = permanent;
	request.forced_sequence_index = forced_sequence_index;
	presentation_request_push(&request);
}

boolean tick_presentation_ticket_alive(
	long ticket)
{
	return presentation_ticket_index(ticket) != NONE;
}

#else /* HALO_WIIU */

typedef int tick_presentation_not_for_this_platform;

#endif
