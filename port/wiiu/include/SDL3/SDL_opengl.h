/*
SDL3/SDL_opengl.h, as the Wii U build has it: the names port/linux/src/gl.h and xgpu.h need to be read, and none of OpenGL.

The Wii U renderer is GX2 (d3d8_gx2.c); xgpu.h, which the Xbox-facing units share with the OpenGL renderer, includes gl.h, which includes
SDL's OpenGL header. This stands in for it: the types, the two enumerants the shared code names, and a declaration of each function gl.h
lists (so that `__typeof__(&glName)` has a name to take the type of); nothing here is ever called. Generated from gl.h's desktop list.
*/

#ifndef HALO_WIIU_GL_SHIM_H
#define HALO_WIIU_GL_SHIM_H

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef char GLchar;
typedef long GLintptr;
typedef long GLsizeiptr;
typedef long long GLint64;
typedef unsigned long long GLuint64;
typedef struct __GLsync *GLsync;
typedef void (*GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam);
typedef GLDEBUGPROC GLDEBUGPROCARB;

#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA8 0x8058
#define GL_ANY_SAMPLES_PASSED 0x8C2F
#define GL_SAMPLES_PASSED 0x8914

/* (no prototypes: these are never called) */
void glGetString();
void glGetIntegerv();
void glGetTexImage();
void glCopyImageSubData();
void glGenerateMipmap();
void glGetError();
void glEnable();
void glDisable();
void glViewport();
void glDepthRange();
void glScissor();
void glClearColor();
void glClearDepth();
void glClearStencil();
void glClear();
void glColorMask();
void glDepthMask();
void glDepthFunc();
void glStencilFunc();
void glStencilOp();
void glStencilMask();
void glBlendFunc();
void glBlendEquation();
void glBlendColor();
void glCullFace();
void glFrontFace();
void glPolygonMode();
void glPolygonOffset();
void glLineWidth();
void glPixelStorei();
void glReadPixels();
void glFinish();
void glFlush();
void glClipControl();
void glGenTextures();
void glDeleteTextures();
void glBindTexture();
void glActiveTexture();
void glTexImage2D();
void glTexImage3D();
void glTexSubImage2D();
void glCompressedTexImage2D();
void glCompressedTexImage3D();
void glTexParameteri();
void glTexParameteriv();
void glTexParameterf();
void glTexParameterfv();
void glGenSamplers();
void glBindSampler();
void glSamplerParameteri();
void glSamplerParameterf();
void glSamplerParameterfv();
void glGenFramebuffers();
void glDeleteFramebuffers();
void glBindFramebuffer();
void glFramebufferTexture2D();
void glCheckFramebufferStatus();
void glBlitFramebuffer();
void glDrawBuffers();
void glGenRenderbuffers();
void glDeleteRenderbuffers();
void glBindRenderbuffer();
void glRenderbufferStorageMultisample();
void glFramebufferRenderbuffer();
void glGenBuffers();
void glDeleteBuffers();
void glBindBuffer();
void glBufferData();
void glBufferSubData();
void glBufferStorage();
void glMapBufferRange();
void glBindBufferBase();
void glGenVertexArrays();
void glBindVertexArray();
void glEnableVertexAttribArray();
void glDisableVertexAttribArray();
void glVertexAttribPointer();
void glVertexAttribIPointer();
void glVertexAttribFormat();
void glVertexAttribIFormat();
void glVertexAttribBinding();
void glBindVertexBuffer();
void glBindTextures();
void glGetQueryBufferObjectuiv();
void glVertexAttrib4fv();
void glVertexAttribI4ui();
void glDrawArrays();
void glDrawElements();
void glDrawElementsBaseVertex();
void glCreateShader();
void glShaderSource();
void glCompileShader();
void glGetShaderiv();
void glGetShaderInfoLog();
void glDeleteShader();
void glCreateProgram();
void glAttachShader();
void glBindAttribLocation();
void glBindFragDataLocation();
void glLinkProgram();
void glGetProgramiv();
void glGetProgramInfoLog();
void glUseProgram();
void glGetUniformLocation();
void glUniform1i();
void glUniform1iv();
void glUniform1f();
void glUniform4fv();
void glUniform2f();
void glGenQueries();
void glBeginQuery();
void glEndQuery();
void glGetQueryObjectuiv();
void glMemoryBarrier();
void glDebugMessageCallback();

#endif
