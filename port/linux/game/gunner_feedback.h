#ifndef GUNNER_FEEDBACK_H
#define GUNNER_FEEDBACK_H
long gunner_view_weapon(short local_player_index,long *vehicle_index);
int gunner_feedback_take_particle(long object_index);
int gunner_feedback_take_contrail(long object_index);
int gunner_feedback_active(void);
void gunner_feedback_draw(void);
#endif
