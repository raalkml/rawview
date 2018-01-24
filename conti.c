#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "utils.h"
#include "rawview.h"

static uint8_t conti[256][256];

void conti_reset_graph(struct window *view)
{
	uint32_t mask = XCB_GC_FOREGROUND;
	uint32_t values[] = { view->colors.graph_bg };
	xcb_rectangle_t graph = { 0, 0, view->graph_area.width, view->graph_area.height };

	xcb_change_gc(view->c, view->graph, mask, values);
	xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, 1, &graph);
	memset(conti, 0, sizeof(conti));
}

static inline uint32_t lighten(uint32_t clr, uint32_t off)
{
	clr &= 0xff;
	clr += off & 0xff;
	return clr > 255 ? 255 : clr;
}

static inline uint32_t darken(uint32_t clr, uint32_t off)
{
	clr &= 0xff;
	off &= 0xff;
	return clr < off ? 0 : clr - off;
}

void conti_analyze(struct window *view, uint8_t buf[], size_t count)
{
	xcb_point_t pts[BUFSIZ / sizeof(xcb_point_t)];
	unsigned i, o = 0;
	uint32_t mask = XCB_GC_FOREGROUND;
	uint32_t values[] = { view->colors.graph_fg };

	xcb_change_gc(view->c, view->graph, mask, values);
	for (i = 1; i < count; ++i) {
		uint32_t clr;
		unsigned cnt = conti[buf[i - 1]][buf[i]] + 1;
		if (cnt < 256)
			conti[buf[i - 1]][buf[i]] = cnt;
		/* redden the frequent byte relationships */
		clr = lighten(view->colors.graph_fg >> 16, cnt * 4) << 16;
		clr |= darken(view->colors.graph_fg >> 8, cnt / 2) << 8;
		clr |= darken(view->colors.graph_fg, cnt / 2);
		pts[o].x = buf[i - 1];
		pts[o].y = buf[i];
		if (values[0] != clr || ++o == countof(pts)) {
			if (values[0] != clr) {
				values[0] = clr;
				xcb_change_gc(view->c, view->graph, mask, values);
			}
			xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->graph_pid, view->graph, o, pts);
			o = 0;
		}
	}
	if (o) {
		xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->graph_pid, view->graph, o, pts);
	}
}

