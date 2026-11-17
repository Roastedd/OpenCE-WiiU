/* the record of a client's join (wiiu_join_trace.c) */
#ifndef WIIU_JOIN_TRACE_H
#define WIIU_JOIN_TRACE_H

void wiiu_join_step(char const *name);
void wiiu_join_frame_step(char const *name);
void wiiu_join_frame_stepf(char const *format, ...);
void wiiu_join_heartbeat(void);
int wiiu_join_window_open(void);
void wiiu_join_note(char const *text);
void wiiu_join_render_frame_begin(void);
int wiiu_join_draw_trace_on(void);
void wiiu_join_draw_line(char const *text);
void wiiu_join_pass_note(int pass);
void wiiu_join_safe_arm(void);
int wiiu_join_safe_window(void);

#endif
