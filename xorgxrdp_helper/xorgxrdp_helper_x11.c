
#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>

#include <epoxy/gl.h>
#include <epoxy/glx.h>

#include "arch.h"
#include "os_calls.h"
#include "xorgxrdp_helper.h"
#include "xorgxrdp_helper_x11.h"

#if defined(XRDP_NVENC)
#include "xorgxrdp_helper_nvenc.h"
#endif

/* X11 */
static Display *g_display = NULL;
static int g_x_socket = 0;
static int g_screen_num = 0;
static Screen *g_screen = NULL;
static Window g_root_window = None;
static Visual *g_vis = NULL;
static GC g_gc;

/* glx */
static int g_n_fbconfigs = 0;
static int g_n_pixconfigs = 0;
static GLXFBConfig *g_fbconfigs = NULL;
static GLXFBConfig *g_pixconfigs = NULL;
static GLXContext g_gl_context = 0;

struct mon_info
{
    int width;
    int height;
    Pixmap pixmap;
    GLXPixmap glxpixmap;
    GLuint bmp_texture;
    GLuint enc_texture;
    struct enc_info *ei;
};

static struct mon_info g_mons[16];

static const int g_fbconfig_attrs[] =
{
    GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
    GLX_RENDER_TYPE,   GLX_RGBA_BIT,
    GLX_DOUBLEBUFFER,  True,
    GLX_RED_SIZE,      8,
    GLX_GREEN_SIZE,    8,
    GLX_BLUE_SIZE,     8,
    None
};

static const int g_pixconfig_attrs[] =
{
    GLX_BIND_TO_TEXTURE_RGBA_EXT,       True,
    GLX_DRAWABLE_TYPE,                  GLX_PIXMAP_BIT,
    GLX_BIND_TO_TEXTURE_TARGETS_EXT,    GLX_TEXTURE_2D_BIT_EXT,
    GLX_DOUBLEBUFFER,                   False,
    GLX_Y_INVERTED_EXT,                 True,
    None
};

static const int g_pixmap_attribs[] =
{
    GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
    GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGBA_EXT,
    None
};

static GLuint g_quad_vao = 0;
static GLuint g_quad_vbo = 0;
static GLuint g_fb = 0;

#define XH_SHADERCOPY       0
#define XH_SHADERRGB2YUV    1
#define XH_SHADERRGB2YUVFR  2
#define XH_SHADERRGB2YUVRFX 3

#define XH_NUM_SHADERS 4

struct shader_info
{
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLint tex_loc;
    GLint tex_size_loc;
};
static struct shader_info g_si[XH_NUM_SHADERS];

static const GLfloat g_vertices[] =
{
    -1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f,  1.0f,
     1.0f, -1.0f
};

static const GLchar g_vs[] =
"\
attribute vec4 position;\n\
void main(void)\n\
{\n\
    gl_Position = vec4(position.xy, 0.0, 1.0);\n\
}\n";
static const GLchar g_fs_copy[] =
"\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
void main(void)\n\
{\n\
    gl_FragColor = texture2D(tex, gl_FragCoord.xy / tex_size);\n\
}\n";
/*
This is basic bt601 formula
    Y = (( 66 * R + 129 * G +  25 * B + 128) >> 8) +  16;
    U = ((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128;
    V = ((112 * R -  94 * G -  18 * B + 128) >> 8) + 128;
Output is NV_ENC_BUFFER_FORMAT_AYUV
    8 bit Packed A8Y8U8V8. This is a word-ordered format
    where a pixel is represented by a 32-bit word with V
    in the lowest 8 bits, U in the next 8 bits, Y in the
    8 bits after that and A in the highest 8 bits.
*/
static const GLchar g_fs_rgb_to_yuv[] =
"\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
void main(void)\n\
{\n\
    vec4 ymath;\n\
    vec4 umath;\n\
    vec4 vmath;\n\
    vec4 pixel;\n\
    vec4 pixel1;\n\
    vec4 pixel2;\n\
    ymath = vec4( 66.0/256.0, 129.0/256.0,  25.0/256.0, 1.0);\n\
    umath = vec4(-38.0/256.0, -74.0/256.0, 112.0/256.0, 1.0);\n\
    vmath = vec4(112.0/256.0, -94.0/256.0, -18.0/256.0, 1.0);\n\
    pixel = texture2D(tex, gl_FragCoord.xy / tex_size);\n\
    ymath = ymath * pixel;\n\
    umath = umath * pixel;\n\
    vmath = vmath * pixel;\n\
    pixel1 = vec4(vmath.r + vmath.g + vmath.b + 128.0/256.0,\n\
                  umath.r + umath.g + umath.b + 128.0/256.0,\n\
                  ymath.r + ymath.g + ymath.b +  16.0/256.0,\n\
                  1.0);\n\
    pixel2 = clamp(pixel1, 0.0, 1.0);\n\
    gl_FragColor = pixel2;\n\
}\n";
/*
This is full range bt709 formula
    Y = (( 54 * R + 183 * G +  18 * B) >> 8);
    U = ((-29 * R -  99 * G + 128 * B) >> 8) + 128;
    V = ((128 * R - 116 * G -  12 * B) >> 8) + 128;
Output is NV_ENC_BUFFER_FORMAT_AYUV
    8 bit Packed A8Y8U8V8. This is a word-ordered format
    where a pixel is represented by a 32-bit word with V
    in the lowest 8 bits, U in the next 8 bits, Y in the
    8 bits after that and A in the highest 8 bits.
*/
static const GLchar g_fs_709fr_rgb_to_yuv[] =
"\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
void main(void)\n\
{\n\
    vec4 ymath;\n\
    vec4 umath;\n\
    vec4 vmath;\n\
    vec4 pixel;\n\
    vec4 pixel1;\n\
    vec4 pixel2;\n\
    ymath = vec4( 54.0/256.0,  183.0/256.0,  18.0/256.0, 1.0);\n\
    umath = vec4(-29.0/256.0,  -99.0/256.0, 128.0/256.0, 1.0);\n\
    vmath = vec4(128.0/256.0, -116.0/256.0, -12.0/256.0, 1.0);\n\
    pixel = texture2D(tex, gl_FragCoord.xy / tex_size);\n\
    ymath = ymath * pixel;\n\
    umath = umath * pixel;\n\
    vmath = vmath * pixel;\n\
    pixel1 = vec4(vmath.r + vmath.g + vmath.b + 128.0/256.0,\n\
                  umath.r + umath.g + umath.b + 128.0/256.0,\n\
                  ymath.r + ymath.g + ymath.b,\n\
                  1.0);\n\
    pixel2 = clamp(pixel1, 0.0, 1.0);\n\
    gl_FragColor = pixel2;\n\
}\n";
/*
This is RFX formula
    Y =   0.299    * R + 0.587    * G + 0.114    * B;
    U = (-0.168935 * R - 0.331665 * G + 0.50059  * B) + 0.5;
    V = ( 0.499813 * R - 0.418531 * G - 0.081282 * B) + 0.5;
Output is NV_ENC_BUFFER_FORMAT_AYUV
    8 bit Packed A8Y8U8V8. This is a word-ordered format
    where a pixel is represented by a 32-bit word with V
    in the lowest 8 bits, U in the next 8 bits, Y in the
    8 bits after that and A in the highest 8 bits.
*/
static const GLchar g_fs_rfx_rgb_to_yuv[] =
"\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
void main(void)\n\
{\n\
    vec4 ymath;\n\
    vec4 umath;\n\
    vec4 vmath;\n\
    vec4 pixel;\n\
    vec4 pixel1;\n\
    vec4 pixel2;\n\
    ymath = vec4( 0.299000,  0.587000,  0.114000, 1.0);\n\
    umath = vec4(-0.168935, -0.331665,  0.500590, 1.0);\n\
    vmath = vec4( 0.499830, -0.418531, -0.081282, 1.0);\n\
    pixel = texture2D(tex, gl_FragCoord.xy / tex_size);\n\
    ymath = ymath * pixel;\n\
    umath = umath * pixel;\n\
    vmath = vmath * pixel;\n\
    pixel1 = vec4(vmath.r + vmath.g + vmath.b + 0.5,\n\
                  umath.r + umath.g + umath.b + 0.5,\n\
                  ymath.r + ymath.g + ymath.b,\n\
                  1.0);\n\
    pixel2 = clamp(pixel1, 0.0, 1.0);\n\
    gl_FragColor = pixel2;\n\
}\n";

/*****************************************************************************/
int
xorgxrdp_helper_x11_init(void)
{
    Bool ok;
    const GLchar *vsource[XH_NUM_SHADERS];
    const GLchar *fsource[XH_NUM_SHADERS];
    GLint linked;
    GLint compiled;
    GLint vlength;
    GLint flength;
    int index;

    /* x11 */
    g_display = XOpenDisplay(0);
    if (g_display == NULL)
    {
        return 1;
    }
    g_x_socket = XConnectionNumber(g_display);
    g_screen_num = DefaultScreen(g_display);
    g_screen = ScreenOfDisplay(g_display, g_screen_num);
    g_root_window = RootWindowOfScreen(g_screen);
    g_vis = XDefaultVisual(g_display, g_screen_num);
    g_gc = DefaultGC(g_display, 0);
    /* glx */
    g_fbconfigs = glXChooseFBConfig(g_display, g_screen_num,
                                    g_fbconfig_attrs, &g_n_fbconfigs);
    g_writeln("g_fbconfigs %p", g_fbconfigs);
    g_gl_context = glXCreateNewContext(g_display, g_fbconfigs[0],
                                       GLX_RGBA_TYPE, NULL, 1);
    g_writeln("g_gl_context %p", g_gl_context);
    ok = glXMakeCurrent(g_display, g_root_window, g_gl_context);
    g_writeln("ok %d", ok);
    g_pixconfigs = glXChooseFBConfig(g_display, g_screen_num,
                                     g_pixconfig_attrs, &g_n_pixconfigs);
    g_writeln("g_pixconfigs %p g_n_pixconfigs %d",
              g_pixconfigs, g_n_pixconfigs);
    g_writeln("vendor: %s", (const char *) glGetString(GL_VENDOR));

    /* create vertex array */
    glGenVertexArrays(1, &g_quad_vao);
    glBindVertexArray(g_quad_vao);
    glGenBuffers(1, &g_quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertices), g_vertices,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, NULL);
    glGenFramebuffers(1, &g_fb);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    /* create copy shader */
    vsource[XH_SHADERCOPY] = g_vs;
    fsource[XH_SHADERCOPY] = g_fs_copy;
    /* create rgb2yuv shader */
    vsource[XH_SHADERRGB2YUV] = g_vs;
    fsource[XH_SHADERRGB2YUV] = g_fs_rgb_to_yuv;
    /* create rgb2yuv shader */
    vsource[XH_SHADERRGB2YUVFR] = g_vs;
    fsource[XH_SHADERRGB2YUVFR] = g_fs_709fr_rgb_to_yuv;
    /* create rgb2yuv shader */
    vsource[XH_SHADERRGB2YUVRFX] = g_vs;
    fsource[XH_SHADERRGB2YUVRFX] = g_fs_rfx_rgb_to_yuv;

    for (index = 0; index < XH_NUM_SHADERS; index++)
    {
        g_si[index].vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        g_si[index].fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        vlength = g_strlen(vsource[index]);
        flength = g_strlen(fsource[index]);
        glShaderSource(g_si[index].vertex_shader, 1,
                       &(vsource[index]), &vlength);
        glShaderSource(g_si[index].fragment_shader, 1,
                       &(fsource[index]), &flength);
        glCompileShader(g_si[index].vertex_shader);
        glGetShaderiv(g_si[index].vertex_shader, GL_COMPILE_STATUS,
                      &compiled);
        g_writeln("xorgxrdp_helper_x11_init: vertex_shader compiled %d",
                  compiled);
        glCompileShader(g_si[index].fragment_shader);
        glGetShaderiv(g_si[index].fragment_shader, GL_COMPILE_STATUS,
                      &compiled);
        g_writeln("xorgxrdp_helper_x11_init: fragment_shader compiled %d",
                  compiled);
        g_si[index].program = glCreateProgram();
        glAttachShader(g_si[index].program, g_si[index].vertex_shader);
        glAttachShader(g_si[index].program, g_si[index].fragment_shader);
        glLinkProgram(g_si[index].program);
        glGetProgramiv(g_si[index].program, GL_LINK_STATUS, &linked);
        g_writeln("xorgxrdp_helper_x11_init: linked %d", linked);
        g_si[index].tex_loc =
            glGetUniformLocation(g_si[index].program, "tex");
        g_si[index].tex_size_loc =
            glGetUniformLocation(g_si[index].program, "tex_size");
        g_writeln("xorgxrdp_helper_x11_init: tex_loc %d tex_size_loc %d",
                  g_si[index].tex_loc, g_si[index].tex_size_loc);
    }
    g_memset(g_mons, 0, sizeof(g_mons));
#if defined(XRDP_NVENC)
    xorgxrdp_helper_nvenc_init();
#endif
    return 0;
}

/*****************************************************************************/
int
xorgxrdp_helper_x11_get_wait_objs(intptr_t *objs, int *obj_count)
{
    objs[*obj_count] = g_x_socket;
    (*obj_count)++;
    return 0;
}

/*****************************************************************************/
int
xorgxrdp_helper_x11_check_wai_objs(void)
{
    XEvent xevent;

    while (XPending(g_display) > 0)
    {
        g_writeln("xorgxrdp_helper_x11_check_wai_objs: loop");
        XNextEvent(g_display, &xevent);
    }
    return 0;
}

/*****************************************************************************/
int
xorgxrdp_helper_x11_delete_all_pixmaps(void)
{
    int index;
    struct mon_info *mi;

    for (index = 0; index < 16; index++)
    {
        mi = g_mons + index;
        if (mi->pixmap != 0)
        {
#if defined(XRDP_NVENC)
            xorgxrdp_helper_nvenc_delete_encoder(mi->ei);
#endif
            glXReleaseTexImageEXT(g_display, mi->glxpixmap, GLX_FRONT_EXT);
            glDeleteTextures(1, &(mi->bmp_texture));
            glDeleteTextures(1, &(mi->enc_texture));
            glXDestroyPixmap(g_display, mi->glxpixmap);
            XFreePixmap(g_display, mi->pixmap);
            mi->pixmap = 0;
        }
    }
    return 0;
}

/*****************************************************************************/
int
xorgxrdp_helper_x11_create_pixmap(int width, int height, int magic,
                                  int con_id, int mon_id)
{
    Pixmap pixmap;
    XImage *image;
    int img[64];
    GLXPixmap glxpixmap;
    GLuint bmp_texture;
    GLuint enc_texture;

    mon_id = mon_id & 0xF;
    if (g_mons[mon_id].pixmap != 0)
    {
        g_writeln("xorgxrdp_helper_x11_create_pixmap: error already setup");
        return 1;
    }
    g_writeln("xorgxrdp_helper_x11_create_pixmap: width %d height %d, "
              "magic 0x%8.8x, con_id %d mod_id %d", width, height,
              magic, con_id, mon_id);
    pixmap = XCreatePixmap(g_display, g_root_window, width, height, 24);
    g_writeln("xorgxrdp_helper_x11_create_pixmap: pixmap %d", (int) pixmap);
    glxpixmap = glXCreatePixmap(g_display, g_pixconfigs[0],
                                pixmap, g_pixmap_attribs);
    g_writeln("xorgxrdp_helper_x11_create_pixmap: glxpixmap %p", (void *) glxpixmap);

    g_memset(img, 0, sizeof(img));
    img[0] = magic;
    img[1] = con_id;
    img[2] = mon_id;
    image = XCreateImage(g_display, g_vis, 24, ZPixmap, 0, (char *) img,
                         4, 4, 32, 0);
    XPutImage(g_display, pixmap, g_gc, image, 0, 0, 0, 0, 4, 4);
    XFree(image);

    glEnable(GL_TEXTURE_2D);
    glGenTextures(1, &enc_texture);
    glBindTexture(GL_TEXTURE_2D, enc_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_INT_8_8_8_8, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &bmp_texture);
    glBindTexture(GL_TEXTURE_2D, bmp_texture);
    glXBindTexImageEXT(g_display, glxpixmap, GLX_FRONT_EXT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, 0);

#if defined(XRDP_NVENC)
    xorgxrdp_helper_nvenc_create_encoder(width, height, enc_texture,
                                         &(g_mons[mon_id].ei));
#endif

    g_mons[mon_id].pixmap = pixmap;
    g_mons[mon_id].glxpixmap = glxpixmap;
    g_mons[mon_id].enc_texture = enc_texture;
    g_mons[mon_id].width = width;
    g_mons[mon_id].height = height;
    g_mons[mon_id].bmp_texture = bmp_texture;

    return 0;
}

/*****************************************************************************/
int
xorgxrdp_helper_x11_encode_pixmap(int width, int height, int mon_id,
                                  int num_crects, struct xh_rect *crects,
                                  void *cdata, int *cdata_bytes)
{
    struct mon_info *mi;
    struct xh_rect *crect;
    struct shader_info *si;
    int index;
    int rv;

    mi = g_mons + (mon_id & 0xF);
    if ((width != mi->width) || (height != mi->height))
    {
        return 1;
    }
    /* rgb to yuv */
    si = g_si + XH_SHADERRGB2YUV;
    glEnable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mi->bmp_texture);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, mi->enc_texture, 0);
    glUseProgram(si->program);
    glBindVertexArray(g_quad_vao);
    glUniform1i(si->tex_loc, 0);
    glUniform2f(si->tex_size_loc, mi->width, mi->height);
    if (num_crects > 0)
    {
        for (index = 0; index < num_crects; index++)
        {
            crect = crects + index;
            glViewport(crect->x, crect->y, crect->w, crect->h);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }
    else
    {
        glViewport(0, 0, mi->width, mi->height);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    /* sync before encoding */
    glFinish();
    /* encode */
#if defined(XRDP_NVENC)
    rv = xorgxrdp_helper_nvenc_encode(mi->ei, mi->enc_texture,
                                      cdata, cdata_bytes);
#else
    rv = 1;
#endif
    return rv;
}

