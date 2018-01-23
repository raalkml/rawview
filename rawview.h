#ifndef _RAWVIEW_H_
#define _RAWVIEW_H_

#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>

struct window {
	xcb_connection_t *c;
	xcb_window_t w;
	xcb_gcontext_t fg;
	xcb_gcontext_t graph;
	xcb_rectangle_t graph_area;
	xcb_font_t font;
	xcb_rectangle_t size;
};

struct well_known_atom
{
	xcb_atom_t _NET_WM_WINDOW_TYPE;
	xcb_atom_t _NET_WM_WINDOW_TYPE_DIALOG;
};
extern struct well_known_atom ATOM;

#endif /* _RAWVIEW_H_ */
