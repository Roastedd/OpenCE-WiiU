/* the record of a client's join (wiiu_join_trace.c) */
#ifndef WIIU_JOIN_TRACE_H
#define WIIU_JOIN_TRACE_H

void wiiu_join_step(char const *name);
void wiiu_join_frame_step(char const *name);
void wiiu_join_heartbeat(void);
int wiiu_join_window_open(void);
void wiiu_join_note(char const *text);

#endif
