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
#include <sys/stat.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>
#include "utils.h"
#include "rawview.h"

#define DEFAULT_INPUT_BLOCK_SIZE (1024)

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
	xcb_key_symbols_t *keysyms;
	struct window *view;
	struct input in;
	struct poll_fd pfd;

	char *title;
	unsigned autoscroll:1;
	unsigned seekable:1;

	/* Status area: color rainbow, stats, other text info */
	unsigned int status_height;

	/* Graph area */
	struct graph_desc *graph;
};

static struct rawview prg;
static int debug;

#define trace(...) trace_if(1, __VA_ARGS__)
int trace_if(int level, const char *fmt, ...)
{
	int ret = 0;
	if (debug >= level) {
		va_list args;
		va_start(args, fmt);
		ret = vfprintf(stderr, fmt, args);
		va_end(args);
	}
	return ret;
}

#define CONTENT_PAD_X (5)
#define CONTENT_PAD_Y (5)
#define STATUS_PAD_Y (3)
#define VIEW_BORDER (2)

static inline uint16_t sub1(uint16_t v, uint16_t d)
{
	return v < d ? 1 : v - d;
}

static void layout_rawview_window(struct window *view,
				  unsigned graph_width,
				  unsigned graph_height)
{
	/* Graph area layout (xy fixed) */
	view->graph_area.x = CONTENT_PAD_X;
	view->graph_area.y = CONTENT_PAD_Y;
	view->graph_area.width = graph_width;
	view->graph_area.height = graph_height;

	/* Status area layout (xy flexible) */
	view->status_area.x = CONTENT_PAD_X;
	view->status_area.y = view->graph_area.y + view->graph_area.height + STATUS_PAD_Y;
	view->status_area.width = sub1(view->size.width, 2 * CONTENT_PAD_X);
	view->status_area.height = sub1(view->size.height, view->status_area.y + CONTENT_PAD_Y);
}

static const char font_name[] = "fixed";

static struct window *create_rawview_window(struct rawview *prg, const char *icon)
{
	uint32_t mask;
	uint32_t values[5];
	xcb_connection_t *c = prg->connection;
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
		{ &view->colors.red,         0xffff, 0,      0 },
		{ &view->colors.green,       0,      0xffff, 0 },
		{ &view->colors.blue,        0,      0,      0xffff },
		{ &view->colors.border,      0x5fff, 0x5fff, 0x5fff },
		{ view->colors.graph_fg + 0, 0x7fff, 0x7fff, 0x7fff },
		{ view->colors.graph_fg + 1, 0x82ff, 0x00ff, 0x8eff },
		{ view->colors.graph_fg + 2, 0xeaff, 0x00ff, 0xffff },
		{ view->colors.graph_fg + 3, 0x10ff, 0x00ff, 0xa9ff },
		{ view->colors.graph_fg + 4, 0x18ff, 0x00ff, 0xffff },
		{ view->colors.graph_fg + 5, 0x00ff, 0xe4ff, 0xffff },
		{ view->colors.graph_fg + 6, 0x00ff, 0xffff, 0x3cff },
		{ view->colors.graph_fg + 7, 0xeaff, 0xffff, 0x00ff },
		{ view->colors.graph_fg + 8, 0xffff, 0x84ff, 0x00ff },
		{ view->colors.graph_fg + 9, 0xffff, 0x00ff, 0x00ff },
		{ &view->colors.graph_bg,    0,      0,      0 },
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
	view->size.width = prg->graph->width + 2 * CONTENT_PAD_X;
	view->size.height = prg->graph->height + 2 * CONTENT_PAD_Y + prg->status_height + STATUS_PAD_Y;

	mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK;
	values[0] = view->colors.border;
	values[1] = view->colors.border;
	values[2] = XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_KEY_PRESS    |
		XCB_EVENT_MASK_KEY_RELEASE  |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY;
	xcb_create_window(view->c,
			  XCB_COPY_FROM_PARENT,	/* depth */
			  view->w,
			  screen->root,		/* parent window */
			  view->size.x, view->size.y,
			  view->size.width, view->size.height,
			  VIEW_BORDER,
			  XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  screen->root_visual,
			  mask, values);
	xcb_change_property(view->c,
			    XCB_PROP_MODE_REPLACE,
			    view->w,
			    XCB_ATOM_WM_NAME,
			    XCB_ATOM_STRING, 8,
			    strlen(prg->title), prg->title);
	if (!icon)
		icon = prg->title;
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
	mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT | XCB_GC_GRAPHICS_EXPOSURES;
	values[0] = screen->white_pixel;
	values[1] = screen->black_pixel;
	values[2] = view->font;
	values[3] = 0;
	xcb_create_gc(view->c, view->fg, view->w, mask, values);

	xcb_query_text_extents_cookie_t rq;
	xcb_query_text_extents_reply_t *text_exts;

	rq = xcb_query_text_extents (view->c, view->font, 2,
				     (xcb_char2b_t []){ {0,'V'}, {0, 'g'} });
	text_exts = xcb_query_text_extents_reply(view->c, rq, NULL);
	view->font_height = 14;
	view->font_base = 12;
	if (text_exts) {
		view->font_base = text_exts->font_ascent;
		view->font_height = view->font_base + text_exts->font_descent;
	}
	free(text_exts);
	xcb_close_font(view->c, view->font);

	layout_rawview_window(view, prg->graph->width, prg->graph->height);

	/* graph area off-screen pixmap */
	xcb_create_pixmap(view->c, screen->root_depth, view->graph_pid, view->w,
			  view->graph_area.width, view->graph_area.height);
	/* graph GC */
	mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_GRAPHICS_EXPOSURES;
	values[0] = view->colors.graph_fg[0];
	values[1] = screen->black_pixel;
	values[2] = 0;
	xcb_create_gc(view->c, view->graph, view->graph_pid, mask, values);
	prg->graph->reset(view);
fail:
	return view;
}

static void update_status_area(struct window *view)
{
	const xcb_point_t line[2] = {
		{ view->status_area.x, view->status_area.y + view->font_base },
		{ view->status_area.x + view->status_area.width, view->status_area.y + view->font_base },
	};

	xcb_change_gc(view->c, view->fg, XCB_GC_FOREGROUND, view->colors.graph_fg);
	/* draw the baseline of the first line of status text */
	xcb_poly_line(view->c, XCB_COORD_MODE_ORIGIN, view->w, view->fg, countof(line), line);
	xcb_poly_rectangle(view->c, view->w, view->fg, 1, &view->status_area);
	xcb_image_text_8(view->c, strlen(view->status_line1), view->w, view->fg,
			 view->status_area.x + 1, line[0].y,
			 view->status_line1);
	xcb_image_text_8(view->c, strlen(view->status_line2), view->w, view->fg,
			 view->status_area.x + 1, line[0].y + view->font_height,
			 view->status_line2);
}

static void expose_view(struct window *view)
{
	unsigned i;

	xcb_copy_area(view->c, view->graph_pid, view->w, view->fg,
		      0, 0,
		      view->graph_area.x, view->graph_area.y,
		      view->graph_area.width,
		      view->graph_area.height);
	update_status_area(view);

	/* rainbow of graph foreground colors */
	int16_t step = view->graph_area.width / countof(view->colors.graph_fg);
	int16_t rem = view->graph_area.width - step * countof(view->colors.graph_fg);

	if (!step)
		step = 1;
	xcb_rectangle_t rt = {
		.x = view->graph_area.x,
		.y = view->graph_area.y + view->graph_area.height + 1,
		.width = step + rem / 2,
		.height = 2
	};
	xcb_change_gc(view->c, view->fg, XCB_GC_FOREGROUND, view->colors.graph_fg);
	xcb_poly_fill_rectangle(view->c, view->w, view->fg, 1, &rt);
	rt.width = step;
	for (i = 1; i < countof(view->colors.graph_fg) - 1; ++i) {
		rt.x += step;
		xcb_change_gc(view->c, view->fg, XCB_GC_FOREGROUND, view->colors.graph_fg + i);
		xcb_poly_fill_rectangle(view->c, view->w, view->fg, 1, &rt);
	}
	xcb_change_gc(view->c, view->fg, XCB_GC_FOREGROUND, view->colors.graph_fg + i);
	rt.x += step;
	rt.width = view->status_area.x + view->status_area.width - rt.x + 1;
	xcb_poly_fill_rectangle(view->c, view->w, view->fg, 1, &rt);
	xcb_flush(view->c);
}

static ssize_t read_input(struct input *in, struct window *view, size_t count)
{
	struct rawview *prg = container_of(in, struct rawview, in);
	ssize_t rd = read(in->pfd.fd, in->buf, count < in->bufsize ? count : in->bufsize);

	trace("%ld %s\n", (long)rd, rd < 0 ? strerror(errno) : "");
	if (rd > 0)
		in->amount += rd;
	if (rd > 1)
		prg->graph->analyze(view, in->buf, rd);
	snprintf(view->status_line1, sizeof(view->status_line1),
		 in->amount != in->input_size ?
		 "0x%llx (%lu/%lu)" : "0x%llx (%lu)",
		 (long long)in->input_offset,
		 (unsigned long)in->amount,
		 (unsigned long)in->input_size);
	snprintf(view->status_line2, sizeof(view->status_line2),
		 "%lld", (long long)in->input_offset);
	xcb_clear_area(view->c, 0, view->w,
		       view->status_area.x,
		       view->status_area.y,
		       view->status_area.width,
		       view->status_area.height);
	update_status_area(view);
	return rd;
}

enum rawview_event
{
	RAWVIEW_EV_NOP,
	RAWVIEW_EV_EXPOSE,
	RAWVIEW_EV_QUIT,
	RAWVIEW_EV_RESTART,
	RAWVIEW_EV_LEFT,
	RAWVIEW_EV_RIGHT,
	RAWVIEW_EV_PLUS,
	RAWVIEW_EV_MINUS,
	RAWVIEW_EV_AUTOSCROLL,
	RAWVIEW_EV_RESIZE,
};

static enum rawview_event do_xcb_events(struct rawview *prg)
{
	union {
		xcb_generic_event_t *generic;
		xcb_generic_error_t *error;
		xcb_button_press_event_t *btn;
		xcb_key_press_event_t *key;
		xcb_configure_notify_event_t *configure;
		xcb_unmap_notify_event_t *unmap;
		xcb_destroy_notify_event_t *destroy;
	} ev;
	enum rawview_event ret = RAWVIEW_EV_NOP;

	while ((ev.generic = xcb_poll_for_event(prg->connection))) {
		unsigned from_server = !(ev.generic->response_type & 0x80);

		if (ev.generic->response_type == 0) {
			trace_if(0, "X11 error: code %u, seq %u resource %u, opcode %u.%u\n",
				 ev.error->error_code, ev.error->sequence,
				 ev.error->resource_id,
				 ev.error->major_code, ev.error->minor_code);
			continue;
		}
		switch (ev.generic->response_type & 0x7f) {
			xcb_keysym_t key;

		case XCB_UNMAP_NOTIFY:
			if (ev.unmap->window == prg->view->w)
				ret = RAWVIEW_EV_QUIT;
			break;

		case XCB_DESTROY_NOTIFY:
			if (ev.destroy->window == prg->view->w)
				ret = RAWVIEW_EV_QUIT;
			break;

		case XCB_CONFIGURE_NOTIFY:
			trace_if(2, "event %02x configure ev %lu wnd %lu above %lu x %d y %d, w %u h %u border %u over %u\n",
				 ev.configure->response_type,
				 ev.configure->event,
				 ev.configure->window,
				 ev.configure->above_sibling,
				 ev.configure->x,
				 ev.configure->y,
				 ev.configure->width,
				 ev.configure->height,
				 ev.configure->border_width,
				 ev.configure->override_redirect);
			if (ev.configure->width != prg->view->size.width ||
			    ev.configure->height != prg->view->size.height) {
				ret = RAWVIEW_EV_RESIZE;
				prg->view->size.width = ev.configure->width;
				prg->view->size.height = ev.configure->height;
			}
			break;

		case XCB_EXPOSE:
			ret = RAWVIEW_EV_EXPOSE;
			break;

		case XCB_KEY_PRESS:
			key = xcb_key_symbols_get_keysym(prg->keysyms, ev.key->detail, 0);

			switch (key) {
			case XK_q:
			case XK_Escape:
				ret = RAWVIEW_EV_QUIT;
				break;
			case XK_a:
				ret = RAWVIEW_EV_AUTOSCROLL;
				break;
			case XK_r:
			case XK_KP_Home:
			case XK_Home:
				ret = RAWVIEW_EV_RESTART;
				break;
			case XK_KP_Page_Up:
			case XK_Page_Up:
			case XK_Left:
				ret = RAWVIEW_EV_LEFT;
				break;
			case XK_KP_Space:
			case XK_space:
			case XK_KP_Page_Down:
			case XK_Page_Down:
			case XK_Right:
				ret = RAWVIEW_EV_RIGHT;
				break;
			case XK_KP_Add:
			case XK_plus:
				ret = RAWVIEW_EV_PLUS;
				break;
			case XK_KP_Subtract:
			case XK_minus:
				ret = RAWVIEW_EV_MINUS;
				break;
			default:
				goto dump_key;
			}
			break;
		dump_key:
			trace("key %s: 0x%02x mod 0x%x keysym 0x%08x\n",
			      (ev.key->response_type & ~0x80) == XCB_KEY_PRESS ?
			      "press" : "release",
			      ev.key->detail, ev.key->state, key);
			break;

		case XCB_KEY_RELEASE:
			key = xcb_key_symbols_get_keysym(prg->keysyms, ev.key->detail, 0);
			goto dump_key;

		case XCB_BUTTON_PRESS:
		case XCB_BUTTON_RELEASE:
			trace("xcb event 0x%x 0x%x\n", ev.btn->response_type, ev.btn->detail);
			break;

		default: 
			trace("xcb %sevent 0x%x\n", from_server ? "" : "sent ", ev.generic->response_type);
			break;
		}
		free(ev.generic);
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
	prg->graph->reset(prg->view);
/*	xcb_clear_area(prg->view->c, 1, prg->view->w,
		       0, 0, prg->view->size.width, prg->view->size.height); */
}

static void pfd_xcb_proc(struct poll_context *pctx, struct poll_fd *pfd)
{
	struct rawview *prg = container_of(pfd, struct rawview, pfd);
	struct window *view = prg->view;

	if (pfd->revents & (POLLHUP|POLLNVAL)) {
		remove_poll(pctx, pfd);
		return;
	}
	if (!(pfd->revents & POLLIN))
		return;

	switch (do_xcb_events(prg)) {
		static int exposed;

	case RAWVIEW_EV_NOP:
		break;

	case RAWVIEW_EV_QUIT:
		xcb_disconnect(prg->connection);
		remove_poll(pctx, pfd);
		break;

	case RAWVIEW_EV_EXPOSE:
		/* start reading stdin on first expose event */
		if (!exposed) {
			exposed = 1;
			add_poll(pctx, &prg->in.pfd);
		}
		expose_view(prg->view);
		break;

	case RAWVIEW_EV_RESIZE:
		view->status_area.width = view->size.width - 2 * CONTENT_PAD_Y;
		if (prg->graph->resize) {
			xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(view->c)).data;

			xcb_free_pixmap(view->c, view->graph_pid);
			layout_rawview_window(view,
				sub1(view->size.width, 2 * CONTENT_PAD_X),
				sub1(view->size.height, STATUS_PAD_Y +
				     view->status_area.height + 2 * CONTENT_PAD_Y));
			xcb_create_pixmap(view->c, screen->root_depth, view->graph_pid, view->w,
					  view->graph_area.width, view->graph_area.height);
			prg->graph->resize(view);
			start_redraw(prg);
			add_poll(pctx, &prg->in.pfd);
		}
		expose_view(view);
		break;

	case RAWVIEW_EV_RIGHT:
		if (!prg->autoscroll)
			prg->in.input_offset += prg->in.input_size;
		start_redraw(prg);
		add_poll(pctx, &prg->in.pfd);
		prg->autoscroll = 0;
		break;

	case RAWVIEW_EV_LEFT:
		if (prg->seekable) {
			off_t prev = prg->in.input_offset;
			if (prg->in.input_offset > prg->in.input_size)
				prg->in.input_offset -= prg->in.input_size;
			else
				prg->in.input_offset = 0;
			if (prev != prg->in.input_offset) {
				start_redraw(prg);
				add_poll(pctx, &prg->in.pfd);
			}
			prg->autoscroll = 0;
		}
		break;

	case RAWVIEW_EV_PLUS:
		prg->in.input_size += 1024;
		start_redraw(prg);
		add_poll(pctx, &prg->in.pfd);
		break;

	case RAWVIEW_EV_MINUS:
		{
			size_t prev = prg->in.input_size;
			if (prg->in.input_size > 1024)
				prg->in.input_size -= 1024;
			else
				prg->in.input_size = 1024;
			if (prev != prg->in.input_size) {
				start_redraw(prg);
				add_poll(pctx, &prg->in.pfd);
			}
		}
		break;

	case RAWVIEW_EV_RESTART:
		if (prg->seekable) {
			prg->autoscroll = 0;
			prg->in.input_offset = 0;
			start_redraw(prg);
			add_poll(pctx, &prg->in.pfd);
		}
		break;

	case RAWVIEW_EV_AUTOSCROLL:
		prg->autoscroll = !prg->autoscroll;
		break;
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
		if (in->amount >= in->input_size) {
			expose_view(prg->view);
			remove_poll(pctx, pfd);
		}
	} else {
		expose_view(prg->view);
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
		.input_size = DEFAULT_INPUT_BLOCK_SIZE,
		.bufsize = sizeof(prg.in.buf),
	},
	.title = RAWVIEW,
	.autoscroll = 0,
	.seekable = 1,

	.status_height = 32,
	.graph = &conti_graph,
};

int main(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "hDO:B:Av:")) != -1)
		switch (opt) {
		case 'A':
			prg.autoscroll = 1;
			break;
		case 'D':
			++debug;
			break;
		case 'B':
			prg.in.input_size = strtoll(optarg, NULL, 0);
			break;
		case 'O':
			prg.in.input_offset = strtoll(optarg, NULL, 0);
			break;
		case 'h':
			break;
		case 'v':
			if (strcasecmp(optarg, conti_graph.name) == 0)
				prg.graph = &conti_graph;
			else if (strcasecmp(optarg, bytemap_graph.name) == 0)
				prg.graph = &bytemap_graph;
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
	if (prg.in.input_size == 0) { /* -B0 */
		struct stat st;
		if (fstat(STDIN_FILENO, &st) == 0)
			prg.in.input_size = st.st_size;
		else
			fprintf(stderr, "rawview: input: %s\n", strerror(errno));
		if (!prg.in.input_size)
			prg.in.input_size = DEFAULT_INPUT_BLOCK_SIZE;
	}

	prg.connection = connect_x_server();
	if (!prg.connection) {
		fprintf(stderr, "Cannot connect to DISPLAY\n");
		exit(2);
	}
	prg.keysyms = xcb_key_symbols_alloc(prg.connection);
	prg.view = create_rawview_window(&prg, RAWVIEW);
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

	int timeout = prg.autoscroll ? 50 : -1;

	while (pollctx.npolls) {
		int n = poll_fds(&pollctx, timeout);
		if (prg.autoscroll)
			timeout = 50;
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
