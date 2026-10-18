#include "iface_check.h"

int iface_output_index(const uint32_t *vs_out_id, unsigned vs_words, unsigned id)
{
	unsigned i;

	for (i = 0; i < vs_words * 4; i++)
	{
		unsigned found = (vs_out_id[i / 4] >> (8 * (i % 4))) & 0xFF;

		if (found == id && found != 0xFF)
			return (int)i;
	}
	return -1;
}

unsigned iface_missing(const uint32_t *vs_out_id, unsigned vs_words, const uint32_t *ps_input_cntl, unsigned ps_inputs, int *first_missing)
{
	unsigned i, missing = 0;

	*first_missing = -1;
	for (i = 0; i < ps_inputs; i++)
	{
		unsigned id = ps_input_cntl[i] & 0xFF;

		if (iface_output_index(vs_out_id, vs_words, id) < 0)
		{
			if (!missing)
				*first_missing = (int)id;
			missing++;
		}
	}
	return missing;
}
