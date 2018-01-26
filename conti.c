#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "utils.h"
#include "rawview.h"

static uint8_t conti[256][256];

static void start_block(struct window *view, off_t off)
{
	uint32_t mask = XCB_GC_FOREGROUND;
	uint32_t values[] = { view->colors.graph_bg };
	xcb_rectangle_t graph = { 0, 0, view->graph_area.width, view->graph_area.height };

	xcb_change_gc(view->c, view->graph, mask, values);
	xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, 1, &graph);
	memset(conti, 0, sizeof(conti));
}

static void analyze_points(struct window *view, uint8_t buf[], size_t count)
{
	xcb_point_t pts[BUFSIZ / sizeof(xcb_point_t)];
	unsigned i, o = 0;
	uint32_t curclr = view->colors.graph_fg[0];

	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);
	for (i = 1; i < count; ++i) {
		uint32_t clr;
		unsigned cnt = conti[buf[i - 1]][buf[i]];

		if (cnt < 255)
			conti[buf[i - 1]][buf[i]] = ++cnt;
		clr = view->colors.graph_fg[countof(view->colors.graph_fg) * cnt / 256];
		pts[o].x = buf[i - 1] * view->graph_area.width / 256;
		pts[o].y = buf[i] * view->graph_area.height / 256;
		++o;
		if (curclr != clr || o == countof(pts)) {
			if (curclr != clr) {
				curclr = clr;
				xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);
			}
			xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->graph_pid, view->graph, o, pts);
			o = 0;
		}
	}
	if (o) {
		xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->graph_pid, view->graph, o, pts);
	}
}

static void analyze_rects(struct window *view, uint8_t buf[], size_t count)
{
	xcb_rectangle_t rts[BUFSIZ / sizeof(xcb_rectangle_t)];
	unsigned i, o = 0;
	uint32_t curclr = view->colors.graph_fg[0];
	int16_t w = view->graph_area.width / 256, h = view->graph_area.height / 256;

	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);
	for (i = 1; i < count; ++i) {
		uint32_t clr;
		unsigned cnt = conti[buf[i - 1]][buf[i]];

		if (cnt < 255)
			conti[buf[i - 1]][buf[i]] = ++cnt;
		clr = view->colors.graph_fg[countof(view->colors.graph_fg) * cnt / 256];
		rts[o].x = buf[i - 1] * view->graph_area.width / 256;
		rts[o].y = buf[i] * view->graph_area.height / 256;
		rts[o].width = w + 1;
		rts[o].height = h + 1;
		if (curclr != clr || ++o == countof(rts)) {
			if (curclr != clr) {
				curclr = clr;
				xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);
			}
			xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, o, rts);
			o = 0;
		}
	}
	if (o) {
		xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, o, rts);
	}
}

static void analyze(struct window *view, uint8_t buf[], size_t count)
{
	if (view->graph_area.width > 256 || view->graph_area.height > 256)
		analyze_rects(view, buf, count);
	else
		analyze_points(view, buf, count);
}

struct graph_desc conti_graph = {
	.name = "conti",
	.width = 256,
	.height = 256,
	.start_block = start_block,
	.analyze = analyze,
};
