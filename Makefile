XCB_LIBS := \
    $(shell pkg-config --libs xcb-atom) \
    $(shell pkg-config --libs xcb) \
    $(shell pkg-config --libs xcb-keysyms) \

XCB_CFLAGS := \
    $(shell pkg-config --cflags xcb) \
    $(shell pkg-config --cflags xcb-atom) \
    $(shell pkg-config --cflags xcb-keysyms) \

CFLAGS = $(XCB_CFLAGS) -Wall -O2 -ggdb
LDFLAGS = -O2 -ggdb
LOADLIBES = $(XCB_LIBS)

rawview: rawview.o poll-fds.o conti.o bytemap.o

rawview.o poll-fds.o conti.o bytemap.o: rawview.h
rawview.o poll-fds.o: poll-fds.h

.PHONY: clean
clean:
	$(RM) rawview *.o
