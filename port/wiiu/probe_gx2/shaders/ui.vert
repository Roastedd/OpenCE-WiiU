#version 330
// The probe's own drawing: a textured, tinted quad (pages, text layer, the texture tests).
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
out vec2 v_uv;
out vec4 v_col;
void main()
{
	gl_Position = vec4(a_pos, 0.0, 1.0);
	v_uv = a_uv;
	v_col = a_col;
}
