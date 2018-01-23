#ifndef _POLL_FDS_H_
#define _POLL_FDS_H_ 1

#include <poll.h>

struct poll_context;

struct poll_fd
{
	int fd;
	short events, revents;
	void (*proc)(struct poll_context *, struct poll_fd *);
};

#define MAX_POLL_ELEMENTS (10)
struct poll_context
{
	unsigned npolls;
	struct poll_fd *polls[MAX_POLL_ELEMENTS];
};

void add_poll(struct poll_context *, struct poll_fd *);
void remove_poll(struct poll_context *, struct poll_fd *);
int poll_fds(struct poll_context *, int ms);

#endif /* _POLL_FDS_H_ */
