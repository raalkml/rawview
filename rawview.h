#ifndef _RAWVIEW_H_
#define _RAWVIEW_H_

#include <stdint.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include "poll-fds.h"

struct window
{
	xcb_connection_t *c;
	xcb_window_t w;
	xcb_gcontext_t fg;
	xcb_gcontext_t graph;
	xcb_pixmap_t graph_pid;
	xcb_rectangle_t graph_area;
	xcb_font_t font;
	xcb_rectangle_t size;
	struct
	{
		uint32_t red;
		uint32_t green;
		uint32_t blue;
		uint32_t white;
		uint32_t black;
		uint32_t border;
		uint32_t graph_fg[10];
		uint32_t graph_bg;
	} colors;
	unsigned int font_height, font_base;
	xcb_rectangle_t status_area;
	char status_line1[100];
	char status_line2[100];
};

struct well_known_atom
{
	xcb_atom_t _NET_WM_WINDOW_TYPE;
	xcb_atom_t _NET_WM_WINDOW_TYPE_DIALOG;
};
extern struct well_known_atom ATOM;

struct graph_desc
{
	const char *name;
	unsigned int width, height;

	void (*start_block)(struct window *, off_t offset);
	void (*setup)(struct window *, size_t blk);
	void (*analyze)(struct window *, uint8_t buf[], size_t count);
};

extern struct graph_desc conti_graph;
extern struct graph_desc bytes_graph;

extern char RAWVIEW[];

#define trace(...) trace_if(1, __VA_ARGS__)
extern int trace_if(int level, const char *fmt, ...);

#define error(fmt, ...) printf_error("%s: " fmt "\n", RAWVIEW, ## __VA_ARGS__)
extern int printf_error(const char *fmt, ...);

#endif /* _RAWVIEW_H_ */
