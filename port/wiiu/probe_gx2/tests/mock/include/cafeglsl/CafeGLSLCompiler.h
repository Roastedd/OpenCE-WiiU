/*
A stand-in for the library's public header (CafeGLSL v1.0.0-rc1, <cafeglsl/CafeGLSLCompiler.h>) for the desktop test of the
stages: the same functions and types the probe calls, so that the stage code compiles unchanged against tests/mock/mock_gx2.c.
The real header is in the library's archive (tools/wiiu/fetch_cafeglsl.sh).
*/

#pragma once

#include <gx2/shaders.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GLSL_COMPILER_FLAG
{
	GLSL_COMPILER_FLAG_NONE = 0,
	GLSL_COMPILER_FLAG_PRINT_DISASSEMBLY_TO_STDERR = 1 << 0,
} GLSL_COMPILER_FLAG;

typedef enum GLSLCompileMode
{
	GLSL_COMPILE_AUTO = 0,
	GLSL_COMPILE_REGISTER = 1,
	GLSL_COMPILE_BLOCK = 2,
} GLSLCompileMode;

#define GLSL_SHADER_MODE_ERROR ((GX2ShaderMode)-1)

void InitGLSLCompiler(void);
void DestroyGLSLCompiler(void);
const char *GetGLSLCompilerVersion(void);
GX2ShaderMode CompileShaderPair(const char *vertexSource, const char *pixelSource, GLSLCompileMode requestedMode, GX2VertexShader **vertexOut,
	GX2PixelShader **pixelOut, char *infoLogOut, int infoLogMaxLength, GLSL_COMPILER_FLAG flags);
void FreeShaders(GX2VertexShader *vertex, GX2PixelShader *pixel);

#ifdef __cplusplus
}
#endif
