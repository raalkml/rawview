#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <getopt.h>
#include <xcb/xcb_ewmh.h>
#include "utils.h"
#include "rawview.h"

struct input
{
	struct poll_fd pfd;
	off_t input_offset;
	size_t input_size;
	size_t amount;
	size_t bufsize;
	uint8_t buf[BUFSIZ];
};

struct rawview
{
	xcb_connection_t *connection;
	struct window *view;
	struct input in;
	struct poll_fd pfd;

	char *title;
	unsigned autoscroll:1;
	unsigned seekable:1;
};

static struct rawview prg;
static int debug;
static void reset_graph(struct window *view);

static inline int trace(const char *fmt, ...)
{
	int ret = 0;
	if (debug) {
		va_list args;
		va_start(args, fmt);
		ret = vfprintf(stderr, fmt, args);
		va_end(args);
	}
	return ret;
}

static const char font_name[] = "fixed";

static struct window *create_rawview_window(xcb_connection_t *c, const char *title, const char *icon)
{
	uint32_t mask;
	uint32_t values[5];
	struct window *view = calloc(1, sizeof(*view));
	unsigned i;

	if (!view)
		goto fail;

	/* get the first screen */
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;

	struct {
		uint32_t *ret;
		uint16_t r, g, b;
		xcb_alloc_color_cookie_t rq;
	} colors[] = {
		{ &view->colors.red,    0xffff, 0, 0 },
		{ &view->colors.green,  0, 0xffff, 0 },
		{ &view->colors.blue,   0, 0, 0xffff },
		{ &view->colors.border, 0x7fff, 0x7fff, 0x7fff },
		{ &view->colors.graph_fg, 0x7fff, 0x7fff, 0x7fff },
		{ &view->colors.graph_bg, 0, 0, 0 },
	};
	for (i = 0; i < countof(colors); ++i)
		colors[i].rq = xcb_alloc_color(c, screen->default_colormap,
					       colors[i].r,
					       colors[i].g,
					       colors[i].b);
	for (i = 0; i < countof(colors); ++i) {
		xcb_alloc_color_reply_t *re;
		re = xcb_alloc_color_reply(c, colors[i].rq, NULL);
		*colors[i].ret = re ? re->pixel : screen->black_pixel;
		trace("color %u: 0x%08x\n", i, *colors[i].ret);
		free(re);
	}

	view->c = c;
	view->font = xcb_generate_id(view->c);
	view->fg = xcb_generate_id(view->c);
	view->graph_pid = xcb_generate_id(view->c);
	view->graph = xcb_generate_id(view->c);
	view->w = xcb_generate_id(view->c);

	view->size.x = 0;
	view->size.y = 0;
	view->size.width = 300;
	view->size.height = 300;

	mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK;
	values[0] = screen->black_pixel;
	values[1] = view->colors.border;
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

	/* Create foreground graphic context */
	xcb_open_font(view->c, view->font, strlen(font_name), font_name);
	mask = XCB_GC_FOREGROUND | XCB_GC_FONT | XCB_GC_GRAPHICS_EXPOSURES;
	values[0] = screen->white_pixel;
	values[1] = view->font;
	values[2] = 0;
	xcb_create_gc(view->c, view->fg, view->w, mask, values);
	xcb_close_font(view->c, view->font);

	view->graph_area.x = 5;
	view->graph_area.y = 5;
	view->graph_area.width = 256;
	view->graph_area.height = 256;
	xcb_create_pixmap(view->c, screen->root_depth, view->graph_pid, view->w,
			  view->graph_area.width, view->graph_area.height);
	/* graph GC */
	mask = XCB_GC_FOREGROUND |XCB_GC_BACKGROUND | XCB_GC_GRAPHICS_EXPOSURES;
	values[0] = screen->white_pixel;
	values[1] = screen->black_pixel;
	values[2] = 0;
	xcb_create_gc(view->c, view->graph, view->graph_pid, mask, values);
	reset_graph(view);

	view->status_area.x = 5;
	view->status_area.y = view->graph_area.y + view->graph_area.height;
	view->status_area.width = view->size.width;
	view->status_area.height = view->size.height - view->status_area.y;
fail:
	return view;
}

static void expose_view(struct window *view)
{
	trace("%s\n", __PRETTY_FUNCTION__);
	xcb_copy_area(view->c, view->graph_pid, view->w, view->fg,
		      0, 0,
		      view->graph_area.x, view->graph_area.y,
		      view->graph_area.width,
		      view->graph_area.height);
	xcb_image_text_8(view->c, strlen(view->status_line),
			 view->w, view->fg,
			 view->status_area.x,
			 view->status_area.y + 12 /* FIXME: use baseline offset */,
			 view->status_line);
	xcb_flush(view->c);
}

static void reset_graph(struct window *view)
{
	uint32_t mask = XCB_GC_FOREGROUND;
	uint32_t values[] = { view->colors.graph_bg };
	xcb_rectangle_t graph = { 0, 0, view->graph_area.width, view->graph_area.height };

	xcb_change_gc(view->c, view->graph, mask, values);
	xcb_poly_fill_rectangle(view->c, view->graph_pid, view->graph, 1, &graph);
}

static void analyze(struct window *view, uint8_t buf[], size_t count)
{
	xcb_point_t pts[BUFSIZ / sizeof(xcb_point_t)];
	unsigned i, o = 0;
	uint32_t mask = XCB_GC_FOREGROUND;
	uint32_t values[] = { view->colors.graph_fg };

	xcb_change_gc(view->c, view->graph, mask, values);
	for (i = 1; i < count; ++i) {
		pts[o].x = buf[i - 1];
		pts[o].y = buf[i];
		if (++o == countof(pts)) {
			xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->graph_pid, view->graph, o, pts);
			o = 0;
		}
	}
	if (o) {
		xcb_poly_point(view->c, XCB_COORD_MODE_ORIGIN, view->graph_pid, view->graph, o, pts);
	}
}

static ssize_t read_input(struct input *in, struct window *view, size_t count)
{
	ssize_t rd = read(in->pfd.fd, in->buf, count < in->bufsize ? count : in->bufsize);
	trace("%ld %s\n", (long)rd, rd < 0 ? strerror(errno) : "");
	if (rd > 0)
		in->amount += rd;
	if (rd > 1)
		analyze(view, in->buf, rd);
	int len = snprintf(view->status_line, sizeof(view->status_line),
			   in->amount != in->input_size ?
			   "%lld (%lu/%lu)" : "%lld (%lu)",
			   (long long)in->input_offset,
			   (unsigned long)in->amount,
			   (unsigned long)in->input_size);
	xcb_clear_area(view->c, 0, view->w,
		       view->status_area.x,
		       view->status_area.y,
		       view->status_area.width,
		       view->status_area.height);
	xcb_image_text_8(view->c, len, view->w, view->fg,
			 view->status_area.x, view->status_area.y + 12,
			 view->status_line);
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
#define DO_XCB_SPACE	(1 << 7)
static uint32_t do_xcb_events(xcb_connection_t *connection, struct window *view)
{
	xcb_generic_event_t *event;
	unsigned ret = 0;

	while ((event = xcb_poll_for_event(connection))) {
		switch (event->response_type & ~0x80) {
		case XCB_EXPOSE:
			ret |= DO_XCB_EXPOSE;
			break;

		case XCB_KEY_PRESS:
		case XCB_KEY_RELEASE:
			{
				xcb_key_press_event_t *key = (xcb_key_press_event_t *)event;
				if ((event->response_type & ~0x80) == XCB_KEY_RELEASE &&
				    (key->detail == 0x1b /* R */ || key->detail == 0x47 /* F5 */)) {
					ret |= DO_XCB_RESTART;
					break;
				}
				if ((event->response_type & ~0x80) == XCB_KEY_PRESS) {
					switch (key->detail) {
					case 0x18 /* Q */:
					case 0x09 /* Esc */:
						ret |= DO_XCB_QUIT;
						break;
					case 0x41 /* Space */:
						ret |= DO_XCB_SPACE;
						break;
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
				trace("key %s: 0x%02x mod 0x%x\n",
				      (event->response_type & ~0x80) == XCB_KEY_PRESS ?
				      "press" : "release",
				      key->detail, key->state);
			}
			break;

		case XCB_BUTTON_PRESS:
			{ xcb_button_press_event_t *press = (xcb_button_press_event_t *)event; }
			trace("xcb event 0x%x\n", event->response_type);
			break;

		default: 
			trace("xcb event 0x%x\n", event->response_type);
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
	xcb_intern_atom_cookie_t cookie[countof(rq)];
	unsigned int i;
	xcb_connection_t *c = xcb_connect(NULL, NULL);

	if (!c)
		goto fail;
	for (i = 0; i < countof(rq); ++i)
		cookie[i] = xcb_intern_atom(c, 1, strlen(rq[i].name), rq[i].name);
	xcb_flush(c);
	for (i = 0; i < countof(rq); ++i) {
		xcb_intern_atom_reply_t *reply;

		reply = xcb_intern_atom_reply(c, cookie[i], NULL);
		trace("atom %s -> %u\n", rq[i].name, reply ? reply->atom : ~0u);
		*rq[i].re = reply ? reply->atom : ~0u;
		free(reply);
	}
fail:
	return c;
}

static void start_redraw(struct rawview *prg)
{
	prg->in.amount = 0;
	if (prg->seekable &&
	    lseek(prg->in.pfd.fd, prg->in.input_offset, SEEK_SET) == -1 && ESPIPE == errno)
		prg->seekable = 0;
	reset_graph(prg->view);
/*	xcb_clear_area(prg->view->c, 1, prg->view->w,
		       0, 0, prg->view->size.width, prg->view->size.height); */
}

static void pfd_xcb_proc(struct poll_context *pctx, struct poll_fd *pfd)
{
	struct rawview *prg = container_of(pfd, struct rawview, pfd);
	unsigned ret;

	if (pfd->revents & (POLLHUP|POLLNVAL)) {
		remove_poll(pctx, pfd);
		return;
	}
	if (!(pfd->revents & POLLIN))
		return;

	ret = do_xcb_events(prg->connection, prg->view);
	if (ret & DO_XCB_QUIT) {
		xcb_disconnect(prg->connection);
		remove_poll(pctx, pfd);
	}
	if (ret & DO_XCB_EXPOSE) {
		/* start reading stdin on first expose event */
		static int exposed;
		if (!exposed) {
			exposed = 1;
			add_poll(pctx, &prg->in.pfd);
		}
		expose_view(prg->view);
	}
	if (ret & (DO_XCB_RIGHT|DO_XCB_SPACE)) {
		prg->in.input_offset += prg->in.input_size;
		start_redraw(prg);
		add_poll(pctx, &prg->in.pfd);
	} else if (prg->seekable && (ret & DO_XCB_LEFT)) {
		if (prg->in.input_offset > prg->in.input_size)
			prg->in.input_offset -= prg->in.input_size;
		else
			prg->in.input_offset = 0;
		start_redraw(prg);
		add_poll(pctx, &prg->in.pfd);
	} else if (ret & DO_XCB_PLUS) {
		prg->in.input_size += 1024;
		prg->in.amount = 0;
		if (prg->seekable) {
			if (lseek(prg->in.pfd.fd, prg->in.input_offset, SEEK_SET) == -1 &&
			    ESPIPE == errno)
				prg->seekable = 0;
		}
		start_redraw(prg);
		add_poll(pctx, &prg->in.pfd);
	} else if (ret & DO_XCB_MINUS) {
		if (prg->in.input_size > 1024)
			prg->in.input_size -= 1024;
		else
			prg->in.input_size = 1024;
		start_redraw(prg);
		add_poll(pctx, &prg->in.pfd);
	}
	if (prg->seekable && (ret & DO_XCB_RESTART)) {
		prg->in.input_offset = 0;
		start_redraw(prg);
		add_poll(pctx, &prg->in.pfd);
	}
}

static void pfd_input_proc(struct poll_context *pctx, struct poll_fd *pfd)
{
	struct input *in = container_of(pfd, struct input, pfd);
	struct rawview *prg = container_of(in, struct rawview, in);

	if (pfd->revents & (POLLHUP|POLLNVAL)) {
		remove_poll(pctx, pfd);
		return;
	}
	if (!(pfd->revents & POLLIN))
		return;
	if (in->amount >= in->input_size) {
		remove_poll(pctx, pfd);
		return;
	}
	ssize_t rd = read_input(in, prg->view, in->input_size - in->amount);
	if (rd > 0) {
		expose_view(prg->view);
	} else {
		remove_poll(pctx, pfd);
		prg->autoscroll = 0;
	}
}

static char RAWVIEW[] = "rawview";
static struct rawview prg = {
	.pfd = {
		.events = POLLIN,
		.proc = pfd_xcb_proc,
	},
	.in = {
		.pfd = {
			.fd = STDIN_FILENO,
			.events = POLLIN,
			.proc = pfd_input_proc,
		},
		.input_offset = 0,
		.input_size = 1024,
		.bufsize = sizeof(prg.in.buf),
	},
	.title = RAWVIEW,
	.autoscroll = 0,
	.seekable = 1,
};

int main(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "hDO:A")) != -1)
		switch (opt) {
		case 'A':
			prg.autoscroll = 1;
			break;
		case 'D':
			++debug;
			break;
		case 'O':
			prg.in.input_offset = strtoll(optarg, NULL, 0);
			break;
		case 'h':
			break;
		}
	if (optind < argc) {
		int fd = open(argv[optind], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "rawview: %s: %s\n", argv[optind], strerror(errno));
			exit(2);
		}
		if (dup2(fd, STDIN_FILENO) < 0) {
			fprintf(stderr, "rawview: %s(%d->%d): %s\n", argv[optind],
				fd, STDIN_FILENO, strerror(errno));
			exit(2);
		}
		close(fd);
		size_t size = strlen(RAWVIEW) + strlen(argv[optind]) + 32;
		prg.title = malloc(size);
		snprintf(prg.title, size, "%s: %s: (conti)", RAWVIEW, argv[optind]);
	}

	prg.connection = connect_x_server();
	if (!prg.connection) {
		fprintf(stderr, "Cannot connect to DISPLAY\n");
		exit(2);
	}
	prg.view = create_rawview_window(prg.connection, prg.title, NULL);
	if (!prg.view) {
		fprintf(stderr, "out of memory\n");
		exit(2);
	}

	/* map the window on the screen */
	xcb_map_window(prg.connection, prg.view->w);
	xcb_flush(prg.connection);

	static struct poll_context pollctx = { 0, };
	prg.pfd.fd = xcb_get_file_descriptor(prg.connection);
	add_poll(&pollctx, &prg.pfd);

	if (prg.in.input_offset &&
	    prg.seekable &&
	    lseek(prg.in.pfd.fd, prg.in.input_offset, SEEK_SET) == -1) {
		prg.in.input_offset = 0;
		prg.seekable = 0;
		fprintf(stderr, "lseek: %s\n", strerror(errno));
	}

	int timeout = -1;

	if (prg.autoscroll)
		timeout = 50;

	while (pollctx.npolls) {
		int n = poll_fds(&pollctx, timeout);
		if (n <= 0) {
			if (prg.autoscroll &&
			    n == 0 &&
			    prg.in.amount >= prg.in.input_size) {
				prg.in.input_offset += prg.in.input_size;
				start_redraw(&prg);
				add_poll(&pollctx, &prg.in.pfd);
			}
		}
	}
	return 0;
}
