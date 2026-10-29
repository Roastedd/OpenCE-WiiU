/* Frozen pre-optimization conversion/emission oracle.
   Function SHA-256: c386ef3d2c7df8861993fea41aa3e314d67693544bcc3c988e89f80bc212d999
   Inputs and conversion expressions are preserved verbatim. */
static void draw_parameters_send(float texture_scale[4][4])
{
	DWORD inputs[DRAW_UNIFORM_INPUT_COUNT];
	unsigned long count = 0;
	int stage;

	memcpy(&inputs[count], device.viewport_scale, sizeof(device.viewport_scale));
	count += 4;
	memcpy(&inputs[count], device.viewport_offset, sizeof(device.viewport_offset));
	count += 4;
	memcpy(&inputs[count], texture_scale, 16 * sizeof(float));
	count += 16;
	inputs[count++] = D3D__RenderState[D3DRS_POINTSIZE];
	for (stage = 0; stage < 8; stage++)
	{
		inputs[count++] = D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage];
		inputs[count++] = D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage];
	}
	inputs[count++] = D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0];
	inputs[count++] = D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1];
	inputs[count++] = D3D__RenderState[D3DRS_FOGCOLOR];
	inputs[count++] = D3D__RenderState[D3DRS_FOGSTART];
	inputs[count++] = D3D__RenderState[D3DRS_FOGEND];
	inputs[count++] = D3D__RenderState[D3DRS_FOGDENSITY];
	inputs[count++] = D3D__RenderState[D3DRS_ALPHAREF];
	inputs[count++] = (DWORD)UI_OFFSET;
	memcpy(&inputs[count], texture_clip, 3 * sizeof(float));
	count += 3;
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		DWORD *state = D3D__TextureState[stage];

		inputs[count++] = state[D3DTSS_BUMPENVMAT00];
		inputs[count++] = state[D3DTSS_BUMPENVMAT01];
		inputs[count++] = state[D3DTSS_BUMPENVMAT10];
		inputs[count++] = state[D3DTSS_BUMPENVMAT11];
		inputs[count++] = state[D3DTSS_BUMPENVLSCALE];
		inputs[count++] = state[D3DTSS_BUMPENVLOFFSET];
	}
	if (draw_uniform_inputs_known && words_equal(inputs, draw_uniform_inputs, sizeof(inputs)))
		return;
	memcpy(draw_uniform_inputs, inputs, sizeof(inputs));
	draw_uniform_inputs_known = TRUE;
	{
		struct dk_vertex_parameters vertex;
		struct dk_pixel_parameters pixel;

		memset(&vertex, 0, sizeof(vertex));
		memset(&pixel, 0, sizeof(pixel));
		memcpy(vertex.viewport_scale, device.viewport_scale, sizeof(vertex.viewport_scale));
		memcpy(vertex.viewport_offset, device.viewport_offset, sizeof(vertex.viewport_offset));
		vertex.point_and_screen[0] = D3D__RenderState[D3DRS_POINTSIZE] ?
			dword_to_float(D3D__RenderState[D3DRS_POINTSIZE]) : 1.0f;
		vertex.point_and_screen[1] = (float)UI_OFFSET;


		for (stage = 0; stage < 8; stage++)
		{
			color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage], pixel.ps_c0[stage]);
			color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage], pixel.ps_c1[stage]);
		}
		color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0], pixel.ps_final_c0);
		color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1], pixel.ps_final_c1);
		color_to_vec4(D3D__RenderState[D3DRS_FOGCOLOR], pixel.fog_color);
		pixel.fog_parameters[0] = dword_to_float(D3D__RenderState[D3DRS_FOGSTART]);
		pixel.fog_parameters[1] = dword_to_float(D3D__RenderState[D3DRS_FOGEND]);
		pixel.fog_parameters[2] = dword_to_float(D3D__RenderState[D3DRS_FOGDENSITY]);
		pixel.alpha_reference[0] = (float)(D3D__RenderState[D3DRS_ALPHAREF] & 0xff);
		/* (the clip, which the pixel shader makes before anything else: its
		limits in the units the stage is sampled in, 0 to 1) */
		pixel.alpha_reference[1] = texture_clip[0];
		pixel.alpha_reference[2] = texture_clip[1];
		pixel.alpha_reference[3] = texture_clip[2];
		memcpy(pixel.texture_scale, texture_scale, sizeof(pixel.texture_scale));
		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		{
			DWORD *state = D3D__TextureState[stage];

			pixel.bump_matrix[stage][0] = dword_to_float(state[D3DTSS_BUMPENVMAT00]);
			pixel.bump_matrix[stage][1] = dword_to_float(state[D3DTSS_BUMPENVMAT01]);
			pixel.bump_matrix[stage][2] = dword_to_float(state[D3DTSS_BUMPENVMAT10]);
			pixel.bump_matrix[stage][3] = dword_to_float(state[D3DTSS_BUMPENVMAT11]);
			pixel.bump_luminance[stage][0] = dword_to_float(state[D3DTSS_BUMPENVLSCALE]);
			pixel.bump_luminance[stage][1] = dword_to_float(state[D3DTSS_BUMPENVLOFFSET]);
		}
		if (!parameters_known || !words_equal(&vertex, &vertex_parameters_told, sizeof(vertex)))
		{
			struct dk_command_vertex_parameters *command = stream_command(DK_COMMAND_VERTEX_PARAMETERS, sizeof(*command));

			command->parameters = vertex;
			vertex_parameters_told = vertex;
		}
		if (!parameters_known || !words_equal(&pixel, &pixel_parameters_told, sizeof(pixel)))
		{
			struct dk_command_pixel_parameters *command = stream_command(DK_COMMAND_PIXEL_PARAMETERS, sizeof(*command));

			command->parameters = pixel;
			pixel_parameters_told = pixel;
		}
		parameters_known = TRUE;
	}
}
