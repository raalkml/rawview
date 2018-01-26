#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "utils.h"
#include "rawview.h"

static unsigned blk_offset;

static void start_block(struct window *view, off_t off)
{
	xcb_rectangle_t rect = { 0, 0, view->graph_area.width, view->graph_area.height };

	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &view->colors.graph_bg);
	xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, 1, &rect);
	blk_offset = 0;
}

static uint32_t classify(struct window *view, uint8_t byte)
{
	switch (byte) {
	case 0:
		return view->colors.graph_bg;
	case '\x20':
	case '\t': /* white space */
		return view->colors.graph_fg[0];
	case 1 ... 8:
	case 11: // VT
	case 12: // FF
	case 14 ... 31: /* control chars */
		return view->colors.graph_fg[1];
	case '\r':
	case '\n':
		return view->colors.graph_fg[2];
	case '0' ... '9':
		return view->colors.graph_fg[3];
	case 'A' ... 'Z':
	case 'a' ... 'z':
	case '_':
		return view->colors.graph_fg[4];
	case '!' ... '/':
	case ':' ... '@':
	case '[' ... '^':
	case '`':
	case '{' ... '~':
		return view->colors.graph_fg[5];
	case 127: /* DEL */
		return view->colors.graph_fg[8];
	case 128 ... 255:
		return view->colors.graph_fg[9];
	}
	return view->colors.graph_fg[7];
}

static void analyze(struct window *view, uint8_t buf[], size_t count)
{
	xcb_point_t pts[BUFSIZ / sizeof(xcb_point_t)];
	unsigned i, o = 0;
	uint32_t curclr = view->colors.graph_fg[0];
	int blkwidth = view->graph_area.width;

	if (blkwidth < 2)
		blkwidth = 2;
	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);
	for (i = 0; i < count; ++i) {
		uint32_t clr = classify(view, buf[i]);
		pts[o].x = view->graph_area.width - blk_offset % blkwidth;
		pts[o].y = blk_offset / blkwidth;
		if (curclr != clr || ++o == countof(pts)) {
			if (curclr != clr) {
				curclr = clr;
				xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);
			}
			xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->graph_pid, view->graph, o, pts);
			o = 0;
		}
		++blk_offset;
		if (blk_offset >= view->graph_area.width * view->graph_area.height)
			break;
	}
	if (o)
		xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->graph_pid, view->graph, o, pts);
}

static void resize(struct window *view)
{
}

struct graph_desc bytes_graph = {
	.name = "bytes",
	.width = 256,
	.height = 256,
	.start_block = start_block,
	.resize = resize,
	.analyze = analyze,
};

