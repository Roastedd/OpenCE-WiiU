#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler3D;
precision highp samplerCube;
in vec4 xD0;
in vec4 xD1;
in vec4 xB0;
in vec4 xB1;
in vec4 xT0;
in vec4 xT1;
in vec4 xT2;
in vec4 xT3;
in float xFog;
layout(location = 0) out vec4 fragment_color;
uniform vec4 ps_c0[8];
uniform vec4 ps_c1[8];
uniform vec4 ps_final_c0;
uniform vec4 ps_final_c1;
uniform vec4 fog_color;
uniform vec4 fog_parameters;
uniform float alpha_reference;
uniform vec4 bump_matrix[4];
uniform vec4 bump_luminance[4];
uniform vec4 texture_scale[4];
uniform vec4 texture_lod_bias;
uniform sampler2D tex0;
uniform sampler3D tex1;
uniform sampler2D tex2;
uniform sampler2D tex3;
uniform vec4 model_lights[12];
in vec4 xWorldNormal;
vec3 model_lighting()
{
	vec3 n = xWorldNormal.xyz * (xWorldNormal.w * inversesqrt(max(dot(xWorldNormal.xyz, xWorldNormal.xyz), 1.0e-24)));
	float facing = dot(n, -model_lights[7].xyz);
	vec3 light = model_lights[11].xyz + max(max(facing, -facing * model_lights[0].z), 0.0) * model_lights[8].xyz +
		max(dot(n, -model_lights[9].xyz), 0.0) * model_lights[10].xyz;
	return clamp(light, 0.0, 1.0);
}
float signed_byte(float x)
{
	float b = floor(x * 255.0 + 0.5);
	return (b >= 128.0 ? b - 256.0 : b) / 127.0;
}
vec3 signed_bytes(vec3 x)
{
	return vec3(signed_byte(x.r), signed_byte(x.g), signed_byte(x.b));
}
void main()
{
	vec4 v0 = xD0;
	vec4 v1 = xD1;
	vec4 t0 = vec4(0.0), t1 = vec4(0.0), t2 = vec4(0.0), t3 = vec4(0.0);
	float dot0 = 0.0, dot1 = 0.0, dot2 = 0.0, dot3 = 0.0;
	v0.rgb = model_lighting();
	/* texture stage 0, mode 1 */
	t0 = texture(tex0, (vec4(xT0.xyz / (xT0.w != 0.0 ? xT0.w : 1.0), 1.0)).xy * texture_scale[0].xy, texture_lod_bias[0]);
	/* texture stage 1, mode 1 */
	t1 = texture(tex1, (vec4(xT1.xyz / (xT1.w != 0.0 ? xT1.w : 1.0), 1.0)).xyz, texture_lod_bias[1]);
	/* texture stage 2, mode 0 */
	t2 = vec4(0.0);
	/* texture stage 3, mode 0 */
	t3 = vec4(0.0);
	float fog_factor = (fog_parameters.y - xFog) / max(fog_parameters.y - fog_parameters.x, 1.0e-6);
	vec4 fog = vec4(fog_color.rgb, clamp(fog_factor, 0.0, 1.0));
	vec4 r0 = vec4(0.0, 0.0, 0.0, t0.a);
	vec4 r1 = vec4(0.0);
	/* combiner stage 0 */
	{
		vec3 cA = max(t0.rgb, 0.0);
		vec3 cB = max(ps_c0[0].rgb, 0.0);
		vec3 cC = max(t1.rgb, 0.0);
		vec3 cD = max(ps_c1[0].rgb, 0.0);
		vec3 cAB = cA * cB;
		vec3 cCD = cC * cD;
		vec3 cSUM = cAB + cCD;
		cAB = clamp((cAB), -1.0, 1.0);
		cCD = clamp((cCD), -1.0, 1.0);
		cSUM = clamp((cSUM), -1.0, 1.0);
		float aA = max(t0.a, 0.0);
		float aB = max(ps_c0[0].a, 0.0);
		float aC = max(t1.a, 0.0);
		float aD = max(ps_c1[0].a, 0.0);
		float aAB = aA * aB;
		float aCD = aC * aD;
		float aSUM = aAB + aCD;
		aAB = clamp((aAB), -1.0, 1.0);
		aCD = clamp((aCD), -1.0, 1.0);
		aSUM = clamp((aSUM), -1.0, 1.0);
		t0.rgb = cAB;
		t1.rgb = cCD;
		t0.a = aAB;
		t1.a = aCD;
	}
	/* combiner stage 1 */
	{
		vec3 cA = max(t2.rgb, 0.0);
		vec3 cB = max(ps_c0[1].rgb, 0.0);
		vec3 cC = max(t0.rgb, 0.0);
		vec3 cD = max(v0.rgb, 0.0);
		vec3 cAB = cA * cB;
		vec3 cCD = cC * cD;
		vec3 cSUM = cAB + cCD;
		cAB = clamp((cAB), -1.0, 1.0);
		cCD = clamp((cCD), -1.0, 1.0);
		cSUM = clamp((cSUM), -1.0, 1.0);
		float aA = max(t2.a, 0.0);
		float aB = max(ps_c0[1].a, 0.0);
		float aC = max(t0.a, 0.0);
		float aD = max(v0.a, 0.0);
		float aAB = aA * aB;
		float aCD = aC * aD;
		float aSUM = aAB + aCD;
		aAB = clamp((aAB), -1.0, 1.0);
		aCD = clamp((aCD), -1.0, 1.0);
		aSUM = clamp((aSUM), -1.0, 1.0);
		t2.rgb = cAB;
		r0.rgb = cCD;
		t2.a = aAB;
		r0.a = aCD;
	}
	vec4 ef_product = vec4(clamp(vec4(0.0).rgb, 0.0, 1.0) * clamp(vec4(0.0).rgb, 0.0, 1.0), 0.0);
	vec4 v1r0_sum = vec4(clamp(v1.rgb, 0.0, 1.0) + clamp(r0.rgb, 0.0, 1.0), 0.0);
	vec3 fA = clamp(vec4(0.0).rgb, 0.0, 1.0);
	vec3 fB = clamp(vec4(0.0).rgb, 0.0, 1.0);
	vec3 fC = clamp(vec4(0.0).rgb, 0.0, 1.0);
	vec3 fD = clamp(r0.rgb, 0.0, 1.0);
	float fG = clamp(r0.a, 0.0, 1.0);
	vec4 result = vec4(fA * fB + (1.0 - fA) * fC + fD, fG);
	if (!(floor(clamp(result.a, 0.0, 1.0) * 255.0 + 0.5) > alpha_reference)) discard;
	fragment_color = clamp(result, 0.0, 1.0);
}
