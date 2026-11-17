#ifndef REMOTE_IR_AIM_MATH_H
#define REMOTE_IR_AIM_MATH_H

#include "remote_free_aim_math.h"

#define REMOTE_IR_SAMPLES 8
struct remote_ir_filter
{
	float x[REMOTE_IR_SAMPLES], y[REMOTE_IR_SAMPLES];
	unsigned next, count;
};

static inline void remote_ir_filter_sample(struct remote_ir_filter *filter,
	float *x, float *y, unsigned samples, float falloff)
{
	float total = 0.0f, weight = 1.0f, sum_x = 0.0f, sum_y = 0.0f;
	unsigned i;
	if (samples < 1) samples = 1;
	if (samples > REMOTE_IR_SAMPLES) samples = REMOTE_IR_SAMPLES;
	filter->x[filter->next] = *x;
	filter->y[filter->next] = *y;
	filter->next = (filter->next + 1) % REMOTE_IR_SAMPLES;
	if (filter->count < REMOTE_IR_SAMPLES) filter->count++;
	if (samples > filter->count) samples = filter->count;
	for (i = 0; i < samples; i++)
	{
		const unsigned slot = (filter->next + REMOTE_IR_SAMPLES - 1 - i) % REMOTE_IR_SAMPLES;
		sum_x += filter->x[slot] * weight;
		sum_y += filter->y[slot] * weight;
		total += weight;
		weight *= falloff;
	}
	*x = sum_x / total;
	*y = sum_y / total;
}

static inline float remote_ir_angle_difference(float angle)
{
	const float pi = 3.14159265358979323846f;
	if (angle > pi) angle -= 2.0f * pi;
	if (angle < -pi) angle += 2.0f * pi;
	return angle;
}

static inline float remote_ir_turn(float position, float deadzone, float speed, float seconds)
{
	const float magnitude = fabsf(position);
	float outside;
	deadzone = fminf(0.95f, fmaxf(0.0f, deadzone));
	if (magnitude <= deadzone)
		return 0.0f;
	outside = fminf(1.0f, (magnitude - deadzone) / (1.0f - deadzone));
	/* A normalized ramp gives independent fine aim and camera turning. */
	return -copysignf(speed * outside * outside * seconds, position);
}

/* Centered vehicle camera: gentle onset, then bounded sustained turning.
 * The Nunchuk and Halo's vehicle steering remain unchanged. */
static inline void remote_ir_vehicle_step(float x, float y, float speed, float seconds,
    float *yaw, float *pitch)
{
    *yaw = *pitch = 0.0f;
    if (!remote_free_aim_finite(x) || !remote_free_aim_finite(y) ||
        !remote_free_aim_finite(speed) || !remote_free_aim_finite(seconds) ||
        speed <= 0.0f || seconds <= 0.0f)
        return;
    seconds = fminf(seconds, 0.1f);
    *yaw = remote_ir_turn(x, 0.15f, speed, seconds);
    *pitch = remote_ir_turn(y, 0.20f, speed * 0.65f, seconds);
}

/* Centered scopes use angular speed divided by optical magnification.
 * This path supplies its own zoom scaling after the game's mouse pre-scale. */
static inline void remote_ir_scope_step(float x, float y, float speed, float sensitivity,
    float magnification, float seconds, float *yaw, float *pitch)
{
    *yaw = *pitch = 0;
    if (!remote_free_aim_finite(magnification) || magnification < 1 ||
        !remote_free_aim_finite(sensitivity) || sensitivity <= 0 ||
        !remote_free_aim_finite(seconds) || seconds <= 0 ||
        !remote_free_aim_finite(x) || !remote_free_aim_finite(y) ||
        !remote_free_aim_finite(speed) || speed <= 0) return;
    seconds = fminf(seconds, 0.1f);
    *yaw = remote_ir_turn(x, 0.08f, speed * sensitivity / magnification, seconds);
    *pitch = remote_ir_turn(y, 0.08f, speed * sensitivity / magnification, seconds);
}

/* Sensor coordinates are right/down positive. Angles are Halo's world
yaw/pitch, with left/up positive. Only the resulting aim delta enters gameplay. */
static inline void remote_ir_aim_step(struct remote_free_aim_state *offset,
	float x, float y, float aim_yaw, float aim_pitch, float tangent_x, float tangent_y,
	float box_yaw, float box_pitch, float speed, float seconds, float *yaw, float *pitch)
{
	const float pi = 3.14159265358979323846f;
	float camera_yaw, camera_pitch, cy, sy, cp, sp;
	float horizontal, vertical, horizontal_depth, dx, dy, dz, target_yaw, target_pitch;
	*yaw = *pitch = 0.0f;
	if (!remote_free_aim_finite(x) || !remote_free_aim_finite(y) ||
		!remote_free_aim_finite(aim_yaw) || !remote_free_aim_finite(aim_pitch) ||
		!(tangent_x > 0.0f && tangent_y > 0.0f && seconds > 0.0f))
		return;
	seconds = fminf(seconds, 0.1f);
	x = fmaxf(-1.0f, fminf(1.0f, x));
	y = fmaxf(-1.0f, fminf(1.0f, y));
	camera_yaw = aim_yaw - offset->yaw;
	camera_pitch = aim_pitch - offset->pitch;
	cy = cosf(camera_yaw); sy = sinf(camera_yaw);
	cp = cosf(camera_pitch); sp = sinf(camera_pitch);
	horizontal = -x * tangent_x;
	vertical = -y * tangent_y;
	/* Do not flip yaw when the pointer crosses above the pitch limit. */
	horizontal_depth = fmaxf(0.001f, cp - vertical * sp);
	dx = horizontal_depth * cy - horizontal * sy;
	dy = horizontal_depth * sy + horizontal * cy;
	dz = sp + vertical * cp;
	target_yaw = atan2f(dy, dx);
	target_pitch = atan2f(dz, sqrtf(dx * dx + dy * dy));
	target_pitch = fminf(pi * 0.475f, fmaxf(-pi * 0.475f, target_pitch));
	offset->yaw = remote_ir_angle_difference(target_yaw - camera_yaw);
	offset->pitch = target_pitch - camera_pitch;
	*yaw = remote_ir_angle_difference(target_yaw - aim_yaw) +
		remote_ir_turn(x, tanf(box_yaw) / tangent_x, speed, seconds);
	*pitch = target_pitch - aim_pitch +
		remote_ir_turn(y, tanf(box_pitch) / tangent_y, speed, seconds);
	*pitch = fminf(pi * 0.475f, fmaxf(-pi * 0.475f, aim_pitch + *pitch)) - aim_pitch;
}
#endif
