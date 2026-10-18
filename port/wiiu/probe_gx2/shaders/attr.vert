#version 330
// The vertex stream tests: whatever the stream under test delivers in a_val comes out as the color.
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_val;
out vec4 v_val;
void main()
{
	gl_Position = vec4(a_pos, 0.0, 1.0);
	v_val = a_val;
}
