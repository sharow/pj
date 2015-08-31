/* -*- Mode: c; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <bcm_host.h>

#include "config.h"
#include "base.h"
#include "video.h"

struct Video_ {
    struct {
        DISPMANX_DISPLAY_HANDLE_T display;
        DISPMANX_ELEMENT_HANDLE_T element;
    } dispmanx;
    struct {
        VC_RECT_T rect;
    } window, source;
};


static int VideoDeviceIdToDispmanxId(Video_DEVICE_ID device_id)
{
    return device_id;
}

static DISPMANX_UPDATE_HANDLE_T StartUpdate(void)
{
    return vc_dispmanx_update_start(sched_get_priority_max(SCHED_OTHER));
}

static void EndUpdate(DISPMANX_UPDATE_HANDLE_T update)
{
    vc_dispmanx_update_submit_sync(update);
}


int Video_GetScreenSize(Video_DEVICE_ID device_id, int *out_width, int *out_height)
{
    uint32_t width;
    uint32_t height;
    graphics_get_display_size(VideoDeviceIdToDispmanxId(device_id),
                              &width, &height);
    *out_width = (int)width;
    *out_height = (int)height;
    return 0;
}

size_t Video_InstanceSize(void)
{
    return sizeof(Video);
}

int Video_ConstructWindow(Video *v,
                          Video_DEVICE_ID device_id,
                          int x, int y, int width, int height,
                          int layer)
{
    int dispmanx_device_id;
    VC_RECT_T dest_rect;
    VC_RECT_T src_rect;
    DISPMANX_DISPLAY_HANDLE_T display;
    DISPMANX_ELEMENT_HANDLE_T element;
    DISPMANX_UPDATE_HANDLE_T update;

    vc_dispmanx_rect_set(&dest_rect, x, y, width, height);
    vc_dispmanx_rect_set(&src_rect, 0, 0, width << 16, height << 16);

    update = StartUpdate();
    if (update == DISPMANX_NO_HANDLE) {
        return 1;
    }
    dispmanx_device_id = VideoDeviceIdToDispmanxId(device_id);
    display = vc_dispmanx_display_open(dispmanx_device_id);
    element = vc_dispmanx_element_add(update,
                                      display,
                                      layer,
                                      &dest_rect,
                                      (DISPMANX_RESOURCE_HANDLE_T)0,
                                      &src_rect,
                                      DISPMANX_PROTECTION_NONE,
                                      (VC_DISPMANX_ALPHA_T *)NULL,
                                      (DISPMANX_CLAMP_T *)NULL,
                                      DISPMANX_NO_ROTATE);
    EndUpdate(update);

    v->dispmanx.display = display;
    v->dispmanx.element = element;
    v->window.rect = dest_rect;
    v->source.rect = src_rect;
    return 0;
}

void Video_DestructWindow(Video *v)
{
    DISPMANX_UPDATE_HANDLE_T update;

    update = StartUpdate();
    if (update == DISPMANX_NO_HANDLE) {
        assert(0);
    }
    vc_dispmanx_element_remove(update, v->dispmanx.element);
    vc_dispmanx_display_close(v->dispmanx.display);
    EndUpdate(update);

    memset(v, 0, sizeof(*v));
}

unsigned int Video_GetNativeWindowElement(Video *v)
{
    return (unsigned int)v->dispmanx.element;
}

void Video_SetSourceRect(Video *v, int x, int y, int width, int height)
{
    vc_dispmanx_rect_set(&v->source.rect, x << 16, y << 16, width << 16, height << 16);
}

void Video_SetWindowRect(Video *v, int x, int y, int width, int height)
{
    vc_dispmanx_rect_set(&v->window.rect, x, y, width, height);
}

int Video_ApplyChange(Video *v)
{
    int ret;
    DISPMANX_UPDATE_HANDLE_T update;

    /* value from interface/vmcs_host/vc_vchi_dispmanx.h */
    const uint32_t ELEMENT_CHANGE_DEST_RECT = (1 << 2);
    const uint32_t ELEMENT_CHANGE_SRC_RECT  = (1 << 3);
    const uint32_t mode = ELEMENT_CHANGE_SRC_RECT | ELEMENT_CHANGE_DEST_RECT;

    update = StartUpdate();
    if (update == DISPMANX_NO_HANDLE) {
        return 1;
    }
    ret = vc_dispmanx_element_change_attributes(update,
                                                v->dispmanx.element,
                                                mode,
                                                0, /* layer */
                                                0xff, /* opacity */
                                                &v->window.rect,
                                                &v->source.rect,
                                                (DISPMANX_RESOURCE_HANDLE_T)0, /* mask */
                                                DISPMANX_NO_ROTATE);
    EndUpdate(update);
    return ret;
}

void Video_GetWindowSize(Video *v, int *out_width, int *out_height)
{
    *out_width = (int)v->window.rect.width;
    *out_height = (int)v->window.rect.height;
}

void Video_GetSourceSize(Video *v, int *out_width, int *out_height)
{
    *out_width = (int)(v->source.rect.width >> 16);
    *out_height = (int)(v->source.rect.height >> 16);
}

