#version 330
// A NORMPACKED3 normal as the generator reads it (nv2a_vsh.c shader_prologue): one 32-bit integer attribute.
layout(location = 0) in vec2 a_pos;
layout(location = 1) in uint a_packed;
out vec4 v_val;
vec4 unpack_normpacked3(uint p)
{
	int x = int(p << 21) >> 21;
	int y = int(p << 10) >> 21;
	int z = int(p) >> 22;
	return vec4(float(x) / 1023.0, float(y) / 1023.0, float(z) / 511.0, 1.0);
}
void main()
{
	gl_Position = vec4(a_pos, 0.0, 1.0);
	v_val = unpack_normpacked3(a_packed);
}
