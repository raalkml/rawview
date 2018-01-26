#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "utils.h"
#include "rawview.h"

static off_t offset;
static size_t blk_offset;
static size_t blk_size;

static unsigned byte_width;
static unsigned bytes_per_line;
static unsigned byte_height;
static int blk_x;

static unsigned calc_bytes_per_line(struct window *view, unsigned bw)
{
	unsigned bpl = (unsigned)(view->graph_area.width - blk_x) / bw;
	unsigned undrawn_line_part = (unsigned)(view->graph_area.width - blk_x) - bpl * bw;
	if (undrawn_line_part > 4)
		bpl++;
	return bpl;
}

static unsigned calc_graph_height(unsigned bh, unsigned bpl)
{
	return (blk_size / bpl + !!(blk_size % bpl)) * bh;
}

static void layout(struct window *view)
{
	unsigned max_bytes, height;

	blk_x = view->graph_area.width / 2 + 1;
	max_bytes = (unsigned)(view->graph_area.width - blk_x) * view->graph_area.height;
	byte_width = 1;
	byte_height = 1;
	bytes_per_line = calc_bytes_per_line(view, byte_width);

	if (blk_size >= max_bytes)
		return;
	height = calc_graph_height(byte_height, bytes_per_line);
	for (;;) {
		unsigned bw = byte_width + 1;
		unsigned bh = byte_height + 1;
		unsigned bpl = calc_bytes_per_line(view, bw);
		unsigned h = calc_graph_height(bh, bpl);

		trace("%s: bw %u bh %u height %u %s %u\n", __func__,
		      bw, bh, h,
		      h < view->graph_area.height ? "<" :
		      h > view->graph_area.height ? ">" : "==",
		      view->graph_area.height);
		if (h > view->graph_area.height)
			break;
		height = h;
		byte_width = bw;
		byte_height = bh;
		bytes_per_line = bpl;
	}
	if (height >= view->graph_area.height)
		return;
	for (;;) {
		unsigned h = calc_graph_height(byte_height, bytes_per_line - 1);

		trace("%s: bw %u bh %u height %u %s %u\n", __func__,
		      byte_width, byte_height, h,
		      h < view->graph_area.height ? "<" :
		      h > view->graph_area.height ? ">" : "==",
		      view->graph_area.height);
		if (h > view->graph_area.height)
			break;
		height = h;
		bytes_per_line--;
		break;
	}
	if (height >= view->graph_area.height)
		return;
	for (;;) {
		unsigned bh = byte_height + 1;
		unsigned h = calc_graph_height(bh, bytes_per_line);

		trace("%s: bw %u bh %u height %u %s %u\n", __func__,
		      byte_width, bh, h,
		      h < view->graph_area.height ? "<" :
		      h > view->graph_area.height ? ">" : "==",
		      view->graph_area.height);
		if (h > view->graph_area.height)
			break;
		height = h;
		byte_height = bh;
	}
	if (height >= view->graph_area.height)
		return;
	for (;;) {
		unsigned h = calc_graph_height(byte_height, bytes_per_line - 1);

		trace("%s: bw %u bh %u height %u %s %u\n", __func__,
		      byte_width, byte_height, h,
		      h < view->graph_area.height ? "<" :
		      h > view->graph_area.height ? ">" : "==",
		      view->graph_area.height);
		if (h > view->graph_area.height)
			break;
		height = h;
		bytes_per_line--;
		break;
	}
	if (height >= view->graph_area.height)
		return;
	for (;;) {
		unsigned bw = byte_width + 1;
		unsigned bpl = calc_bytes_per_line(view, bw);
		unsigned h = calc_graph_height(byte_height, bpl);

		trace("%s: bw %u bh %u height %u %s %u\n", __func__,
		      bw, byte_height, h,
		      h < view->graph_area.height ? "<" :
		      h > view->graph_area.height ? ">" : "==",
		      view->graph_area.height);
		if (h > view->graph_area.height)
			break;
		height = h;
		byte_width = bw;
		bytes_per_line = bpl;
	}
	if (height >= view->graph_area.height)
		return;
	for (;;) {
		unsigned h = calc_graph_height(byte_height, bytes_per_line - 1);

		trace("%s: bw %u bh %u height %u %s %u\n", __func__,
		      byte_width, byte_height, h,
		      h < view->graph_area.height ? "<" :
		      h > view->graph_area.height ? ">" : "==",
		      view->graph_area.height);
		if (h > view->graph_area.height)
			break;
		height = h;
		bytes_per_line--;
		break;
	}
	if (height >= view->graph_area.height)
		return;
	byte_width = 1;
	byte_height = 1;
	bytes_per_line = calc_bytes_per_line(view, byte_width);
	for (;;) {
		unsigned bh = byte_height + 1;
		unsigned h = calc_graph_height(bh, bytes_per_line);

		trace("%s: bw %u bh %u height %u %s %u\n", __func__,
		      byte_width, bh, h,
		      h < view->graph_area.height ? "<" :
		      h > view->graph_area.height ? ">" : "==",
		      view->graph_area.height);
		if (h > view->graph_area.height)
			break;
		height = h;
		byte_height = bh;
	}
	if (height >= view->graph_area.height)
		return;
	for (;;) {
		unsigned h = calc_graph_height(byte_height, bytes_per_line - 1);

		trace("%s: bw %u bh %u height %u %s %u\n", __func__,
		      byte_width, byte_height, h,
		      h < view->graph_area.height ? "<" :
		      h > view->graph_area.height ? ">" : "==",
		      view->graph_area.height);
		if (h > view->graph_area.height)
			break;
		height = h;
		bytes_per_line--;
		break;
	}
}

static void start_block(struct window *view, off_t off)
{
	offset = off;
	blk_offset = 0;
	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &view->colors.graph_bg);
	xcb_rectangle_t rect = { 0 /* blk_x */, 0, view->graph_area.width /* - blk_x */, view->graph_area.height };
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

static void analyze(struct window *view, uint8_t buf[], size_t count)
{
	xcb_rectangle_t rect;
	xcb_rectangle_t rts[BUFSIZ / sizeof(xcb_rectangle_t)];
	unsigned i, o;
	uint32_t curclr = view->colors.graph_fg[0];

	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);
	for (i = o = 0; i < count; ++i) {
		uint32_t clr = classify(view, buf[i]);

		rts[o].width = byte_width;
		rts[o].x = blk_x + (blk_offset % bytes_per_line) * byte_width;
		rts[o].height = byte_height;
		rts[o].y = (blk_offset / bytes_per_line) * byte_height;
		rect.x = rts[o].x + rts[o].width;
		rect.y = rts[o].y;
		++o;
		if (curclr != clr || o == countof(rts)) {
			if (curclr != clr) {
				curclr = clr;
				xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, &curclr);
			}
			xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, o, rts);
			o = 0;
		}
		++blk_offset;
		if (blk_offset > blk_size) {
			trace("%s: off %u size %u\n", __func__, blk_offset, blk_size);
			break;
		}
	}
	if (o)
		xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, o, rts);
	/* make the unused part of the graph area visible */
	xcb_change_gc(view->c, view->graph, XCB_GC_FOREGROUND, view->colors.graph_fg + 6);
	rect.width = bytes_per_line * byte_width - (rect.x - blk_x);
	rect.height = byte_height;
	if (rect.width)
		xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, 1, &rect);
	rect.x = blk_x;
	rect.y += byte_height;
	rect.width = bytes_per_line * byte_width;
	rect.height = view->graph_area.height - rect.y;
	if (rect.height)
		xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, 1, &rect);
}

static void setup(struct window *view, size_t blk)
{
	blk_size = blk;
	layout(view);
	trace("%s: blk %u\n", __func__, blk);
}

struct graph_desc bytes_graph = {
	.name = "bytes",
	.width = 256,
	.height = 512,
	.start_block = start_block,
	.setup = setup,
	.analyze = analyze,
};

