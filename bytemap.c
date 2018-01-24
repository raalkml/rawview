#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "utils.h"
#include "rawview.h"

static unsigned offset;

static void reset(struct window *view)
{
	xcb_rectangle_t rect = { 0, 0, view->graph_area.width, view->graph_area.height };

	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &view->colors.graph_bg);
	xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, 1, &rect);
	offset = 0;
}

static void analyze(struct window *view, uint8_t buf[], size_t count)
{
	xcb_point_t pts[BUFSIZ / sizeof(xcb_point_t)];
	unsigned i, o = 0;
	uint32_t curclr = view->colors.graph_fg[0];

	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);
	for (i = 0; i < count; ++i) {
		uint32_t clr;
		switch (buf[i]) {
		case 0:
			clr = view->colors.graph_bg;
			break;
		case '\x20':
		case '\t': /* white space */
			clr = view->colors.graph_fg[0];
			break;
		case 1 ... 8:
		case 11: // VT
		case 12: // FF
		case 14 ... 31: /* control chars */
			clr = view->colors.graph_fg[1];
			break;
		case '\r':
		case '\n':
			clr = view->colors.graph_fg[2];
			break;
		case '0' ... '9':
			clr = view->colors.graph_fg[3];
			break;
		case 'A' ... 'Z':
		case 'a' ... 'z':
		case '_':
			clr = view->colors.graph_fg[4];
			break;
		case '!' ... '/':
		case ':' ... '@':
		case '[' ... '^':
		case '`':
		case '{' ... '~':
			clr = view->colors.graph_fg[5];
			break;
		case 127: /* DEL */
			clr = view->colors.graph_fg[8];
			break;
		case 128 ... 255:
			clr = view->colors.graph_fg[9];
			break;
		default:
			clr = view->colors.graph_fg[7];
			break;
		}
		pts[o].x = offset % 256;
		pts[o].y = offset / 256;
		if (curclr != clr || ++o == countof(pts)) {
			if (curclr != clr) {
				curclr = clr;
				xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);
			}
			xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->graph_pid, view->graph, o, pts);
			o = 0;
		}
		++offset;
		if (offset >= 256 * 256)
			break;
	}
	if (o) {
		xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->graph_pid, view->graph, o, pts);
	}
}

struct graph_desc bytemap_graph = {
	.name = "bytemap",
	.width = 256,
	.height = 256,
	.reset = reset,
	.analyze = analyze,
};

