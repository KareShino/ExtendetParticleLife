#pragma once
// 必要最小限の OpenGL 3.3 関数ローダー
#include <cstddef>
#include <cstdint>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef unsigned int GLbitfield;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_SCISSOR_TEST 0x0C11
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_PROGRAM_POINT_SIZE 0x8642

#define GL_FUNCS(X)                                                                            \
    X(void, glViewport, GLint, GLint, GLsizei, GLsizei)                                        \
    X(void, glScissor, GLint, GLint, GLsizei, GLsizei)                                         \
    X(void, glClearColor, GLfloat, GLfloat, GLfloat, GLfloat)                                  \
    X(void, glClear, GLbitfield)                                                               \
    X(void, glEnable, GLenum)                                                                  \
    X(void, glDisable, GLenum)                                                                 \
    X(void, glBlendFunc, GLenum, GLenum)                                                       \
    X(void, glDrawArrays, GLenum, GLint, GLsizei)                                              \
    X(void, glGenBuffers, GLsizei, GLuint*)                                                    \
    X(void, glBindBuffer, GLenum, GLuint)                                                      \
    X(void, glBufferData, GLenum, GLsizeiptr, const void*, GLenum)                             \
    X(void, glGenVertexArrays, GLsizei, GLuint*)                                               \
    X(void, glBindVertexArray, GLuint)                                                         \
    X(void, glVertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)     \
    X(void, glEnableVertexAttribArray, GLuint)                                                 \
    X(GLuint, glCreateShader, GLenum)                                                          \
    X(void, glShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*)               \
    X(void, glCompileShader, GLuint)                                                           \
    X(void, glGetShaderiv, GLuint, GLenum, GLint*)                                             \
    X(void, glGetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                            \
    X(void, glDeleteShader, GLuint)                                                            \
    X(GLuint, glCreateProgram)                                                                 \
    X(void, glAttachShader, GLuint, GLuint)                                                    \
    X(void, glLinkProgram, GLuint)                                                             \
    X(void, glGetProgramiv, GLuint, GLenum, GLint*)                                            \
    X(void, glUseProgram, GLuint)                                                              \
    X(GLint, glGetUniformLocation, GLuint, const GLchar*)                                      \
    X(void, glUniform1f, GLint, GLfloat)                                                       \
    X(void, glUniform2f, GLint, GLfloat, GLfloat)                                              \
    X(void, glUniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat)

#define GL_DECL(ret, name, ...)                         \
    typedef ret(APIENTRY* PFN_##name)(__VA_ARGS__);     \
    extern PFN_##name p_##name;
GL_FUNCS(GL_DECL)
#undef GL_DECL

#define glViewport p_glViewport
#define glScissor p_glScissor
#define glClearColor p_glClearColor
#define glClear p_glClear
#define glEnable p_glEnable
#define glDisable p_glDisable
#define glBlendFunc p_glBlendFunc
#define glDrawArrays p_glDrawArrays
#define glGenBuffers p_glGenBuffers
#define glBindBuffer p_glBindBuffer
#define glBufferData p_glBufferData
#define glGenVertexArrays p_glGenVertexArrays
#define glBindVertexArray p_glBindVertexArray
#define glVertexAttribPointer p_glVertexAttribPointer
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glCreateShader p_glCreateShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glGetShaderiv p_glGetShaderiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glDeleteShader p_glDeleteShader
#define glCreateProgram p_glCreateProgram
#define glAttachShader p_glAttachShader
#define glLinkProgram p_glLinkProgram
#define glGetProgramiv p_glGetProgramiv
#define glUseProgram p_glUseProgram
#define glGetUniformLocation p_glGetUniformLocation
#define glUniform1f p_glUniform1f
#define glUniform2f p_glUniform2f
#define glUniform4f p_glUniform4f

// GLFW のコンテキストが current になってから呼ぶ。失敗時 false
bool LoadGL();
