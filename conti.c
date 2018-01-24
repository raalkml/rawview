#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "utils.h"
#include "rawview.h"

static uint8_t conti[256][256];

static void reset(struct window *view)
{
	uint32_t mask = XCB_GC_FOREGROUND;
	uint32_t values[] = { view->colors.graph_bg };
	xcb_rectangle_t graph = { 0, 0, view->graph_area.width, view->graph_area.height };

	xcb_change_gc(view->c, view->graph, mask, values);
	xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, 1, &graph);
	memset(conti, 0, sizeof(conti));
}

static void analyze(struct window *view, uint8_t buf[], size_t count)
{
	xcb_point_t pts[BUFSIZ / sizeof(xcb_point_t)];
	unsigned i, o = 0;
	uint32_t mask = XCB_GC_FOREGROUND;
	uint32_t values[] = { view->colors.graph_fg[0] };

	xcb_change_gc(view->c, view->graph, mask, values);
	for (i = 1; i < count; ++i) {
		uint32_t clr;
		unsigned cnt = conti[buf[i - 1]][buf[i]];

		if (cnt < 255)
			conti[buf[i - 1]][buf[i]] = ++cnt;
		clr = view->colors.graph_fg[countof(view->colors.graph_fg) * cnt / 256];
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

struct graph_desc conti_graph = {
	.name = "conti",
	.width = 256,
	.height = 256,
	.reset = reset,
	.analyze = analyze,
};
