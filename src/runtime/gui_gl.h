/* The slice of OpenGL 3.3 core the GPU backend uses, declared here rather than
 * taken from a system header.
 *
 * Every platform ships a usable GL 3.3 driver but none of them ship usable 3.3
 * *headers*: Windows' <GL/gl.h> stops at 1.1, and pulling in GLEW/GLAD/glcorearb
 * would add a dependency for what is, in the end, one struct of function
 * pointers. So the types, the enums and the entry points this backend actually
 * calls are spelled out below -- all of them are frozen ABI, so this cannot
 * drift the way a vendored header can.
 *
 * Every entry point is fetched at run time (wglGetProcAddress / glXGetProcAddress
 * / dlsym), so the shared library never links against libGL and keeps loading on
 * a machine with no GL at all -- the loader just fails and the CPU backend stays
 * in charge. */
#ifndef ZAN_GUI_GL_H
#define ZAN_GUI_GL_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned int  zgl_enum;
typedef unsigned int  zgl_bitfield;
typedef unsigned int  zgl_uint;
typedef int           zgl_int;
typedef int           zgl_sizei;
typedef ptrdiff_t     zgl_sizeiptr;
typedef ptrdiff_t     zgl_intptr;
typedef unsigned char zgl_boolean;
typedef char          zgl_char;
typedef float         zgl_float;

#define ZGL_FALSE                        0
#define ZGL_TRUE                         1
#define ZGL_NO_ERROR                     0
#define ZGL_TRIANGLES                    0x0004
#define ZGL_SRC_ALPHA                    0x0302
#define ZGL_ONE_MINUS_SRC_ALPHA          0x0303
#define ZGL_ONE                          1
/* Dual-source blending (core since 3.3): lets the text shader hand a *colour*
 * of coverage to the blender, which is what per-channel (subpixel) text AA
 * needs -- one alpha cannot express it. */
#define ZGL_SRC1_COLOR                   0x88F9
#define ZGL_ONE_MINUS_SRC1_COLOR         0x88FA
#define ZGL_SRC1_ALPHA                   0x8589
#define ZGL_ONE_MINUS_SRC1_ALPHA         0x88FB
#define ZGL_ZERO                         0
#define ZGL_BLEND                        0x0BE2
#define ZGL_SCISSOR_TEST                 0x0C11
#define ZGL_UNPACK_ROW_LENGTH            0x0CF2
#define ZGL_UNPACK_ALIGNMENT             0x0CF5
#define ZGL_PACK_ALIGNMENT               0x0D05
#define ZGL_TEXTURE_2D                   0x0DE1
#define ZGL_UNSIGNED_BYTE                0x1401
#define ZGL_FLOAT                        0x1406
#define ZGL_RGBA                         0x1908
#define ZGL_BGRA                         0x80E1
#define ZGL_RED                          0x1903
#define ZGL_R8                           0x8229
#define ZGL_RGBA8                        0x8058
#define ZGL_NEAREST                      0x2600
#define ZGL_LINEAR                       0x2601
#define ZGL_TEXTURE_MAG_FILTER           0x2800
#define ZGL_TEXTURE_MIN_FILTER           0x2801
#define ZGL_TEXTURE_WRAP_S               0x2802
#define ZGL_TEXTURE_WRAP_T               0x2803
#define ZGL_CLAMP_TO_EDGE                0x812F
#define ZGL_TEXTURE0                     0x84C0
#define ZGL_TEXTURE1                     0x84C1
#define ZGL_ARRAY_BUFFER                 0x8892
#define ZGL_STREAM_DRAW                  0x88E0
#define ZGL_FRAGMENT_SHADER              0x8B30
#define ZGL_VERTEX_SHADER                0x8B31
#define ZGL_COMPILE_STATUS               0x8B81
#define ZGL_LINK_STATUS                  0x8B82
#define ZGL_FRAMEBUFFER                  0x8D40
#define ZGL_COLOR_ATTACHMENT0            0x8CE0
#define ZGL_FRAMEBUFFER_COMPLETE         0x8CD5
#define ZGL_COLOR_BUFFER_BIT             0x00004000
#define ZGL_READ_FRAMEBUFFER             0x8CA8
#define ZGL_DRAW_FRAMEBUFFER             0x8CA9
#define ZGL_VERSION                      0x1F02
#define ZGL_RENDERER                     0x1F01

typedef struct zan_gl_api_s {
    /* --- state ---------------------------------------------------------- */
    void      (*Enable)(zgl_enum);
    void      (*Disable)(zgl_enum);
    void      (*BlendFunc)(zgl_enum, zgl_enum);
    void      (*BlendFuncSeparate)(zgl_enum, zgl_enum, zgl_enum, zgl_enum);
    void      (*Viewport)(zgl_int, zgl_int, zgl_sizei, zgl_sizei);
    void      (*Scissor)(zgl_int, zgl_int, zgl_sizei, zgl_sizei);
    void      (*ClearColor)(zgl_float, zgl_float, zgl_float, zgl_float);
    void      (*Clear)(zgl_bitfield);
    void      (*PixelStorei)(zgl_enum, zgl_int);
    void      (*Flush)(void);
    void      (*Finish)(void);
    zgl_enum  (*GetError)(void);
    const zgl_char *(*GetString)(zgl_enum);
    void      (*DrawArrays)(zgl_enum, zgl_int, zgl_sizei);
    void      (*ReadPixels)(zgl_int, zgl_int, zgl_sizei, zgl_sizei,
                            zgl_enum, zgl_enum, void *);
    /* --- shaders -------------------------------------------------------- */
    zgl_uint  (*CreateShader)(zgl_enum);
    void      (*ShaderSource)(zgl_uint, zgl_sizei, const zgl_char *const *,
                              const zgl_int *);
    void      (*CompileShader)(zgl_uint);
    void      (*GetShaderiv)(zgl_uint, zgl_enum, zgl_int *);
    void      (*GetShaderInfoLog)(zgl_uint, zgl_sizei, zgl_sizei *, zgl_char *);
    void      (*DeleteShader)(zgl_uint);
    zgl_uint  (*CreateProgram)(void);
    void      (*AttachShader)(zgl_uint, zgl_uint);
    void      (*LinkProgram)(zgl_uint);
    void      (*GetProgramiv)(zgl_uint, zgl_enum, zgl_int *);
    void      (*GetProgramInfoLog)(zgl_uint, zgl_sizei, zgl_sizei *, zgl_char *);
    void      (*UseProgram)(zgl_uint);
    void      (*DeleteProgram)(zgl_uint);
    zgl_int   (*GetUniformLocation)(zgl_uint, const zgl_char *);
    zgl_int   (*GetAttribLocation)(zgl_uint, const zgl_char *);
    void      (*Uniform1i)(zgl_int, zgl_int);
    void      (*Uniform2f)(zgl_int, zgl_float, zgl_float);
    /* --- buffers -------------------------------------------------------- */
    void      (*GenBuffers)(zgl_sizei, zgl_uint *);
    void      (*DeleteBuffers)(zgl_sizei, const zgl_uint *);
    void      (*BindBuffer)(zgl_enum, zgl_uint);
    void      (*BufferData)(zgl_enum, zgl_sizeiptr, const void *, zgl_enum);
    void      (*GenVertexArrays)(zgl_sizei, zgl_uint *);
    void      (*DeleteVertexArrays)(zgl_sizei, const zgl_uint *);
    void      (*BindVertexArray)(zgl_uint);
    void      (*EnableVertexAttribArray)(zgl_uint);
    void      (*VertexAttribPointer)(zgl_uint, zgl_int, zgl_enum, zgl_boolean,
                                     zgl_sizei, const void *);
    /* --- textures ------------------------------------------------------- */
    void      (*GenTextures)(zgl_sizei, zgl_uint *);
    void      (*DeleteTextures)(zgl_sizei, const zgl_uint *);
    void      (*BindTexture)(zgl_enum, zgl_uint);
    void      (*ActiveTexture)(zgl_enum);
    void      (*TexParameteri)(zgl_enum, zgl_enum, zgl_int);
    void      (*TexImage2D)(zgl_enum, zgl_int, zgl_int, zgl_sizei, zgl_sizei,
                            zgl_int, zgl_enum, zgl_enum, const void *);
    void      (*TexSubImage2D)(zgl_enum, zgl_int, zgl_int, zgl_int, zgl_sizei,
                               zgl_sizei, zgl_enum, zgl_enum, const void *);
    /* --- framebuffers --------------------------------------------------- */
    void      (*GenFramebuffers)(zgl_sizei, zgl_uint *);
    void      (*DeleteFramebuffers)(zgl_sizei, const zgl_uint *);
    void      (*BindFramebuffer)(zgl_enum, zgl_uint);
    void      (*FramebufferTexture2D)(zgl_enum, zgl_enum, zgl_enum, zgl_uint,
                                      zgl_int);
    zgl_enum  (*CheckFramebufferStatus)(zgl_enum);
    /* Presentation: the finished frame is copied from the surface's FBO to the
     * window's back buffer, so presenting needs no shader or geometry of its
     * own (core since GL 3.0, and this backend already demands 3.3). */
    void      (*BlitFramebuffer)(zgl_int, zgl_int, zgl_int, zgl_int,
                                 zgl_int, zgl_int, zgl_int, zgl_int,
                                 zgl_bitfield, zgl_enum);
} zan_gl_api;

/* Fills `api` from `getproc`, returning 0 when any entry point is missing --
 * a driver that cannot supply all of GL 3.3 core is not usable for this backend
 * and the caller keeps the CPU one. */
int zan_gl_api_load(zan_gl_api *api, void *(*getproc)(const char *name));

#endif /* ZAN_GUI_GL_H */
