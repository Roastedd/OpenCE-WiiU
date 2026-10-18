#version 330
in vec2 v_uv;
in vec4 v_col;
uniform sampler2D tex0;
layout(location = 0) out vec4 o_color;
void main()
{
	o_color = texture(tex0, v_uv) * v_col;
}
