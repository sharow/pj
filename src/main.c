/* -*- Mode: c; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "config.h"
#include "base.h"
#include "pj.h"

#ifdef USE_TERMIOS
#include <fcntl.h>
#include <termios.h>
#endif


static void PrintHead(void)
{
    printf("PJ version %x.%x\r\n", VERSION_MAJOR, VERSION_MINOR);
    printf("build %s\r\n", __DATE__);
}

static void PrintCommandUsage(void)
{
    printf("usage: pj [options] <layer0.glsl> [layer1.glsl] [layer2.glsl] ...\r\n");
    printf("options:\r\n");
    printf("  offscreen format:\r\n");
    printf("    --RGB888\r\n");
    printf("    --RGBA8888 (default)\r\n");
    printf("    --RGB565\r\n");
    printf("  interpolation mode:\r\n");
    printf("    --nearestneighbor (default)\r\n");
    printf("    --bilinear\r\n");
    printf("  wrap mode:\r\n");
    printf("    --wrap-clamp_to_edge\r\n");
    printf("    --wrap-repeat (default)\r\n");
    printf("    --wrap-mirror_repeat\r\n");
    printf("  backbuffer:\r\n");
    printf("    --backbuffer   enable backbuffer(default:OFF)\r\n");
    printf("\r\n");
}

int main(int argc, char *argv[])
{
    int ret;

#ifdef USE_TERMIOS
    struct termios ios_old, ios_new;

    tcgetattr(STDIN_FILENO, &ios_old);
    tcgetattr(STDIN_FILENO, &ios_new);
    cfmakeraw(&ios_new);
    tcsetattr(STDIN_FILENO, 0, &ios_new);
    fcntl(0, F_SETFL, O_NONBLOCK);
#endif

    PJContext_HostInitialize();

    PrintHead();
    ret = EXIT_SUCCESS;

    if (argc == 1) {
        PrintCommandUsage();
    } else {
        PJContext *pj;
        pj = malloc(PJContext_InstanceSize());
        PJContext_Construct(pj);
        if (PJContext_ParseArgs(pj, argc, (const char **)argv) == 0) {
            ret = PJContext_Main(pj);
        }
        PJContext_Destruct(pj);
        free(pj);
    }

    PJContext_HostDeinitialize();

#ifdef USE_TERMIOS
    tcsetattr(STDIN_FILENO, 0, &ios_old);
#endif
    return ret;
}

