/* -*- Mode: c; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

#ifndef INCLUDED_VIDEO_EGL_H
#define INCLUDED_VIDEO_EGL_H


#include <stddef.h>


typedef struct VideoEGL_ VideoEGL;


size_t VideoEGL_InstanceSize(void);

int VideoEGL_Construct(VideoEGL *ve);
void VideoEGL_Destruct(VideoEGL *ve);

int VideoEGL_CreateSurface(VideoEGL *ve, unsigned int native_window_element, int width, int height);
void VideoEGL_DestroySurface(VideoEGL *ve);

int VideoEGL_MakeCurrent(VideoEGL *ve);
int VideoEGL_UnmakeCurrent(VideoEGL *ve);

int VideoEGL_SwapBuffers(VideoEGL *ve);


#endif
