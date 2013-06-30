#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/tty.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>

#include "laser_cmd.h"

typedef void (*ReadFunc_t)(int, void *closure, void *arg);

typedef struct InputEventHandle {
	int fd;
	ReadFunc_t func;
	void *arg_data;
} InputEventHandleRec, *InputEventHandlePtr;

static int maxfds;
static fd_set rfds;

#define MAX_EVENT_SET 8
static InputEventHandlePtr gInputEventSet[MAX_EVENT_SET];
static int gInputEventCount = 0;

int fireFD(int fd)
{
	int i;
	for (i = 0; i < MAX_EVENT_SET; ++i) {
		if (gInputEventSet[i]->fd == fd) {
			InputEventHandlePtr h = gInputEventSet[i];
			gInputEventSet[i] = NULL;
			free(h);
			break;
		}
	}
	if (i == MAX_EVENT_SET)
		return -1;
	--gInputEventCount;
	return 0;
}

int registerFD(int fd, ReadFunc_t func, void *arg)
{
	int flags, i;
	InputEventHandlePtr handle;

	if ((flags = fcntl(fd, F_GETFL, 0)) < 0) {
		fprintf(stderr, "fcntl F_GETFL failed: %s\n", strerror(errno));
		return -1;
	}
	if (fcntl(fd, F_SETFL, (flags | O_NONBLOCK)) < 0) {
		fprintf(stderr, "fcntl use F_SETFL to set O_NONBLOCK failed: %s\n", strerror(errno));
		return -1;
	}
	/* find a empty slot */
	for (i = 0; i < MAX_EVENT_SET; ++i) {
		if (gInputEventSet[i] == NULL)
			break;
	}
	if (i == MAX_EVENT_SET)
		return -1;

	handle = malloc(sizeof(InputEventHandleRec));
	if (handle == NULL) {
		fprintf(stderr, "malloc InputEventHandleRec failed: %s\n", strerror(errno));
		return -1;
	}
	memset(handle, 0, sizeof(*handle));
	if (fd > maxfds)
		maxfds = fd;
	FD_SET(fd, &rfds);
	handle->fd = fd;
	handle->func = func;
	handle->arg_data = arg;

	gInputEventSet[i] = handle;
	++gInputEventCount;

	return 0;
}

int handleEvent(fd_set *set, int n)
{
	int i;
	for (i = 0; i < MAX_EVENT_SET && n > 0; ++i) {
		InputEventHandlePtr handle = gInputEventSet[i];
		if (handle && FD_ISSET(handle->fd, set)) {
			n--;
			if (handle->func)
				handle->func(handle->fd, NULL, handle->arg_data);
		}
	}
	return 0;
}

void dispatch(void)
{
	int ret;
	fd_set dump_rfds;
	while (1) {
		/* this can do some work queue job */

		/* prepare read fd set */
		memcpy(&dump_rfds, &rfds, sizeof(rfds));
		ret = select(maxfds, &dump_rfds, NULL, NULL, NULL);
		if (ret > 0)
			handleEvent(&dump_rfds, ret);
	}
}

typedef struct {
	char *buf;
	size_t length;
	size_t capacity;
} BufferRec, *BufferPtr;

int parse_cmd(char *buf, size_t len)
{
	char *p, *q, *end;

	p = buf;
	end = buf + len;
	while (p != end) {
		/* skip blank line */
		while (*p == '\r' || *p == '\n') p++;
		q = p;
		while (*q ! = '\r' && q != end) q++;

		if (q != end) {
			/* get a line */
		}
		p = q;
	}
	return 0;
}

void ptmx_read(int fd, void *closure, void *arg)
{
	int ret;
	BufferPtr pb = (BufferPtr)arg; 

	while (1) {
		ret = read(fd, &(pb->buf[pb->length]), pb->capacity - pb->length);
		if (ret < 0) {
			if (errno == EAGAIN)
				break;
			else
				return;
		}
		pb->length += ret;
		ret = parse_cmd(pb->buf, pb->length);
		if (ret > 0) {
			memcpy(pb->buf, &(pb->buf[pb->length - ret]), ret);
			pb->length = ret;
		}
	}
}

int main()
{
	int fd, ret;
	char buf[BUFSIZ];
	size_t length = 0, bufsiz = BUFSIZ;
	BufferRec ptmxBuf;

	fd = open("/dev/ptmx",O_RDWR);

	grantpt(fd);
	unlockpt(fd);
	printf("ptsname: %s\n", ptsname(fd));

	ptmxBuf.buf = buf;
	ptmxBuf.length = 0;
	ptmxBuf.capacity = BUFSIZ;
	registerFD(fd, ptmx_read, &ptmxBuf);

	dispatch();

	return 0;
}
