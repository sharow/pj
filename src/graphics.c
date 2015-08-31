/* -*- Mode: c; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <bcm_host.h>
#include <GLES2/gl2.h>

#include "config.h"
#include "base.h"
#include "video.h"
#include "video_egl.h"
#include "graphics.h"


enum {
    MAX_RENDER_LAYER = 8,
    MAX_STATIC_IMAGE = 8
    /* MAX_SCENE = 6 */
};

typedef struct {
    int numer;
    int denom;
} Scaling;

struct RenderLayer_ {
    GLuint fragment_shader;
    GLuint program;
    GLuint texture_object;
    GLuint texture_unit;
    GLuint framebuffer;
    struct {
        GLuint vertex_coord;
        GLuint mouse;
        GLuint time;
        GLuint resolution;
        GLuint backbuffer;
        GLuint rand;
        GLuint prev_layer;
        GLuint prev_layer_resolution;
    } attr;
    void *auxptr;
};

/*
struct Scene_ {
    RenderLayer render_layer[MAX_RENDER_LAYER];
    int num_render_layer;
};
*/

struct Graphics_ {
    Video *video;
    VideoEGL *video_egl;
    Graphics_LAYOUT layout;
    GLuint array_buffer_fullscene_quad;
    GLuint vertex_shader;
    Graphics_WRAP_MODE texture_wrap_mode;
    Graphics_INTERPOLATION_MODE texture_interpolation_mode;
    Graphics_PIXELFORMAT texture_pixel_format;
    RenderLayer render_layer[MAX_RENDER_LAYER];
    int num_render_layer;
    struct {
        GLuint texture;
    } static_image[MAX_STATIC_IMAGE]; /* TODO */
    int num_static_image;
    int enable_backbuffer;
    GLuint backbuffer_texture_object;
    GLuint backbuffer_texture_unit;
    Scaling window_scaling;
    Scaling primary_framebuffer; /* TODO */
};


static void DeterminePixelFormat(Graphics_PIXELFORMAT pixel_format,
                                 GLint *out_internal_format,
                                 GLenum *out_format, GLenum *out_type);
static void DetermineLayoutPosition(Graphics_LAYOUT layout,
                                    int screen_width, int screen_height,
                                    int *out_x, int *out_y,
                                    int *out_width, int *out_height);

#ifdef NDEBUG
# define CHECK_GL()
#else
# define CHECK_GL() CheckGLError(__FILE__, __LINE__, __func__)
#endif

#ifndef NDEBUG
static void CheckGLError(const char *file, int line, const char *func)
{
    GLenum e;
    static const struct {
        const char *str;
        GLenum code;
    } tbl[] = {
        { "GL_INVALID_ENUM", 0x0500 },
        { "GL_INVALID_VALUE", 0x0501 },
        { "GL_INVALID_OPERATION", 0x0502 },
        { "GL_OUT_OF_MEMORY", 0x0505 }
    };

    e = glGetError();
    if (e != 0) {
        int i;
        printf("\r\nfrom %s(%d): function %s\r\n", file, line, func);
        for (i = 0; i < (int)ARRAY_SIZEOF(tbl); i++) {
            if (e == tbl[i].code) {
                printf("  OpenGL|ES raise: code 0x%04x (%s)\r\n", e, tbl[i].str);
                return;
            }
        }
        printf("  OpenGL|ES raise: code 0x%04x (?)\r\n", e);
    }    
}
#endif


void Graphics_HostInitialize(void)
{
    bcm_host_init();
}

void Graphics_HostDeinitialize(void)
{
}

static void PrintShaderLog(const char *message, GLuint shader)
{
    GLchar build_log[512];
    glGetShaderInfoLog(shader, sizeof(build_log), NULL, build_log);
    printf("%s %d: %s\r\n", message, shader, build_log);
}

static void PrintProgramLog(const char *message, GLuint program)
{
    GLchar build_log[512];
    glGetProgramInfoLog(program, sizeof(build_log), NULL, build_log);
    printf("%s %d: %s\r\n", message, program, build_log);
}


static void Scaling_Apply(Scaling *sc, int *inout_width, int *inout_height)
{
    *inout_width = (*inout_width * sc->numer) / sc->denom;
    *inout_height = (*inout_height * sc->numer) / sc->denom;
}

static int Scaling_IsOne(Scaling *sc)
{
    return (sc->numer == sc->denom) ? 1 : 0;
}


/* RenderLayer */
static int RenderLayer_Construct(RenderLayer *layer,
                                 OPTIONAL void *auxptr)
{
    memset(layer, 0, sizeof(*layer));
    layer->auxptr = auxptr;
    layer->fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    return (layer->fragment_shader == 0) ? 1 : 0;
}

static void RenderLayer_Destruct(RenderLayer *layer)
{
    glDeleteProgram(layer->program);
    layer->program = 0;
    glDeleteShader(layer->fragment_shader);
    layer->fragment_shader = 0;
    assert(layer->texture_object == 0);
}

void *RenderLayer_GetAux(RenderLayer *layer)
{
    return layer->auxptr;
}

int RenderLayer_UpdateShaderSource(RenderLayer *layer,
                                   const char *source,
                                   OPTIONAL int source_length)
{
    glShaderSource(layer->fragment_shader, 1, &source, &source_length);
    if (glGetError() != 0) {
        return 1;
    }
    return 0;
}

static int RenderLayer_AllocateOffscreen(RenderLayer *layer,
                                         int is_final_layer, int tex_unit,
                                         int tex_width, int tex_height,
                                         Graphics_PIXELFORMAT pixel_format,
                                         Graphics_INTERPOLATION_MODE interpolation_mode,
                                         Graphics_WRAP_MODE wrap_mode)
{
    /* TODO: handle error */
    GLint internal_format;
    GLenum format;
    GLenum type;
    GLint interpolation;
    GLint wrap;

    CHECK_GL();

    DeterminePixelFormat(pixel_format, &internal_format, &format, &type);

    switch (interpolation_mode) {
    case Graphics_INTERPOLATION_MODE_NEARESTNEIGHBOR:
        interpolation = GL_NEAREST;
        break;
    case Graphics_INTERPOLATION_MODE_BILINEAR:
        interpolation = GL_LINEAR;
        break;
    default:
        assert(0);
        interpolation = GL_NEAREST;
        break;
    }

    switch (wrap_mode) {
    case Graphics_WRAP_MODE_CLAMP_TO_EDGE:
        wrap = GL_CLAMP_TO_EDGE;
        break;
    case Graphics_WRAP_MODE_REPEAT:
        wrap = GL_REPEAT;
        break;
    case Graphics_WRAP_MODE_MIRRORED_REPEAT:
        wrap = GL_MIRRORED_REPEAT;
        break;
    default:
        assert(0);
        wrap = GL_REPEAT;
        break;
    }

    if (is_final_layer) {
        /* use FRAMEBUFFER = 0 */
        layer->texture_unit = 0; /* not use */
        layer->texture_object = 0;
        layer->framebuffer = 0;
    } else {
        layer->texture_unit = tex_unit;
        glGenTextures(1, &layer->texture_object);
        glBindTexture(GL_TEXTURE_2D, layer->texture_object);
        glTexImage2D(GL_TEXTURE_2D,
                     0,             /* level */
                     internal_format,
                     tex_width, tex_height,
                     0,             /* border */
                     format,
                     type,
                     NULL);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, interpolation);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, interpolation);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &layer->framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, layer->framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, layer->texture_object, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    CHECK_GL();
    return 0;
}

static void RenderLayer_DeallocateOffscreen(RenderLayer *layer)
{
    glBindTexture(GL_TEXTURE_2D, 0);
    if (layer->texture_object) {
        glDeleteTextures(1, &layer->texture_object);
        layer->texture_object = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (layer->framebuffer) {
        glDeleteFramebuffers(1, &layer->framebuffer);
        layer->framebuffer = 0;
    }
}

static int RenderLayer_BuildProgram(RenderLayer *layer,
                                    GLuint vertex_shader,
                                    GLuint array_buffer_fullscene_quad)
{
    GLint param;
    GLuint new_program;

    CHECK_GL();
    glCompileShader(layer->fragment_shader);
    glGetShaderiv(layer->fragment_shader, GL_COMPILE_STATUS, &param);
    if (param != GL_TRUE) {
        PrintShaderLog("fragment_shader", layer->fragment_shader);
        return 2;
    }

    new_program = glCreateProgram();
    glAttachShader(new_program, vertex_shader);
    glAttachShader(new_program, layer->fragment_shader);
    glLinkProgram(new_program);
    glGetProgramiv(new_program, GL_LINK_STATUS, &param);
    if (param != GL_TRUE) {
        PrintProgramLog("program", new_program);
        return 3;
    }
    glDeleteProgram(layer->program);
    layer->program = new_program;

    glUseProgram(layer->program);
    layer->attr.vertex_coord = glGetAttribLocation(layer->program, "vertex_coord");
    layer->attr.time = glGetUniformLocation(layer->program, "time");
    layer->attr.mouse = glGetUniformLocation(layer->program, "mouse");
    layer->attr.resolution = glGetUniformLocation(layer->program, "resolution");
    layer->attr.backbuffer = glGetUniformLocation(layer->program, "backbuffer");
    layer->attr.rand = glGetUniformLocation(layer->program, "rand");

    /* no need for 0 layer */
    layer->attr.prev_layer = glGetUniformLocation(layer->program, "prev_layer");
    layer->attr.prev_layer_resolution = glGetUniformLocation(layer->program, "prev_layer_resolution");

    glBindBuffer(GL_ARRAY_BUFFER, array_buffer_fullscene_quad);
    glVertexAttribPointer(layer->attr.vertex_coord,
                          4,
                          GL_FLOAT,
                          GL_FALSE, /* normalize */
                          16,
                          NULL);
    glEnableVertexAttribArray(layer->attr.vertex_coord);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    CHECK_GL();
    return 0;
}


/* Graphics */
static int Graphics_SetupInitialState(Graphics *g);


Graphics *Graphics_Create(Graphics_LAYOUT layout,
                          int scaling_numer, int scaling_denom)
{
    Graphics *g;
    Video *v;
    VideoEGL *ve;
    int width, height;
    int scaled_width, scaled_height;
    Scaling sc;

    v = NULL;
    ve = NULL;

    g = malloc(sizeof(*g));
    if (!g) {
        goto damn;
    }
    v = malloc(Video_InstanceSize());
    if (!v) {
        goto damn;
    }

    sc.numer = scaling_numer;
    sc.denom = scaling_denom;
    {
        int x, y;
        int screen_width, screen_height;
        /* prevent warning */
        screen_width = screen_height = 0;
        width = height = 0;
        x = y = 0;
        Video_GetScreenSize(Video_DEVICE_ID_MAIN_LCD, &screen_width, &screen_height);
        DetermineLayoutPosition(layout, screen_width, screen_height,
                                &x, &y, &width, &height);
        if (Video_ConstructWindow(v, Video_DEVICE_ID_MAIN_LCD,
                                  x, y, width, height, 0)) {
            goto damn;
        }
        scaled_width = width;
        scaled_height = height;
        Scaling_Apply(&sc, &scaled_width, &scaled_height);
        if (!Scaling_IsOne(&sc)) {
            Video_SetSourceRect(v, 0, 0, scaled_width, scaled_height);
            Video_ApplyChange(v);
        }
    }

    ve = malloc(VideoEGL_InstanceSize());
    if (!ve) {
        Video_DestructWindow(v);
        goto damn;

    }
    if (VideoEGL_Construct(ve)) {
        Video_DestructWindow(v);
        goto damn;
    }

    {
        unsigned int native_window_element = Video_GetNativeWindowElement(v);
        if (VideoEGL_CreateSurface(ve, native_window_element, scaled_width, scaled_height)) {
            VideoEGL_Destruct(ve);
            Video_DestructWindow(v);
            goto damn;
        }
    }

    if (VideoEGL_MakeCurrent(ve)) {
        VideoEGL_DestroySurface(ve);
        VideoEGL_Destruct(ve);
        Video_DestructWindow(v);
        goto damn;
    }

    g->video = v;
    g->video_egl = ve;
    g->layout = layout;
    g->array_buffer_fullscene_quad = 0;
    g->vertex_shader = 0;
    g->texture_wrap_mode = Graphics_WRAP_MODE_REPEAT;
    g->texture_interpolation_mode = Graphics_INTERPOLATION_MODE_NEARESTNEIGHBOR;
    g->texture_pixel_format = Graphics_PIXELFORMAT_RGBA8888;
    g->num_render_layer = 0;
    g->window_scaling = sc;
    g->enable_backbuffer = 0;
    g->backbuffer_texture_object = 0;
    g->backbuffer_texture_unit = 0;

    Graphics_SetupInitialState(g);
    return g;

  damn:
    if (ve) free(ve);
    if (v) free(v);
    if (g) free(g);
    return NULL;
}

void Graphics_Delete(Graphics *g)
{
    Graphics_DeallocateOffscreen(g);

    CHECK_GL();
    if (g->vertex_shader) {
        glDeleteShader(g->vertex_shader);
    }
    CHECK_GL();
    if (g->array_buffer_fullscene_quad) {
        glDeleteBuffers(1, &g->array_buffer_fullscene_quad);
    }
    CHECK_GL();
    
    VideoEGL_UnmakeCurrent(g->video_egl);
    VideoEGL_DestroySurface(g->video_egl);
    VideoEGL_Destruct(g->video_egl);

    Video_DestructWindow(g->video);

    free(g->video_egl);
    free(g->video);
    free(g);
}

static int Graphics_SetupInitialState(Graphics *g)
{
    CHECK_GL();

    if (g->array_buffer_fullscene_quad == 0) {
        static const GLfloat fullscene_quad[] = {
            -1.0, -1.0, 1.0, 1.0,
             1.0, -1.0, 1.0, 1.0,
             1.0,  1.0, 1.0, 1.0,
            -1.0,  1.0, 1.0, 1.0
        };
        glGenBuffers(1, &g->array_buffer_fullscene_quad);
        glBindBuffer(GL_ARRAY_BUFFER, g->array_buffer_fullscene_quad);
        glBufferData(GL_ARRAY_BUFFER, sizeof(fullscene_quad),
                     fullscene_quad, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        CHECK_GL();
    }


    if (g->vertex_shader == 0) {
        GLint param;
        static const GLchar *vertex_shader_source =
            "attribute vec4 vertex_coord;"
            "void main(void) { gl_Position = vertex_coord; }";
        g->vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(g->vertex_shader, 1, &vertex_shader_source, NULL);
        glCompileShader(g->vertex_shader);
        glGetShaderiv(g->vertex_shader, GL_COMPILE_STATUS, &param);
        if (param != GL_TRUE) {
            PrintShaderLog("vertex_shader", g->vertex_shader);
            assert(0);
            return 1;
        }
        CHECK_GL();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);

    {
        int width, height;
        Video_GetSourceSize(g->video, &width, &height);
        glViewport(0, 0, width, height);
    }
    return 0;
}

static int Graphics_ApplyWindowChange(Graphics *g)
{
    int width, height;
    int scaled_width, scaled_height;

    /* TODO handle error */
    VideoEGL_UnmakeCurrent(g->video_egl);
    VideoEGL_DestroySurface(g->video_egl);

    {
        int x, y;
        int screen_width, screen_height;
        /* prevent warning */
        screen_width = screen_height = 0;
        width = height = 0;
        x = y = 0;
        Video_GetScreenSize(Video_DEVICE_ID_MAIN_LCD, &screen_width, &screen_height);
        DetermineLayoutPosition(g->layout, screen_width, screen_height,
                                &x, &y, &width, &height);
        scaled_width = width;
        scaled_height = height;
        Scaling_Apply(&g->window_scaling, &scaled_width, &scaled_height);
        Video_SetWindowRect(g->video, x, y, width, height);
        Video_SetSourceRect(g->video, 0, 0, scaled_width, scaled_height);
        Video_ApplyChange(g->video);
    }

    {
        unsigned int native_window_element = Video_GetNativeWindowElement(g->video);
        if (VideoEGL_CreateSurface(g->video_egl, native_window_element, scaled_width, scaled_height)) {
            /* TODO */
            goto damn;
        }
    }

    if (VideoEGL_MakeCurrent(g->video_egl)) {
        /* TODO */
        goto damn;
    }
    CHECK_GL();

    Graphics_SetupInitialState(g);
    Graphics_DeallocateOffscreen(g);
    Graphics_AllocateOffscreen(g);
    return 0;
  damn:
    return 1;
}

int Graphics_AppendRenderLayer(Graphics *g,
                               const char *source,
                               OPTIONAL int source_length,
                               OPTIONAL void *auxptr)
{
    RenderLayer *layer;

    if (g->num_render_layer >= MAX_RENDER_LAYER) {
        return 1;
    }

    layer = &g->render_layer[g->num_render_layer];
    if (RenderLayer_Construct(layer, auxptr)) {
        return 2;
    }

    if (RenderLayer_UpdateShaderSource(layer, source, source_length)) {
        RenderLayer_Destruct(layer);
        return 3;
    }
    g->num_render_layer += 1;
    return 0;
}

RenderLayer *Graphics_GetRenderLayer(Graphics *g, int layer_index)
{
    assert(layer_index >= 0);
    if (layer_index >= g->num_render_layer) {
        return NULL;
    }
    return &g->render_layer[layer_index];
}

void Graphics_SetLayout(Graphics *g, Graphics_LAYOUT layout)
{
    g->layout = layout;
}

int Graphics_ApplyLayoutChange(Graphics *g)
{
    return Graphics_ApplyWindowChange(g);
}

void Graphics_SetOffscreenPixelFormat(Graphics *g, Graphics_PIXELFORMAT pixel_format)
{
    g->texture_pixel_format = pixel_format;
}

void Graphics_SetOffscreenInterpolationMode(Graphics *g, Graphics_INTERPOLATION_MODE interpolation_mode)
{
    g->texture_interpolation_mode = interpolation_mode;
}

void Graphics_SetOffscreenWrapMode(Graphics *g, Graphics_WRAP_MODE wrap_mode)
{
    g->texture_wrap_mode = wrap_mode;
}

int Graphics_ApplyOffscreenChange(Graphics *g)
{
    Graphics_DeallocateOffscreen(g);
    return Graphics_AllocateOffscreen(g);
}

void Graphics_SetWindowScaling(Graphics *g, int numer, int denom)
{
    assert(numer >= 0);
    assert(denom >= 0);
    assert(numer <= denom);

    g->window_scaling.numer = numer;
    g->window_scaling.denom = denom;
}

int Graphics_ApplyWindowScalingChange(Graphics *g)
{
    return Graphics_ApplyWindowChange(g);
}

int Graphics_AllocateOffscreen(Graphics *g)
{
    int i;
    int source_width, source_height;

    Video_GetSourceSize(g->video, &source_width, &source_height);
    //printf("Graphics_AllocateOffscreen: width=%d, height=%d\r\n", source_width, source_height);

    CHECK_GL();
    for (i = 0; i < g->num_render_layer; i++) {
        RenderLayer *layer = &g->render_layer[i];
        int texture_unit = i;
        int is_final_layer = (i == (g->num_render_layer - 1)) ? 1 : 0;
        RenderLayer_AllocateOffscreen(layer, is_final_layer, texture_unit,
                                      source_width, source_height,
                                      g->texture_pixel_format,
                                      g->texture_interpolation_mode,
                                      g->texture_wrap_mode);
        /* TODO: handle error */
    }
    if (g->enable_backbuffer) {
        GLint internal_format;
        GLenum format;
        GLenum type;
        DeterminePixelFormat(g->texture_pixel_format, &internal_format, &format, &type);
        g->backbuffer_texture_unit = g->num_render_layer;
        glGenTextures(1, &g->backbuffer_texture_object);
        glBindTexture(GL_TEXTURE_2D, g->backbuffer_texture_object);
        glTexImage2D(GL_TEXTURE_2D,
                     0,             /* level */
                     internal_format,
                     source_width, source_height,
                     0,             /* border */
                     format,
                     type,
                     NULL);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    CHECK_GL();
    return 0;
}

void Graphics_DeallocateOffscreen(Graphics *g)
{
    int i;
    if (g->backbuffer_texture_object) {
        glDeleteTextures(1, &g->backbuffer_texture_object);
        g->backbuffer_texture_object = 0;
    }
    for (i = g->num_render_layer - 1; i >= 0; i--) {
        RenderLayer_DeallocateOffscreen(&g->render_layer[i]);
    }
}

int Graphics_BuildRenderLayer(Graphics *g, int layer_index)
{
    RenderLayer_BuildProgram(&g->render_layer[layer_index],
                             g->vertex_shader,
                             g->array_buffer_fullscene_quad);
    /* TODO: handle error */
    return 0;
}

void Graphics_SetUniforms(Graphics *g, double t,
                          double mouse_x, double mouse_y,
                          double random)
{
    int i;
    int width, height;

    CHECK_GL();
    Video_GetSourceSize(g->video, &width, &height);
    for (i = 0; i < g->num_render_layer; i++) {
        RenderLayer *p;
        p = &g->render_layer[i];
        glUseProgram(p->program);
        glUniform1f(p->attr.time, t);
        glUniform2f(p->attr.resolution, (double)width, (double)height);
        glUniform2f(p->attr.mouse, mouse_x, mouse_y);
        glUniform1f(p->attr.rand, random);
        glUseProgram(0);
    }
    CHECK_GL();
}

void Graphics_Render(Graphics *g)
{
    int i;
    GLuint prev_layer_texture_unit;
    GLuint prev_layer_texture_object;

    CHECK_GL();
    prev_layer_texture_unit = 0;
    prev_layer_texture_object = 0;
    for (i = 0; i < g->num_render_layer; i++) {
        RenderLayer *p;
        p = &g->render_layer[i];
        glUseProgram(p->program);
        if (g->enable_backbuffer) {
            glUniform1i(p->attr.backbuffer, g->backbuffer_texture_unit);
            glActiveTexture(GL_TEXTURE0 + g->backbuffer_texture_unit);
            glBindTexture(GL_TEXTURE_2D, g->backbuffer_texture_object);
        }
        if (i == 0) {
            /* primary layer */
            glBindFramebuffer(GL_FRAMEBUFFER, p->framebuffer);
            glActiveTexture(GL_TEXTURE0 + p->texture_unit);
            glBindTexture(GL_TEXTURE_2D, 0);
        } else if (i == (g->num_render_layer-1)) {
            /* final layer */
            glUniform1i(p->attr.prev_layer, prev_layer_texture_unit);
            /* TODO: plav_layer_resolution */
            glActiveTexture(GL_TEXTURE0 + prev_layer_texture_unit);
            glBindTexture(GL_TEXTURE_2D, prev_layer_texture_object);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else {
            glUniform1i(p->attr.prev_layer, prev_layer_texture_unit);
            /* TODO: plav_layer_resolution */
            glActiveTexture(GL_TEXTURE0 + p->texture_unit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + prev_layer_texture_unit);
            glBindTexture(GL_TEXTURE_2D, prev_layer_texture_object);
            glBindFramebuffer(GL_FRAMEBUFFER, p->framebuffer);
        }

        glBindBuffer(GL_ARRAY_BUFFER, g->array_buffer_fullscene_quad);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glFlush();

        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glUseProgram(0);

        prev_layer_texture_unit = p->texture_unit;
        prev_layer_texture_object = p->texture_object;
    }

    if (g->enable_backbuffer) {
        int width, height;
        Video_GetSourceSize(g->video, &width, &height);
        glActiveTexture(GL_TEXTURE0 + g->backbuffer_texture_unit);
        glBindTexture(GL_TEXTURE_2D, g->backbuffer_texture_object); /* destination */
        glBindFramebuffer(GL_FRAMEBUFFER, g->render_layer[g->num_render_layer-1].framebuffer); /* source */

        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, width, height, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    CHECK_GL();

    VideoEGL_SwapBuffers(g->video_egl);
}

void Graphics_SetBackbuffer(Graphics *g, int enable)
{
    g->enable_backbuffer = enable;
}

void Graphics_GetWindowSize(Graphics *g, int *out_width, int *out_height)
{
    Video_GetWindowSize(g->video, out_width, out_height);
}

void Graphics_GetSourceSize(Graphics *g, int *out_width, int *out_height)
{
    Video_GetSourceSize(g->video, out_width, out_height);
}

Graphics_LAYOUT Graphics_GetCurrentLayout(Graphics *g)
{
    return g->layout;
}

Graphics_LAYOUT Graphics_GetLayout(Graphics_LAYOUT layout, int forward)
{
    if (forward) {
        layout += 1;
        if (layout == Graphics_LAYOUT_ENUMS) {
            layout = 0;
        }
    } else {
        if (layout == 0) {
            layout = Graphics_LAYOUT_ENUMS - 1;
        } else {
            layout -= 1;
        }
    }
    return layout;
}

static void DeterminePixelFormat(Graphics_PIXELFORMAT pixel_format,
                                 GLint *out_internal_format,
                                 GLenum *out_format, GLenum *out_type)
{
    GLint internal_format;
    GLenum format;
    GLenum type;

    CHECK_GL();

    switch (pixel_format) {
        /* 32bpp */
    case Graphics_PIXELFORMAT_RGBA8888:
        internal_format = GL_RGBA;
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
        break;
        /* 24bpp */
    case Graphics_PIXELFORMAT_RGB888:
        internal_format = GL_RGB;
        format = GL_RGB;
        type = GL_UNSIGNED_BYTE;
        break;
        /* 16bpp */
    case Graphics_PIXELFORMAT_RGB565:
        internal_format = GL_RGB;
        format = GL_RGB;
        type = GL_UNSIGNED_SHORT_5_6_5;
        break;
    case Graphics_PIXELFORMAT_RGBA5551:
        internal_format = GL_RGBA;
        format = GL_RGBA;
        type = GL_UNSIGNED_SHORT_5_5_5_1;
        break;
    case Graphics_PIXELFORMAT_RGBA4444:
        internal_format = GL_RGBA;
        format = GL_RGBA;
        type = GL_UNSIGNED_SHORT_4_4_4_4;
        break;
    default:
        assert(!"invalid pixel format");
        internal_format = GL_RGBA;
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
        break;
    }
    *out_internal_format = internal_format;
    *out_format = format;
    *out_type = type;
}

static void DetermineLayoutPosition(Graphics_LAYOUT layout,
                                    int screen_width, int screen_height,
                                    int *out_x, int *out_y,
                                    int *out_width, int *out_height)
{
    switch (layout) {
    case Graphics_LAYOUT_LEFT_TOP:
        *out_x = 0;
        *out_y = 0;
        *out_width = screen_width / 2;
        *out_height = screen_height / 2;
        break;
    case Graphics_LAYOUT_RIGHT_TOP:
        *out_x = screen_width / 2;
        *out_y = 0;
        *out_width = screen_width / 2;
        *out_height = screen_height / 2;
        break;
    case Graphics_LAYOUT_LEFT_BOTTOM:
        *out_x = 0;
        *out_y = screen_height / 2;
        *out_width = screen_width / 2;
        *out_height = screen_height / 2;
        break;
    case Graphics_LAYOUT_RIGHT_BOTTOM:
        *out_x = screen_width / 2;
        *out_y = screen_height / 2;
        *out_width = screen_width / 2;
        *out_height = screen_height / 2;
        break;
    case Graphics_LAYOUT_LEFT:
        *out_x = 0;
        *out_y = 0;
        *out_width = screen_width / 2;
        *out_height = screen_height;
        break;
    case Graphics_LAYOUT_RIGHT:
        *out_x = screen_width / 2;
        *out_y = 0;
        *out_width = screen_width / 2;
        *out_height = screen_height;
        break;
    case Graphics_LAYOUT_TOP:
        *out_x = 0;
        *out_y = 0;
        *out_width = screen_width;
        *out_height = screen_height / 2;
        break;
    case Graphics_LAYOUT_BOTTOM:
        *out_x = 0;
        *out_y = screen_height / 2;
        *out_width = screen_width;
        *out_height = screen_height / 2;
        break;
    case Graphics_LAYOUT_CENTER_VERY_WIDE:
        *out_x = 0;
        *out_y = screen_height / 4;
        *out_width = screen_width;
        *out_height = screen_height / 2;
        break;
    case Graphics_LAYOUT_CENTER_WIDE:
        *out_x = 0;
        *out_y = screen_height / 8;
        *out_width = screen_width;
        *out_height = (screen_height * 6) / 8;
        break;
    case Graphics_LAYOUT_FULLSCREEN:
        *out_x = 0;
        *out_y = 0;
        *out_width = screen_width;
        *out_height = screen_height;
        break;
    default:
        assert(0);
        break;
    }
}

