#ifndef CAMERA_STATUS_H
#define CAMERA_STATUS_H

/* Mode values follow director's following/orbiting/flying/editor/first-person enum. */
void camera_status_reset(void);
void camera_status_post(short local_player_index, short mode, int controls);
void camera_status_draw(void);

#endif
