/*
TICK_PRESENTATION.H

The particles, particle systems, contrails and decals are what the game shows (the render thread's presentation state,
updated every frame by game_frame), and the ticks make and delete them: a projectile's contrail, an object's particle
system, a bullet's decal, an effect's particles. With the tick owners' queues on (tick_owners.h) the ticks do not touch
those arrays: each make or delete is a request in a queue that the render thread applies at the start of its
frame (tick_owners_drain(TICK_OWNER_RENDER): the frame's particles_update, and the frame's draw), in the order the ticks
made them. (And the other way, the effects and material effects that the frame's particles_update makes, a particle that
dies or lands, are requests in a queue of particles.c's that the tick thread applies at the start of its pass.)

A contrail or a particle system that a tick makes is held by the object that has it (its attachment index, in the game
state), before the render thread has made it. What the tick holds is a ticket, a positive number; the render thread
keeps which datum each ticket became, and the tick's later ask (a delete, a point added) names the ticket.

Compiled for the Wii U only (HALO_WIIU).
*/

#ifndef __TICK_PRESENTATION_H
#define __TICK_PRESENTATION_H

#ifdef HALO_WIIU

struct new_particle_data;

/* the tick's side: each returns at once, with a ticket where the call returned an index */
void tick_presentation_particle_new(struct new_particle_data const *data);
void tick_presentation_particles_stop_on_first_person_weapon(short local_player_index);
long tick_presentation_particle_system_new_attached(long definition_index, long object_index, short attachment_index);
void tick_presentation_particle_system_orphan(long ticket);
long tick_presentation_contrail_new(long definition_index, long object_index, short attachment_index);
void tick_presentation_contrail_delete(long ticket);
void tick_presentation_contrail_owner_collision(long ticket, unsigned char object_dying, real dt);
void tick_presentation_decal_new(long definition_index, real_point3d const *origin, real_vector3d const *velocity,
	real radius_modifier, boolean permanent, short forced_sequence_index);

/* whether a ticket's datum is there (a contrail made and not gone) */
boolean tick_presentation_ticket_alive(long ticket);

#endif /* HALO_WIIU */

#endif /* __TICK_PRESENTATION_H */
