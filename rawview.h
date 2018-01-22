#ifndef _RAWVIEW_H_
#define _RAWVIEW_H_

#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>

struct window {
	xcb_connection_t *c;
	xcb_window_t w;
	xcb_gcontext_t fg;
};

struct well_known_atom
{
	xcb_atom_t _NET_WM_WINDOW_TYPE;
	xcb_atom_t _NET_WM_WINDOW_TYPE_DIALOG;
};
extern struct well_known_atom ATOM;

#endif /* _RAWVIEW_H_ */
