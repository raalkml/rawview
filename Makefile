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

rawview: rawview.o poll-fds.o conti.o bytes.o

.PHONY: profile
profile: CFLAGS = $(XCB_CFLAGS) -Wall -O2 -ggdb -pg -fprofile-arcs -ftest-coverage
profile: LDFLAGS = -O2 -ggdb -pg -fprofile-arcs -ftest-coverage -lgcov
profile: rawview

rawview.o poll-fds.o conti.o bytes.o: rawview.h
rawview.o poll-fds.o: poll-fds.h

.PHONY: clean
clean:
	$(RM) rawview *.o *.gcda
