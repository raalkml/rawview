#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <getopt.h>
#include <xcb/xcb_ewmh.h>
#include "rawview.h"

static const char font_name[] = "fixed";

static struct window *create_rawview_window(xcb_connection_t *c, const char *title, const char *icon)
{
	struct window *view = calloc(1, sizeof(*view));

	if (!view)
		goto fail;

	view->c = c;
	view->size.x = 0;
	view->size.y = 0;
	view->size.width = 300;
	view->size.height = 300;

	/* get the first screen */
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;

	view->font = xcb_generate_id(view->c);
	xcb_open_font(view->c, view->font, strlen(font_name), font_name);

	/* Create foreground graphic context */
	uint32_t mask      = XCB_GC_FOREGROUND | XCB_GC_FONT | XCB_GC_GRAPHICS_EXPOSURES;
	uint32_t values[4] = { screen->white_pixel, view->font, 0 };

	view->fg = xcb_generate_id(view->c);
	xcb_create_gc(view->c, view->fg, screen->root, mask, values);
	xcb_close_font(view->c, view->font);

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
			  view->size.x, view->size.y,
			  view->size.width, view->size.height,
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

static void analyze(struct window *view, uint8_t buf[], size_t count)
{
	static xcb_point_t pts[BUFSIZ];

	//xcb_set_clip_rectangles(view->c, XCB_CLIP_ORDERING_UNSORTED, view->fg, 5, 5, 0, NULL);
	unsigned i, o = 0;
	for (i = 1; i < count; ++i) {
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
}

struct input
{
	int fd;
	off_t input_offset;
	size_t input_size;
	size_t amount;
	size_t bufsize;
	uint8_t buf[0];
};

static ssize_t read_input(struct input *in, struct window *view, size_t count)
{
	ssize_t rd = read(in->fd, in->buf, count < in->bufsize ? count : in->bufsize);
	//fprintf(stderr, "%ld %s\n", (long)rd, rd < 0 ? strerror(errno) : "");
	if (rd > 0)
		in->amount += rd;
	if (rd > 1)
		analyze(view, in->buf, rd);
	char s[100];
	int len = snprintf(s, sizeof(s), "%lld (%lu)", (long long)in->input_offset, (unsigned long)in->amount);
	xcb_image_text_8(view->c, len, view->w, view->fg, 5, 5 + 256 + 12, s);
	xcb_flush(view->c);
	return rd;
}

#define DO_XCB_EXPOSE	(1 << 0)
#define DO_XCB_QUIT	(1 << 1)
#define DO_XCB_RESTART	(1 << 2)
#define DO_XCB_LEFT	(1 << 3)
#define DO_XCB_RIGHT	(1 << 4)
#define DO_XCB_PLUS	(1 << 5)
#define DO_XCB_MINUS	(1 << 6)
static uint32_t do_xcb_events(xcb_connection_t *connection, struct window *view)
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
				if ((event->response_type & ~0x80) == XCB_KEY_RELEASE &&
				    (key->detail == 0x1b /* R */ || key->detail == 0x47 /* F5 */)) {
					ret |= DO_XCB_RESTART;
					break;
				}
				if ((event->response_type & ~0x80) == XCB_KEY_PRESS) {
					switch (key->detail) {
					case 0x72: /* Right */
						ret |= DO_XCB_RIGHT;
						break;
					case 0x71: /* Left */
						ret |= DO_XCB_LEFT;
						break;
					case 0x56: /* KP_Plus */
						ret |= DO_XCB_PLUS;
						break;
					case 0x52: /* KP_Minus */
						ret |= DO_XCB_MINUS;
						break;
					default:
						goto dump_key;
					}
					break;
				}
				dump_key:
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

static void start_redraw(struct input *in, struct window *view)
{
	in->amount = 0;
	lseek(in->fd, in->input_offset, SEEK_SET);
	xcb_clear_area(view->c, 1, view->w, 0, 0,
		       view->size.width, view->size.height);
}

int main(int argc, char *argv[])
{
	static char RAWVIEW[] = "rawview";
	char *title = RAWVIEW;
	int opt;

	while ((opt = getopt(argc, argv, "h")) != -1)
		switch (opt) {
		case 'h':
			break;
		}
	if (optind < argc) {
		int fd = open(argv[optind], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "rawview: %s: %s\n", argv[optind], strerror(errno));
			exit(2);
		}
		dup2(fd, STDIN_FILENO);
		close(fd);
		size_t size = strlen(RAWVIEW) + strlen(argv[optind]) + 32;
		title = malloc(size);
		snprintf(title, size, "%s: %s: (conti)", RAWVIEW, argv[optind]);
	}

	xcb_connection_t *connection = connect_x_server();
	if (!connection) {
		fprintf(stderr, "Cannot connect to DISPLAY\n");
		exit(2);
	}

	struct window *view = create_rawview_window(connection, title, NULL);
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
	fds[1].revents = 0;

	struct input *in = malloc(sizeof(*in) + 4096);
	in->fd = STDIN_FILENO;
	in->input_offset = 0;
	in->input_size = 4096;
	in->amount = 0;
	in->bufsize = 4096;

	int timeout = -1;
	while (nfds > 0) {
		int n = poll(fds, nfds, timeout);

		if (n <= 0)
			continue;
		if (fds[0].revents & (POLLHUP|POLLNVAL))
			break;
		if (fds[0].revents & POLLIN) {
			unsigned ret = do_xcb_events(connection, view);
			if (ret & DO_XCB_EXPOSE) {
				/* start reading stdin on first expose event */
				static int exposed;
				if (!exposed)
					exposed = nfds = 2;
			}
			if (ret & DO_XCB_RIGHT) {
				in->input_offset += in->input_size;
				start_redraw(in, view);
				nfds = 2;
				fds[1].revents |= POLLIN;
			} else if (ret & DO_XCB_LEFT) {
				if (in->input_offset > in->input_size)
					in->input_offset -= in->input_size;
				else
					in->input_offset = 0;
				start_redraw(in, view);
				nfds = 2;
				fds[1].revents |= POLLIN;
			} else if (ret & DO_XCB_PLUS) {
				in->input_size += 1024;
				start_redraw(in, view);
				nfds = 2;
				fds[1].revents |= POLLIN;
			} else if (ret & DO_XCB_MINUS) {
				if (in->input_size > 1024)
					in->input_size -= 1024;
				else
					in->input_size = 1024;
				start_redraw(in, view);
				nfds = 2;
				fds[1].revents |= POLLIN;
			}
			if (ret & DO_XCB_RESTART) {
				in->input_offset = 0;
				start_redraw(in, view);
				nfds = 2;
				fds[1].revents |= POLLIN;
			}
		}
		if (nfds == 1)
			continue;
		if (fds[1].revents & (POLLHUP|POLLNVAL)) {
			nfds = 1;
			continue;
		}
		if (fds[1].revents & POLLIN) {
			if (in->amount >= in->input_size) {
				nfds = 1;
				continue;
			}
			ssize_t rd = read_input(in, view, in->input_size - in->amount);
			if (rd <= 0)
				nfds = 1;
		}
	}
	free(view);
	return 0;
}
