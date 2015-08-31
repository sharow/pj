/* -*- Mode: c; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <GLES/gl.h>
#include <EGL/egl.h>

#include "config.h"
#include "base.h"
#include "video_egl.h"


/* #define PRINT_EGLCONFIG_ATTRS*/

struct VideoEGL_ {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    EGLConfig config;
    EGL_DISPMANX_WINDOW_T native_window;
};

static void PrintEGLConfigAttrib(EGLDisplay display)
{
#ifdef PRINT_EGLCONFIG_ATTRS
    EGLBoolean r;
    EGLint num_config;
    EGLConfig configs[32];
    int i, j;

    static const struct {
        const char * const str;
        EGLint attr;
    } attrib_pairs[] = {
        /* full list in http://www.khronos.org/opengles/documentation/opengles1_0/html/eglGetConfigAttrib.html */
        { "EGL_BUFFER_SIZE", EGL_BUFFER_SIZE },
        { "EGL_RED_SIZE", EGL_RED_SIZE },
        { "EGL_GREEN_SIZE", EGL_GREEN_SIZE },
        { "EGL_BLUE_SIZE", EGL_BLUE_SIZE },
        { "EGL_ALPHA_SIZE", EGL_ALPHA_SIZE },
        { "EGL_DEPTH_SIZE", EGL_DEPTH_SIZE },
        { "EGL_CONFIG_ID", EGL_CONFIG_ID },
        { "EGL_MAX_PBUFFER_WIDTH", EGL_MAX_PBUFFER_WIDTH },
        { "EGL_MAX_PBUFFER_HEIGHT", EGL_MAX_PBUFFER_HEIGHT },
        { "EGL_NATIVE_RENDERABLE", EGL_NATIVE_RENDERABLE }
    };

    r = eglGetConfigs(display, configs, ARRAY_SIZEOF(configs), &num_config);
    for (i = 0; i < num_config; i++) {
        EGLint value;
        printf("config number: %d\r\n", i);
        for (j = 0; j < ARRAY_SIZEOF(attrib_pairs); j++) {
            r = eglGetConfigAttrib(display, configs[i], attrib_pairs[j].attr, &value);
            printf("  %s: %d\r\n", attrib_pairs[j].str, value);
        }
        printf("\r\n");
    }
#else
    (void)display;
#endif
}

size_t VideoEGL_InstanceSize(void)
{
    return sizeof(VideoEGL);
}

int VideoEGL_Construct(VideoEGL *ve)
{
    EGLDisplay display;
    EGLContext context;
    EGLConfig config;
    EGLBoolean r;

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        return 1;
    }

    r = eglInitialize(display, NULL, NULL);
    switch (r) {
    case EGL_TRUE:
        break;                  /* ok */
    case EGL_FALSE:
        return 2;
    case EGL_BAD_DISPLAY:
        return 3;
    case EGL_NOT_INITIALIZED:
        return 4;
    default:
        assert(0);
    }

    PrintEGLConfigAttrib(display);

    {
        EGLint num;
        static const EGLint config_attrib_list[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        r = eglChooseConfig(display, config_attrib_list, &config, 1, &num);
        if (r != EGL_TRUE) {
            eglTerminate(ve->display);
            return 5;
        }
        /* printf("eglChooseConfig: %d\r\n", num); */
    }

    {
        static const EGLint context_attrib_list[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrib_list);
        if (context == 0) {
            eglTerminate(ve->display);
            return 6;
        }
    }

    ve->display = display;
    ve->context = context;
    ve->surface = 0;
    ve->config = config;
    memset(&ve->native_window, 0, sizeof(ve->native_window));
    return 0;
}

void VideoEGL_Destruct(VideoEGL *ve)
{
    eglDestroyContext(ve->display, ve->context);
    eglTerminate(ve->display);
    memset(ve, 0, sizeof(*ve));
}

int VideoEGL_CreateSurface(VideoEGL *ve, unsigned int native_window_element, int width, int height)
{
    EGL_DISPMANX_WINDOW_T *nw;
    nw = &ve->native_window;
    nw->element = (DISPMANX_ELEMENT_HANDLE_T)native_window_element;
    nw->width = width;
    nw->height = height;
    ve->surface = eglCreateWindowSurface(ve->display, ve->config, nw, NULL);
    return 0;
}

void VideoEGL_DestroySurface(VideoEGL *ve)
{
    eglDestroySurface(ve->display, ve->surface);
    ve->surface = 0;
}

int VideoEGL_MakeCurrent(VideoEGL *ve)
{
    return (eglMakeCurrent(ve->display, ve->surface, ve->surface, ve->context) == EGL_TRUE) ? 0 : 1;
}

int VideoEGL_UnmakeCurrent(VideoEGL *ve)
{
    eglMakeCurrent(ve->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return 0;
}


int VideoEGL_SwapBuffers(VideoEGL *ve)
{
    EGLBoolean r;
    r = eglSwapBuffers(ve->display, ve->surface);
    switch (r) {
    case EGL_TRUE:
        return 0;
    case EGL_FALSE:
        return 1;
    case EGL_BAD_DISPLAY:
        return 2;
    case EGL_NOT_INITIALIZED:
        return 3;
    case EGL_BAD_SURFACE:
        return 4;
    default:
        assert(0);
        return 5;
    }
}

