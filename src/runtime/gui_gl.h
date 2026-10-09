/* 底层系统交互与数据协议契约 */
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
/* 底层系统交互与数据协议契约 */
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
#define ZGL_DEPTH_TEST                   0x0B71
#define ZGL_LEQUAL                       0x0203
#define ZGL_DEPTH_COMPONENT              0x1902
#define ZGL_DEPTH_COMPONENT16            0x81A5
#define ZGL_DEPTH_ATTACHMENT             0x8D00
#define ZGL_RENDERBUFFER                 0x8D41
#define ZGL_ELEMENT_ARRAY_BUFFER         0x8893
#define ZGL_UNSIGNED_SHORT               0x1403
#define ZGL_UNSIGNED_INT                 0x1405
#define ZGL_DEPTH_BUFFER_BIT             0x00000100
#define ZGL_TEXTURE_MAG_FILTER           0x2800
#define ZGL_TEXTURE_MIN_FILTER           0x2801
#define ZGL_TEXTURE_WRAP_S               0x2802
#define ZGL_TEXTURE_WRAP_T               0x2803
#define ZGL_CLAMP_TO_EDGE                0x812F
#define ZGL_TEXTURE0                     0x84C0
#define ZGL_TEXTURE1                     0x84C1
#define ZGL_TEXTURE2                     0x84C2
#define ZGL_ARRAY_BUFFER                 0x8892
#define ZGL_STREAM_DRAW                  0x88E0
#define ZGL_STATIC_DRAW                  0x88E4
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
    void      (*DrawElements)(zgl_enum, zgl_sizei, zgl_enum, const void *);
    void      (*DepthFunc)(zgl_enum);
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
    void      (*Uniform4f)(zgl_int, zgl_float, zgl_float, zgl_float, zgl_float);
    void      (*UniformMatrix4fv)(zgl_int, zgl_sizei, zgl_boolean,
                                  const zgl_float *);
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
    void      (*GenRenderbuffers)(zgl_sizei, zgl_uint *);
    void      (*DeleteRenderbuffers)(zgl_sizei, const zgl_uint *);
    void      (*BindRenderbuffer)(zgl_enum, zgl_uint);
    void      (*RenderbufferStorage)(zgl_enum, zgl_enum, zgl_sizei, zgl_sizei);
    void      (*FramebufferRenderbuffer)(zgl_enum, zgl_enum, zgl_enum,
                                         zgl_uint);
    zgl_enum  (*CheckFramebufferStatus)(zgl_enum);
    /* 底层系统交互与数据协议契约 */
    void      (*BlitFramebuffer)(zgl_int, zgl_int, zgl_int, zgl_int,
                                 zgl_int, zgl_int, zgl_int, zgl_int,
                                 zgl_bitfield, zgl_enum);
} zan_gl_api;

/* 底层系统交互与数据协议契约 */
int zan_gl_api_load(zan_gl_api *api, void *(*getproc)(const char *name));

#endif /* ZAN_GUI_GL_H */
