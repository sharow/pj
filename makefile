# PiJockey for RaspberryPi

TARGET=pj
VERSION=0.3

all:
	make -C src $@
	cp -fu src/$(TARGET) ./

clean:
	make -C src clean
	rm -f $(TARGET)

init: clean depend

depend:
	make -C src depend

