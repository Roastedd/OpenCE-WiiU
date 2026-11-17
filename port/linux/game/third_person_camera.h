#ifndef PORT_THIRD_PERSON_CAMERA_H
#define PORT_THIRD_PERSON_CAMERA_H
#include "cseries.h"
#include "math/real_math.h"
struct camera_command;
int third_person_camera_aim_ray(short local_player_index,real_point3d *origin,real_vector3d *direction);
int third_person_camera_available(short local_player_index);
int third_person_camera_scoped(short local_player_index);
void third_person_camera_adjust(short local_player_index,struct camera_command *command);
#endif
