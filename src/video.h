/* -*- Mode: c; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

/* undocumented-dispmanx wrapper */

#ifndef INCLUDED_VIDEO_H
#define INCLUDED_VIDEO_H


#include <stddef.h>

typedef struct Video_ Video;

typedef enum {
	Video_DEVICE_ID_MAIN_LCD = 0,
	Video_DEVICE_ID_AUX_LCD = 1,
	Video_DEVICE_ID_HDMI = 2,
	Video_DEVICE_ID_SDTV = 3,
	Video_DEVICE_ID_ENUMS
} Video_DEVICE_ID;


size_t Video_InstanceSize(void); /* you need pre-allocate this size */
int Video_GetScreenSize(Video_DEVICE_ID device_id, int *out_width, int *out_height); /* the unit is 'pixel' */

int Video_ConstructWindow(Video *v,
                          Video_DEVICE_ID device_id,
                          int x, int y, int width, int height,
                          int layer);
void Video_DestructWindow(Video *v);

unsigned int Video_GetNativeWindowElement(Video *v);
void Video_SetSourceRect(Video *v, int x, int y, int width, int height);
void Video_SetWindowRect(Video *v, int x, int y, int width, int height);
int Video_ApplyChange(Video *v);
void Video_GetWindowSize(Video *v, int *out_width, int *out_height);
void Video_GetSourceSize(Video *v, int *out_width, int *out_height);


#endif
