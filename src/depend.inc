main.o: main.c config.h base.h pj.h
pj.o: pj.c config.h base.h pj.h graphics.h
video.o: video.c config.h base.h video.h
video_egl.o: video_egl.c config.h base.h video_egl.h
graphics.o: graphics.c config.h base.h video.h video_egl.h graphics.h
