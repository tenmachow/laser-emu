#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/tty.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "laser_cmd.h"

typedef struct _TimerRec *TimerPtr;
typedef unsigned int (*TimerCallback)(TimerPtr /* timer */, 
									unsigned int /*time */, 
									void * /* arg */);

struct _TimerRec {
	TimerPtr next;
	unsigned int expires;
	unsigned int delta;
	TimerCallback callback;
	void *arg;
};

#define MILLI_PER_SECOND (1000)

#define TimerAbsolute (1<<0)
#define TimerForceOld (1<<1)

TimerPtr TimerSet(TimerPtr timer, int flags, unsigned int millis,
         TimerCallback func, void *arg);
int TimerForce(TimerPtr timer);
void TimerCancel(TimerPtr timer);
void TimerFree(TimerPtr timer);
void TimerCheck(void);
void TimerInit(void);

static TimerPtr timers;

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

/*--- timer ---*/

/* If time has rewound, re-run every affected timer.
 * Timers might drop out of the list, so we have to restart every time. */
static unsigned int GetTimeInMillis(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}
static void
CheckAllTimers(void)
{
    TimerPtr timer;
    unsigned int now;

 start:
    now = GetTimeInMillis();

    for (timer = timers; timer; timer = timer->next) {
        if (timer->expires - now > timer->delta + 250) {
            TimerForce(timer);
            goto start;
        }
    }
}


static void
DoTimer(TimerPtr timer, unsigned int now, TimerPtr *prev)
{
    unsigned int newTime;

    *prev = timer->next;
    timer->next = NULL;
    newTime = (*timer->callback) (timer, now, timer->arg);
    if (newTime)
        TimerSet(timer, 0, newTime, timer->callback, timer->arg);
}


TimerPtr TimerSet(TimerPtr timer, int flags, unsigned int millis, 
		TimerCallback func, void *arg)
{
	TimerPtr *prev;
	unsigned int now = GetTimeInMillis();

	if (!timer) {
		timer = malloc(sizeof(struct _TimerRec));
		if (!timer)
			return NULL;
	 } else {
	 	for (prev = &timers; *prev; prev = &(*prev)->next) {
			if (*prev == timer) {
				*prev = timer->next;
				if (flags & TimerForceOld)
					(void)(*timer->callback)(timer, now, timer->arg);
				break;
			}
		}
	 }
	if (!millis)
		return timer;
	if (flags & TimerAbsolute) {
		timer->delta = millis - now;
	} else {
		timer->delta = millis;
		millis += now;
	}
	timer->expires = millis;
	timer->callback = func;
	timer->arg = arg;
    if ((int) (millis - now) <= 0) {
        timer->next = NULL;
        millis = (*timer->callback) (timer, now, timer->arg);
        if (!millis)
            return timer;
    }
    for (prev = &timers;
         *prev && (int) ((*prev)->expires - millis) <= 0;
         prev = &(*prev)->next);
    timer->next = *prev;
    *prev = timer;
    return timer;
}

int TimerForce(TimerPtr timer)
{
	int rc = -1;
	TimerPtr *prev;

	for (prev = &timers; *prev; prev = &(*prev)->next) {
		if (*prev == timer) {
			DoTimer(timer, GetTimeInMillis(), prev);
			rc = 0;
			break;
		}
	}
	return rc;
}

void TimerCancel(TimerPtr timer)
{
	TimerPtr *prev;

	if (!timer)
		return;
	for (prev = &timers; *prev; prev = &(*prev)->next) {
		if (*prev == timer) {
			*prev = timer->next;
			break;
		}
	}
}

void TimerFree(TimerPtr timer)
{
	if (!timer)
		return;
	TimerCancel(timer);
	free(timer);
}

void TimerCheck(void)
{
	unsigned int now = GetTimeInMillis();

	while (timers && (int)(timers->expires - now) <= 0)
		DoTimer(timers, now, &timers);
}

void TimerInit(void)
{
	TimerPtr timer;

	while ((timer = timers)) {
		timers = timers->next;
		free(timer);
	}
}

void dispatch(void)
{
	int ret, i;
	struct timeval *wt, waittime;
	int timeout;
	unsigned int now;
	fd_set dump_rfds;
	while (1) {
		/* this can do some work queue job */

		/* check timer list */
		wt = NULL;
		if (timers) {
			now = GetTimeInMillis();
			timeout = timers->expires - now;
			if (timeout > 0 && timeout > timers->delta + 250) {
				/* time has rewound. reset the timers. */
				CheckAllTimers();
			}

			if (timers) {
				timeout = timers->expires - now;
				if (timeout < 0) 
					timeout = 0;
				waittime.tv_sec = timeout / MILLI_PER_SECOND;
				waittime.tv_usec = (timeout % MILLI_PER_SECOND) * (1000000 / MILLI_PER_SECOND);
				wt = &waittime;
			}
		}
		/* prepare read fd set */
		FD_ZERO(&dump_rfds);
		//memcpy(&dump_rfds, &rfds, sizeof(rfds));
		for (i = 0; i < MAX_EVENT_SET; i++) {
			if (!gInputEventSet[i])
				continue;
			if (maxfds < gInputEventSet[i]->fd)
				maxfds = gInputEventSet[i]->fd;
			FD_SET(gInputEventSet[i]->fd, &dump_rfds);
		}
		ret = select(maxfds+1, &dump_rfds, NULL, NULL, wt);
		if (ret <= 0) { 	/* An error or timeout occurred */
			if (ret < 0) {
				return;
			}
			if (timers) {
				int expired = 0;
				now = GetTimeInMillis();
				if ((int)(timers->expires - now) <= 0)
					expired = 1;

				while (timers && (int)(timers->expires - now) <= 0)
					DoTimer(timers, now, &timers);
			}
		
		} else {
			handleEvent(&dump_rfds, ret);

			if (timers) {
				now = GetTimeInMillis();
				while (timers && (int)(timers->expires - now) <= 0)
					DoTimer(timers, now, &timers);
			}
		}
	}
}

typedef struct {
	char *buf;
	size_t length;
	size_t capacity;
} BufferRec, *BufferPtr;

static TimerPtr simpleTimer;

unsigned int GenerateSimpleData(TimerPtr timer, unsigned int millis, void *arg)
{
	int fd = (int)arg;

	write(fd, "1022.8\r\n", 8);
	return timer->delta;
}

int parse_cmd(char *buf, size_t len, int fd)
{
	char *p, *q, *end;
	int ret = 0;

	p = buf;
	end = buf + len;
	while (p != end) {
		/* skip blank line */
		while (*p == '\r' || *p == '\n') p++;
		q = p;
		while ((*q != '\r') && (q != end)) q++;

		if (q != end) {
			/* get a line */
			*q = 0;
			if (strcmp(p, "LO") == 0) {
				write(fd, "LO\r\n", 4);
				TimerCancel(simpleTimer);
			} else if (strcmp(p, "LF") == 0) {
				write(fd, "LF\r\n", 4);
				TimerCancel(simpleTimer);
			} else if (strcmp(p, "DX") == 0) {
				simpleTimer = TimerSet(simpleTimer, 0, 50, GenerateSimpleData, (void *)fd);
			} else {
				write(fd, "E404\r\n", 6);
				TimerCancel(simpleTimer);
			}
			*q = '\r';
		} else {
			ret = q - q;
			break;
		}
		p = q;
	}
	return ret;
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
		ret = parse_cmd(pb->buf, pb->length, fd);
		if (ret > 0) {
			memcpy(pb->buf, &(pb->buf[pb->length - ret]), ret);
		}
		pb->length = ret;
	}
}

int main()
{
	int fd, ret;
	char *pts;
	char buf[BUFSIZ];
	size_t length = 0, bufsiz = BUFSIZ;
	BufferRec ptmxBuf;

	fd = open("/dev/ptmx",O_RDWR);

	if (grantpt(fd)) {
		fprintf(stderr, "grantpt failed: %s\n", strerror(errno));
		close(fd);
		exit(1);
	}
	if (unlockpt(fd)) {
		fprintf(stderr, "unlockpt failed: %s\n", strerror(errno));
		close(fd);
		exit(1);
	}
	ret = ptsname_r(fd, buf, sizeof(buf));
	if (ret) {
		fprintf(stderr, "ptsname failed: %s\n", strerror(errno));
		close(fd);
		exit(1);
	}
	printf("%s\n", buf);

	ptmxBuf.buf = buf;
	ptmxBuf.length = 0;
	ptmxBuf.capacity = BUFSIZ;
	registerFD(fd, ptmx_read, &ptmxBuf);

	dispatch();

	close(fd);

	return 0;
}
