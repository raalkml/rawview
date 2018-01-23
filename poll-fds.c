#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "poll-fds.h"

void add_poll(struct poll_context *ctx, struct poll_fd *pfd)
{
	unsigned i;

	for (i = 0; i < ctx->npolls; ++i)
		if (ctx->polls[i] == pfd)
			return;
	if (ctx->npolls >= MAX_POLL_ELEMENTS) {
		abort();
		return;
	}
	ctx->polls[ctx->npolls++] = pfd;
}

void remove_poll(struct poll_context *ctx, struct poll_fd *pfd)
{
	unsigned i, o, nfds = ctx->npolls;

	for (i = 0, o = 0; i < nfds; ++i) {
		if (ctx->polls[i] == pfd) {
			ctx->npolls--;
			continue;
		}
		ctx->polls[o++] = ctx->polls[i];
	}
}

int poll_fds(struct poll_context *ctx, int ms)
{
	int ret;
	struct poll_fd **ppfd;
	struct poll_fd *polls[MAX_POLL_ELEMENTS];
	struct pollfd fds[MAX_POLL_ELEMENTS];
	nfds_t nfds = ctx->npolls;
	unsigned i;

	ppfd = ctx->polls;
	for (i = 0; i < nfds; ++i, ++ppfd) {
		polls[i] = *ppfd;
		fds[i].fd = (*ppfd)->fd;
		fds[i].events = (*ppfd)->events;
		(*ppfd)->revents = 0;
	}
	ret = poll(fds, nfds, ms);
	if (ret) {
		ppfd = polls;
		for (i = 0; i < nfds; ++i, ++ppfd) {
			(*ppfd)->revents = fds[i].revents;
			if ((*ppfd)->revents && (*ppfd)->proc)
				(*ppfd)->proc(ctx, *ppfd);
		}
	}
	return ret;
}

