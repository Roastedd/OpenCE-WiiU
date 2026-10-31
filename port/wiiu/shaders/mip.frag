#version 330
in vec2 v_uv;
uniform sampler2D tex0;
layout(location = 0) out vec4 o_color;
void main()
{
	ivec2 size = textureSize(tex0, 0);
	ivec2 output_size = max(size / 2, ivec2(1));
	ivec2 p = ivec2(v_uv * vec2(output_size)) * 2;
	ivec2 p1 = min(p + ivec2(1), size - ivec2(1));
	/* Reconstruct bytes before addition: integer sums up to 1020 are exact
	in a float, including the half-up ties of the CPU reference. */
	vec4 a = floor(texelFetch(tex0, p, 0) * 255.0 + 0.5);
	vec4 b = floor(texelFetch(tex0, ivec2(p1.x, p.y), 0) * 255.0 + 0.5);
	vec4 c = floor(texelFetch(tex0, ivec2(p.x, p1.y), 0) * 255.0 + 0.5);
	vec4 d = floor(texelFetch(tex0, p1, 0) * 255.0 + 0.5);
	o_color = floor((a + b + c + d + 2.0) * 0.25) / 255.0;
}
