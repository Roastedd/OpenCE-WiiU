#version 330
in vec4 v_val;
layout(location = 0) out vec4 o_color;
void main()
{
	// -2..2 shown as 0..1, so that signed values can be seen
	o_color = clamp(v_val * 0.25 + 0.5, 0.0, 1.0);
}
