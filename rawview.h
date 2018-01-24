#ifndef _RAWVIEW_H_
#define _RAWVIEW_H_

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
	xcb_rectangle_t status_area;
	char status_line[100];
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

	void (*reset)(struct window *);
	void (*analyze)(struct window *, uint8_t buf[], size_t count);
};

extern struct graph_desc conti_graph;

int trace(const char *fmt, ...);

#endif /* _RAWVIEW_H_ */
