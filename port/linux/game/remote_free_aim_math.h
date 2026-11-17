#ifndef REMOTE_FREE_AIM_MATH_H
#define REMOTE_FREE_AIM_MATH_H

#include <math.h>
#include <float.h>

struct remote_free_aim_state
{
	float yaw, pitch;
};

/* This target's C library has no callable isfinite. Ordered comparisons
reject NaNs and infinities without a runtime helper. */
static inline int remote_free_aim_finite(float value)
{
	return value >= -FLT_MAX && value <= FLT_MAX;
}

static inline float remote_free_aim_axis(float *offset, float movement, float limit,
	float speed, float seconds)
{
	float edge, magnitude;
	*offset = fmaxf(-limit, fminf(limit, *offset + movement));
	magnitude = fabsf(*offset);
	edge = fmaxf(0.0f, (magnitude / limit - 0.55f) / 0.45f);
	/* Camera turning accelerates smoothly through the outer 45% of the box.
	The offset stays bounded; movement beyond it goes straight to the camera. */
	return movement + copysignf(speed * edge * edge * (3.0f - 2.0f * edge) * seconds, *offset);
}

static inline void remote_free_aim_step(struct remote_free_aim_state *state,
	float *yaw, float *pitch, float limit, float vertical_limit, float speed, float seconds)
{
	if (!remote_free_aim_finite(*yaw) || !remote_free_aim_finite(*pitch) || !remote_free_aim_finite(seconds) ||
		!remote_free_aim_finite(vertical_limit) || vertical_limit <= 0.0f || !remote_free_aim_finite(limit) || !remote_free_aim_finite(speed) || limit <= 0.0f || speed < 0.0f || seconds < 0.0f)
	{
		*yaw = *pitch = 0.0f;
		return;
	}
	seconds = fminf(seconds, 0.1f);
	*yaw = remote_free_aim_axis(&state->yaw, *yaw, limit, speed, seconds);
	*pitch = remote_free_aim_axis(&state->pitch, *pitch, vertical_limit, speed, seconds);
}
#endif
