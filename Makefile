XCB_LIBS := $(shell pkg-config --libs xcb-atom)
XCB_CFLAGS := $(shell pkg-config --cflags xcb)

CFLAGS = $(XCB_CFLAGS) -Wall -O2 -ggdb
LDFLAGS = -O2 -ggdb
LOADLIBES = $(XCB_LIBS)

rawview: rawview.o

.PHONY: clean
clean:
	$(RM) rawview rawview.o
