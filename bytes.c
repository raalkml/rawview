#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "utils.h"
#include "rawview.h"

static off_t offset;
static size_t blk_size;

static unsigned byte_width;
static unsigned bytes_per_row;
static unsigned byte_height;
static unsigned vert_fill, vert_step;
static int blk_left, blk_x, blk_y, blk_row, blk_col;

static inline unsigned sub0(unsigned a, unsigned b)
{
	return a > b ? a - b : 0;
}

static unsigned calc_bytes_per_row(struct window *view, unsigned bw)
{
	unsigned bpr = (unsigned)(view->graph_area.width - blk_left) / bw;
	unsigned undrawn_line_part = (unsigned)(view->graph_area.width - blk_left) - bpr * bw;
	if (undrawn_line_part > 4)
		bpr++;
	/* allow no incomplete rows */
	//while (blk_size % bpr)
	//	--bpr;
	return bpr;
}

static unsigned calc_graph_rows(unsigned bpr)
{
	return blk_size / bpr + !!(blk_size % bpr);
}

static unsigned calc_graph_height(unsigned bh, unsigned bpr)
{
	return calc_graph_rows(bpr) * bh;
}

static void layout(struct window *view)
{
	unsigned max_bytes;

	blk_left = view->graph_area.width / 2 + 1;
	max_bytes = (unsigned)(view->graph_area.width - blk_left) * view->graph_area.height;
	byte_width = 1;
	byte_height = 1;
	bytes_per_row = calc_bytes_per_row(view, byte_width);
	vert_fill = 0;
	vert_step = 0;

	trace_if(2, "%s: bw %u bh %u bpr %u blk %u max %u\n", __func__,
	      byte_width, byte_width, bytes_per_row, blk_size, max_bytes);
	if (blk_size >= max_bytes)
		return;
	for (;;) {
		unsigned bw = byte_width + 1;
		unsigned bh = byte_height + 1;
		unsigned bpr = calc_bytes_per_row(view, bw);
		//bw = (unsigned)(view->graph_area.width - blk_left) / bpr;
		unsigned nrows = calc_graph_rows(bpr);
		unsigned h = calc_graph_height(bh, bpr);
		unsigned vf = view->graph_area.height - h;
		unsigned vs = vf / nrows + 1;

		trace_if(2, "%s: bw %u bh %u bpr %u fill %u a %u rows %u; height %u %s %u\n", __func__,
		      bw, bh, bpr, vf, vs, nrows,
		      h,
		      h < view->graph_area.height ? "<" : h > view->graph_area.height ? ">" : "==",
		      view->graph_area.height);
		if (h > view->graph_area.height)
			break;
		byte_width = bw;
		byte_height = bh;
		bytes_per_row = bpr;
		vert_fill = vf;
		vert_step = vs;
	}
}

static void start_block(struct window *view, off_t off)
{
	offset = off;
	blk_x = 0;
	blk_y = 0;
	blk_row = 0;
	blk_col = 0;
	vert_fill = sub0(view->graph_area.height, calc_graph_height(byte_height, bytes_per_row));
	vert_step = vert_fill / calc_graph_rows(bytes_per_row) + 1;
	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &view->colors.graph_bg);
	xcb_rectangle_t rect = { 0 /* blk_left */, 0, view->graph_area.width /* - blk_left */, view->graph_area.height };
	xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, 1, &rect);

	trace("%s: file off %lld\n", __func__, (long long)off);
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

static inline unsigned calc_row_height(unsigned row)
{
	unsigned h = byte_height + vert_step;
	if (vert_fill)
		vert_fill -= vert_step;
	else
		vert_step = 0;
	return h;
}

static void analyze(struct window *view, uint8_t buf[], size_t count)
{
	xcb_rectangle_t rect;
	xcb_rectangle_t rts[BUFSIZ / sizeof(xcb_rectangle_t)];
	unsigned i, o;
	uint32_t curclr = view->colors.graph_fg[0];
	unsigned row_height = calc_row_height(blk_row);

	trace_if(2,"row %u (bh %u, rh %u, vert %u a %u)\n", blk_row,
		 byte_height, row_height, vert_fill, vert_step);
	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);

	for (i = o = 0; i < count; ++i) {
		uint32_t clr = classify(view, buf[i]);

		rts[o].x = blk_left + blk_x;
		rts[o].y = blk_y;
		rts[o].width = byte_width;
		rts[o].height = row_height;
		blk_x += byte_width;
		if (++blk_col == bytes_per_row) {
			blk_x = 0;
			blk_y += row_height;
			row_height = calc_row_height(++blk_row);
			blk_col = 0;
			trace_if(3, "row %u (%u, %u, vert %u a %u)\n", blk_row,
				 byte_height, row_height, vert_fill, vert_step);
		}
		++o;
		if (curclr != clr || o == countof(rts)) {
			if (curclr != clr) {
				curclr = clr;
				xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);
			}
			xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, o, rts);
			o = 0;
		}
	}
	if (o)
		xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, o, rts);

	/* make the unused part of the graph area visible */
	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, view->colors.graph_fg + 6);
	rect.x = blk_left + blk_x;
	rect.y = blk_y;
	rect.width = bytes_per_row * byte_width - blk_x;
	rect.height = row_height;
	if (rect.width)
		xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, 1, &rect);
	rect.x = blk_left;
	rect.y += rect.height;
	rect.width = bytes_per_row * byte_width;
	if (view->graph_area.height > rect.y) {
		rect.height = view->graph_area.height - rect.y;
		xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, 1, &rect);
	}
}

static void setup(struct window *view, size_t blk)
{
	trace("%s: blk %u\n", __func__, blk);
	blk_size = blk;
	layout(view);
}

struct graph_desc bytes_graph = {
	.name = "bytes",
	.width = 256,
	.height = 512,
	.start_block = start_block,
	.setup = setup,
	.analyze = analyze,
};

