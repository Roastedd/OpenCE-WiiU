#version 300 es
precision highp float;
precision highp int;
uniform vec4 c[192];
uniform vec4 viewport_scale;
uniform vec4 viewport_offset;
uniform float point_size;
uniform float screen_offset;
out vec4 xD0;
out vec4 xD1;
out vec4 xB0;
out vec4 xB1;
out vec4 xT0;
out vec4 xT1;
out vec4 xT2;
out vec4 xT3;
out float xFog;
invariant gl_Position;
vec4 unpack_normpacked3(uint p)
{
	int x = int(p << 21) >> 21;
	int y = int(p << 10) >> 21;
	int z = int(p) >> 22;
	return vec4(float(x) / 1023.0, float(y) / 1023.0, float(z) / 511.0, 1.0);
}
vec4 nv2a_rcc(float x)
{
	float r = 1.0 / x;
	if (r > 0.0) r = clamp(r, 5.42101e-20, 1.884467e+19);
	else r = clamp(r, -1.884467e+19, -5.42101e-20);
	return vec4(r);
}
vec4 nv2a_exp(float x)
{
	return vec4(exp2(floor(x)), fract(x), exp2(x), 1.0);
}
vec4 nv2a_log(float x)
{
	x = abs(x);
	if (x == 0.0) return vec4(-1.0e30, 1.0, -1.0e30, 1.0);
	float e = floor(log2(x));
	return vec4(e, x / exp2(e), log2(x), 1.0);
}
vec4 nv2a_lit(vec4 s)
{
	float specular = s.x > 0.0 ? pow(max(s.y, 0.0), clamp(s.w, -127.9961, 127.9961)) : 0.0;
	return vec4(1.0, max(s.x, 0.0), specular, 1.0);
}
layout(location = 0) in vec4 v0_in;
layout(location = 1) in vec4 v1_in;
layout(location = 2) in vec4 v2_in;
layout(location = 3) in vec4 v3_in;
layout(location = 4) in vec4 v4_in;
layout(location = 5) in vec4 v5_in;
layout(location = 6) in vec4 v6_in;
layout(location = 7) in vec4 v7_in;
layout(location = 8) in vec4 v8_in;
layout(location = 9) in vec4 v9_in;
layout(location = 10) in vec4 v10_in;
layout(location = 11) in vec4 v11_in;
layout(location = 12) in vec4 v12_in;
layout(location = 13) in vec4 v13_in;
layout(location = 14) in vec4 v14_in;
layout(location = 15) in vec4 v15_in;
void main()
{
	vec4 v0 = v0_in;
	vec4 v1 = v1_in;
	vec4 v2 = v2_in;
	vec4 v3 = v3_in;
	vec4 v4 = v4_in;
	vec4 v5 = v5_in;
	vec4 v6 = v6_in;
	vec4 v7 = v7_in;
	vec4 v8 = v8_in;
	vec4 v9 = v9_in;
	vec4 v10 = v10_in;
	vec4 v11 = v11_in;
	vec4 v12 = v12_in;
	vec4 v13 = v13_in;
	vec4 v14 = v14_in;
	vec4 v15 = v15_in;
	vec4 r0 = vec4(0.0), r1 = vec4(0.0), r2 = vec4(0.0), r3 = vec4(0.0);
	vec4 r4 = vec4(0.0), r5 = vec4(0.0), r6 = vec4(0.0), r7 = vec4(0.0);
	vec4 r8 = vec4(0.0), r9 = vec4(0.0), r10 = vec4(0.0), r11 = vec4(0.0);
	vec4 oPos = vec4(0.0, 0.0, 0.0, 1.0);
	vec4 oD0 = vec4(0.0, 0.0, 0.0, 1.0), oD1 = vec4(0.0, 0.0, 0.0, 1.0);
	vec4 oB0 = vec4(0.0, 0.0, 0.0, 1.0), oB1 = vec4(0.0, 0.0, 0.0, 1.0);
	vec4 oT0 = vec4(0.0, 0.0, 0.0, 1.0), oT1 = vec4(0.0, 0.0, 0.0, 1.0);
	vec4 oT2 = vec4(0.0, 0.0, 0.0, 1.0), oT3 = vec4(0.0, 0.0, 0.0, 1.0);
	vec4 oFog = vec4(1.0), oPts = vec4(point_size), oUnused = vec4(0.0);
	int a0 = 0;
	vec4 A, B, C, mac, ilu;
	vec4 clip_position = vec4(0.0);
	bool clip_captured = false;
	/* 0 */
	A = v0.xyzw;
	B = c[0].xyzw;
	C = v0.xyzw;
	mac = vec4(dot(A.xyz, B.xyz) + B.w);
	oPos.x = mac.x;
	/* 1 */
	A = v0.xyzw;
	B = c[1].xyzw;
	C = v0.xyzw;
	mac = vec4(dot(A.xyz, B.xyz) + B.w);
	oPos.y = mac.y;
	/* 2 */
	A = v0.xyzw;
	B = c[2].xyzw;
	C = v0.xyzw;
	mac = vec4(dot(A.xyz, B.xyz) + B.w);
	oPos.z = mac.z;
	/* 3 */
	A = v0.xyzw;
	B = c[3].xyzw;
	C = v0.xyzw;
	mac = vec4(dot(A.xyz, B.xyz) + B.w);
	oPos.w = mac.w;
	/* 4 */
	A = v9.xyzw;
	B = v9.xyzw;
	C = v9.xyzw;
	mac = A;
	oD0.xyzw = mac.xyzw;
	/* 5 */
	A = oPos.xyzw;
	B = c[58].xyzw;
	C = oPos.wwww;
	mac = A * B;
	ilu = nv2a_rcc(C.x);
	clip_position = oPos;
	clip_captured = true;
	r1.x = ilu.x;
	oPos.xyz = mac.xyz;
	/* 6 */
	A = oPos.xyzw;
	B = r1.xxxx;
	C = c[59].xyzw;
	mac = A * B + C;
	oPos.xyz = mac.xyz;
	/* undo the screen-space conversion done with c[-38] and c[-37] */
	vec3 scale = vec3(viewport_scale.x != 0.0 ? viewport_scale.x : 1.0,
		viewport_scale.y != 0.0 ? viewport_scale.y : 1.0,
		viewport_scale.z != 0.0 ? viewport_scale.z : 1.0);
	vec4 position;
	if (clip_captured)
		position = vec4((clip_position.xyz * c[58].xyz + (c[59].xyz + vec3(0.5 + screen_offset, 0.5, 0.0)
			- viewport_offset.xyz) * clip_position.w) / scale, clip_position.w);
	else
	{
		vec3 ndc = (vec3(oPos.xy + vec2(0.5 + screen_offset, 0.5), oPos.z) - viewport_offset.xyz) / scale;
		position = vec4(ndc * oPos.w, oPos.w);
	}
	if (!(abs(position.w) > 0.0))
		position = vec4(0.0, 0.0, 0.0, -1.0);
	gl_Position = position;
	gl_Position.y = -gl_Position.y;
	gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;
	gl_PointSize = oPts.x;
	xD0 = clamp(oD0, 0.0, 1.0);
	xD1 = clamp(oD1, 0.0, 1.0);
	xB0 = clamp(oB0, 0.0, 1.0);
	xB1 = clamp(oB1, 0.0, 1.0);
	xT0 = oT0;
	xT1 = oT1;
	xT2 = oT2;
	xT3 = oT3;
	xFog = oFog.x;
}
