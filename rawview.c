#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <xcb/xcb_ewmh.h>
#include "rawview.h"

/* geometric objects */
static const xcb_point_t          points[] = {
	{10, 10},
	{10, 20},
	{20, 10},
	{20, 20}};

static const xcb_point_t          polyline[] = {
	{50, 10},
	{ 5, 20},     /* rest of points are relative */
	{25,-20},
	{10, 10}};

static const xcb_segment_t        segments[] = {
	{100, 10, 140, 30},
	{110, 25, 130, 60}};

static const xcb_rectangle_t      rectangles[] = {
	{ 10, 50, 40, 20},
	{ 80, 50, 10, 40}};

static const xcb_arc_t arcs[] = {
	{10, 100, 60, 40, 0, 90 << 6},
	{90, 100, 55, 40, 0, 270 << 6}};

static struct window *create_rawview_window(xcb_connection_t *c, const char *title, const char *icon)
{
	struct window *view = calloc(1, sizeof(*view));

	if (!view)
		goto fail;

	view->c = c;
	/* get the first screen */
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;

	/* Create black (foreground) graphic context */
	view->fg = xcb_generate_id(view->c);
	uint32_t mask      = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
	uint32_t values[3] = { screen->white_pixel, 0 };

	xcb_create_gc(view->c, view->fg, screen->root, mask, values);
	view->w = xcb_generate_id(view->c);
	mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK;
	values[0] = screen->black_pixel;
	values[1] = screen->white_pixel;
	values[2] = XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_KEY_PRESS    |
		XCB_EVENT_MASK_KEY_RELEASE;
	xcb_create_window(view->c,
			  XCB_COPY_FROM_PARENT,	/* depth */
			  view->w,
			  screen->root,		/* parent window */
			  0, 0,			/* x, y */
			  300, 300,		/* width, height */
			  2,			/* border_width */
			  XCB_WINDOW_CLASS_INPUT_OUTPUT, /* class */
			  screen->root_visual,	/* visual */
			  mask, values);
	xcb_change_property(view->c,
			    XCB_PROP_MODE_REPLACE,
			    view->w,
			    XCB_ATOM_WM_NAME,
			    XCB_ATOM_STRING, 8,
			    strlen(title), title);
	if (!icon)
		icon = title;
	xcb_change_property(view->c,
			    XCB_PROP_MODE_REPLACE,
			    view->w,
			    XCB_ATOM_WM_ICON_NAME,
			    XCB_ATOM_STRING, 8,
			    strlen(icon), icon);
	xcb_change_property(view->c,
			    XCB_PROP_MODE_REPLACE,
			    view->w,
			    ATOM._NET_WM_WINDOW_TYPE,
			    XCB_ATOM_ATOM,
			    32,
			    1,
			    (unsigned char *)&ATOM._NET_WM_WINDOW_TYPE_DIALOG);

fail:
	return view;
}

static void expose_view(struct window *view)
{
	/*
	xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->w, view->fg, 4, points);
	xcb_poly_line(view->c, XCB_COORD_MODE_PREVIOUS, view->w, view->fg, 4, polyline);
	xcb_poly_segment(view->c, view->w, view->fg, 2, segments);
	xcb_poly_rectangle(view->c, view->w, view->fg, 2, rectangles);
	xcb_poly_arc(view->c, view->w, view->fg, 2, arcs);
	xcb_flush(view->c);
	*/
}

static ssize_t read_input(int fd, struct window *view)
{
	static uint8_t buf[BUFSIZ];
	static xcb_point_t pts[BUFSIZ];
	ssize_t rd = read(fd, buf, BUFSIZ);
	//fprintf(stderr, "%ld %s\n", (long)rd, rd < 0 ? strerror(errno) : "");
	if (rd <= 0 || rd == 1)
		return rd;
	//xcb_set_clip_rectangles(view->c, XCB_CLIP_ORDERING_UNSORTED, view->fg, 5, 5, 0, NULL);
	unsigned i, o = 0;
	for (i = 1; i < (unsigned)rd; ++i) {
		pts[o].x = buf[i - 1];
		pts[o].y = buf[i];
		pts[o].x += 5;
		pts[o].y += 5;
		if (++o > BUFSIZ-1) {
			xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->w, view->fg, o, pts);
			o = 0;
		}
	}
	if (o) {
		xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->w, view->fg, o, pts);
	}
	xcb_flush(view->c);
	//usleep(100000);
	return rd;
}

#define DO_XCB_EXPOSE (1 << 0)
#define DO_XCB_QUIT   (1 << 1)
static unsigned do_xcb_events(xcb_connection_t *connection, struct window *view)
{
	xcb_generic_event_t *event;
	unsigned ret = 0;

	while ((event = xcb_poll_for_event(connection))) {
		switch (event->response_type & ~0x80) {
		case XCB_EXPOSE:
			expose_view(view);
			ret |= DO_XCB_EXPOSE;
			break;

		case XCB_KEY_PRESS:
		case XCB_KEY_RELEASE:
			{
				xcb_key_press_event_t *key = (xcb_key_press_event_t *)event;
				if ((event->response_type & ~0x80) == XCB_KEY_RELEASE &&
				    (key->detail == 0x18 /* Q */ || key->detail == 0x09 /* Esc */)) {
					xcb_disconnect(connection);
					ret |= DO_XCB_QUIT;
					break;
				}
				fprintf(stderr, "key %s: 0x%02x mod 0x%x\n",
					(event->response_type & ~0x80) == XCB_KEY_PRESS ?
					"press" : "release",
					key->detail, key->state);
			}
			break;

		case XCB_BUTTON_PRESS:
			{ xcb_button_press_event_t *press = (xcb_button_press_event_t *)event; }
			fprintf(stderr, "xcb event 0x%x\n", event->response_type);
			break;

		default: 
			fprintf(stderr, "xcb event 0x%x\n", event->response_type);
			break;
		}

		free (event);
	}
	return ret;
}

struct well_known_atom ATOM;

static xcb_connection_t *connect_x_server()
{
	static struct { const char *name; xcb_atom_t *re; } rq[] = {
		{ "_NET_WM_WINDOW_TYPE", &ATOM._NET_WM_WINDOW_TYPE },
		{ "_NET_WM_WINDOW_TYPE_DIALOG", &ATOM._NET_WM_WINDOW_TYPE_DIALOG },
	};
	xcb_intern_atom_cookie_t cookie[sizeof(rq)/sizeof(rq[0])];
	unsigned int i;
	xcb_connection_t *c = xcb_connect(NULL, NULL);

	if (!c)
		goto fail;
	for (i = 0; i < sizeof(rq)/sizeof(rq[0]); ++i)
		cookie[i] = xcb_intern_atom(c, 1, strlen(rq[i].name), rq[i].name);
	xcb_flush(c);
	for (i = 0; i < sizeof(rq)/sizeof(rq[0]); ++i) {
		xcb_intern_atom_reply_t *reply;

		reply = xcb_intern_atom_reply(c, cookie[i], NULL);
		fprintf(stderr, "atom %s -> %u\n", rq[i].name, reply ? reply->atom : ~0u);
		*rq[i].re = reply ? reply->atom : ~0u;
		free(reply);
	}
fail:
	return c;
}

int main(int argc, char *argv[])
{
	xcb_connection_t *connection = connect_x_server();
	if (!connection) {
		fprintf(stderr, "Cannot connect to DISPLAY\n");
		exit(2);
	}

	struct window *view = create_rawview_window(connection, "rawview", NULL);
	if (!view) {
		fprintf(stderr, "out of memory\n");
		exit(2);
	}

	/* map the window on the screen */
	xcb_map_window(connection, view->w);
	xcb_flush(connection);

	struct pollfd fds[2];
	nfds_t nfds = 1;

	fds[0].fd = xcb_get_file_descriptor(connection);
	fds[0].events = POLLIN;
	fds[1].fd = STDIN_FILENO;
	fds[1].events = POLLIN;

	while (nfds > 0) {
		int n = poll(fds, nfds, -1);

		if (n <= 0)
			continue;
		if (fds[0].revents & (POLLHUP|POLLNVAL))
			break;
		if (fds[0].revents & POLLIN) {
			unsigned ret = do_xcb_events(connection, view);
			if (ret & DO_XCB_EXPOSE)
				nfds = 2;
		}
		if (fds[1].revents & (POLLHUP|POLLNVAL)) {
			nfds = 1;
			continue;
		}
		if (fds[1].revents & POLLIN) {
			if (read_input(fds[1].fd, view) <= 0)
				nfds = 1;
		}
	}
	free(view);
	return 0;
}
