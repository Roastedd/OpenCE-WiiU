#ifndef WIIU_RESAMPLER_PS_H
#define WIIU_RESAMPLER_PS_H

/* Sole audio worker only. The first begin fixes the decision until exit;
   a failed readiness check cannot be retried by calling begin again. Zero
   leaves the backend disabled without executing GQR or paired instructions.
   Native qualification must cover FPSCR mode 4 before requesting activation. */
int wiiu_resampler_ps_worker_begin(int requested);
int wiiu_resampler_ps_check_chunk(void);
int wiiu_resampler_ps_ready(void);

enum wiiu_resampler_ps_result
{
	WIIU_RESAMPLER_PS_DISABLED,
	WIIU_RESAMPLER_PS_READY,
	WIIU_RESAMPLER_PS_WRONG_CORE,
	WIIU_RESAMPLER_PS_FPSCR_MODE,
	WIIU_RESAMPLER_PS_GQR_TYPE,
	WIIU_RESAMPLER_PS_SELFTEST
};

struct wiiu_resampler_ps_status
{
	unsigned result, core, fpscr, gqr0, selftest_cases, failed_case;
};

/* Consume one initial result and, if it occurs, one permanent-disable result.
   The caller may log this outside render; this module performs no I/O. */
int wiiu_resampler_ps_take_report(struct wiiu_resampler_ps_status *status);
const char *wiiu_resampler_ps_reason(unsigned result);

/* Exactly 48 valid floats per input, all pointers four-byte aligned; outputs
   must not alias inputs. Stereo writes two consecutive floats. Inputs may
   alias one another. Use only after worker/chunk readiness succeeds. The
   already-rounded blend is passed by pointer, without a float argument ABI. */
void wiiu_resampler_ps_dot48(const float *frames, const float *weights, float *out);
void wiiu_resampler_ps_dot48_stereo(const float *left, const float *right,
	const float *weights, float *out_stereo);
void wiiu_resampler_ps_blended48(const float *frames, const float *weights,
	const float *deltas, const float *blend, float *out);
void wiiu_resampler_ps_blended48_stereo(const float *left, const float *right,
	const float *weights, const float *deltas, const float *blend, float *out_stereo);

#endif
