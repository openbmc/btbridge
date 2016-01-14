#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>

#include <linux/bt-host.h>

struct bttest_data {
	int status;
	const char msg[64];
};

static int bt_host_fd;
static int timer_fd;

static int stop;
static int sent_id = -1;
static int recv_id;

/*
 * btbridged doesn't care about the message EXCEPT the first byte must be
 * correct.
 * The first byte is the size not including the length byte its self.
 * A len less than 4 will constitute an invalid message according to the BT
 * protocol, btbridged will care.
 */
static struct bttest_data data[] = {
	/*
	 * Note, the 4th byte is cmd, the ipmi-bouncer will put cmd in cc so
	 * in this array always duplicate the command
	 *
	 * Make the first message look like:
	 * seq = 1, netfn = 2, lun = 3 and cmd= 4
	 * (thats how btbridged will print it)
	 */
	{ 0, { 4, 0xb, 1, 4, 4 }},
	{ 0, { 4, 0xff, 0xee, 0xdd, 0xdd, 0xbb }},
	/*
	 * A bug was found in bt_q_drop(), write a test!
	 * Simply send the same seq number a bunch of times
	 */
	{ 0, { 4, 0xaa, 0xde, 0xaa, 0xaa }},
	{ 0, { 4, 0xab, 0xde, 0xab, 0xab }},
	{ 0, { 4, 0xac, 0xde, 0xac, 0xac }},
	{ 0, { 4, 0xad, 0xde, 0xad, 0xad }},
	{ 0, { 4, 0xae, 0xde, 0xae, 0xae }},
	{ 0, { 4, 0xaf, 0xde, 0xaf, 0xaf }},
	{ 0, { 4, 0xa0, 0xde, 0xa0, 0xa0 }},
	{ 0, { 4, 0xa1, 0xde, 0xa1, 0xa1 }},
	{ 0, { 4, 0xa2, 0xde, 0xa2, 0xa2 }},
	{ 0, { 4, 0xa3, 0xde, 0xa3, 0xa3 }},
	{ 0, { 4, 0xa4, 0xde, 0xa4, 0xa4 }},
	{ 0, { 4, 0xa5, 0xde, 0xa5, 0xa5 }},
	{ 0, { 4, 0xa6, 0xde, 0xa6, 0xa6 }},
	{ 0, { 4, 0xa7, 0xde, 0xa7, 0xa7 }},
	{ 0, { 4, 0xa8, 0xde, 0xa8, 0xa8 }},
	{ 0, { 4, 0xa9, 0xde, 0xa9, 0xa9 }},
	{ 0, { 4, 0xaa, 0x88, 0xaa, 0xaa }},
	{ 0, { 4, 0xab, 0x88, 0xab, 0xab }},
	{ 0, { 4, 0xac, 0x88, 0xac, 0xac }},
	{ 0, { 4, 0xad, 0x88, 0xad, 0xad }},
	{ 0, { 4, 0xae, 0x88, 0xae, 0xae }},
	{ 0, { 4, 0xaf, 0x88, 0xaf, 0xaf }},
	{ 0, { 4, 0xa0, 0x88, 0xa0, 0xa0 }},
	{ 0, { 4, 0xa1, 0x88, 0xa1, 0xa1 }},
	{ 0, { 4, 0xa2, 0x88, 0xa2, 0xa2 }},
	{ 0, { 4, 0xa3, 0x88, 0xa3, 0xa3 }},
	{ 0, { 4, 0xa4, 0x88, 0xa4, 0xa4 }},
	{ 0, { 4, 0xa5, 0x88, 0xa5, 0xa5 }},
	{ 0, { 4, 0xa6, 0x88, 0xa6, 0xa6 }},
	{ 0, { 4, 0xa7, 0x88, 0xa7, 0xa7 }},
	{ 0, { 4, 0xa8, 0x88, 0xa8, 0xa8 }},
	{ 0, { 4, 0xa9, 0x88, 0xa9, 0xa9 }},
};
#define BTTEST_NUM (sizeof(data)/sizeof(struct bttest_data))
#define PREFIX "[BTHOST] "

#define MSG_OUT(f_, ...) do { printf(PREFIX); printf((f_), ##__VA_ARGS__); } while(0)
#define MSG_ERR(f_, ...) do { fprintf(stderr,PREFIX); fprintf(stderr, (f_), ##__VA_ARGS__); } while(0)

typedef int (*orig_open_t)(const char *pathname, int flags);
typedef int (*orig_poll_t)(struct pollfd *fds, nfds_t nfds, int timeout);
typedef int (*orig_read_t)(int fd, void *buf, size_t count);
typedef ssize_t (*orig_write_t)(int fd, const void *buf, size_t count);
typedef int (*orig_ioctl_t)(int fd, unsigned long request, char *p);
typedef int (*orig_timerfd_create_t)(int clockid, int flags);

int ioctl(int fd, unsigned long request, char *p)
{
	if (fd == bt_host_fd) {
		MSG_OUT("ioctl(%d, %lu, %p)\n", fd, request, p);
		/* TODO Check the request number */
		return 0;
	}

	orig_ioctl_t orig_ioctl;
	orig_ioctl = (orig_ioctl_t)dlsym(RTLD_NEXT, "ioctl");
	return orig_ioctl(fd, request, p);
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	int i, j;
	int ret = 0;
	int dropped = 0;
	struct pollfd *new_fds = calloc(nfds, sizeof(struct pollfd));
	j = 0;
	for (i = 0; i  < nfds; i++) {
		if (fds[i].fd == bt_host_fd) {
			short revents = fds[i].events;

			MSG_OUT("poll() on bt_host fd\n");

			if (stop)
				revents &= ~POLLIN;
			if (sent_id == -1)
				revents &= ~POLLOUT;
			fds[i].revents = revents;
			ret++;
			dropped++;
		} else if(fds[i].fd == timer_fd) {
			MSG_OUT("poll() on timerfd fd, dropping request\n");

			fds[i].revents = 0;
			dropped++;
		} else {
			new_fds[j].fd = fds[i].fd;
			new_fds[j].events = fds[i].events;
			/* Copy this to be sure */
			new_fds[j].revents = fds[i].revents;
			j++;
		}
	}
	orig_poll_t orig_poll;
	orig_poll = (orig_poll_t)dlsym(RTLD_NEXT, "poll");
	ret += orig_poll(new_fds, nfds - dropped, timeout);
	j = 0;
	for (i = 0; i < nfds; i++) {
		if (fds[i].fd != bt_host_fd && fds[i].fd != timer_fd) {
			fds[i].fd = new_fds[j].fd;
			fds[i].revents = new_fds[j].revents;
			j++;
		}
	}
	free(new_fds);
	return ret;
}

int open(const char *pathname, int flags)
{
	if (strcmp("/dev/bt-host", pathname) == 0) {
		MSG_OUT("open(%s, %x)\n", pathname, flags);
		bt_host_fd = socket(AF_UNIX, SOCK_STREAM, 0);
		return bt_host_fd;
	}
	orig_open_t orig_open;
	orig_open = (orig_open_t)dlsym(RTLD_NEXT, "open");
	return orig_open(pathname, flags);
}

int read(int fd, void *buf, size_t count)
{
	if (fd == bt_host_fd) {
		MSG_OUT("read(%d, %p, %ld)\n", fd, buf, count);

		if (sent_id == -1)
			sent_id = 0;
		else
			sent_id++;

		MSG_OUT("Send msg id %d\n", sent_id);

		if (count < data[sent_id].msg[0] + 1) {
			/*
			 * TODO handle this, not urgent, the real driver also gets it
			 * wrong
			 */
			MSG_ERR("Read size was too small\n");
			errno = ENOMEM;
			return -1;
		}
		if (sent_id == BTTEST_NUM - 1)
			stop = 1;

		memcpy(buf, data[sent_id].msg, data[sent_id].msg[0] + 1);
		return data[sent_id].msg[0] + 1;
	}

	orig_read_t orig_read;
	orig_read = (orig_read_t)dlsym(RTLD_NEXT, "read");
	return orig_read(fd, buf, count);
}

int write(int fd, const void *buf, size_t count)
{
	if (fd == bt_host_fd) {
		MSG_OUT("write(%d, %p, %ld)\n", fd, buf, count);
		if (count == 5 && ((char *)buf)[4] == 0xce) {
			MSG_ERR("CAUGHT A TIMEOUT!!!! 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",  ((char *)buf)[0], ((char *)buf)[1], ((char *)buf)[2], ((char *)buf)[3], ((char *)buf)[4]);
			exit(1);
		}
		if (memcmp(buf + 1, data[recv_id].msg + 1, count - 2) != 0) {
			int j;

			MSG_ERR("Bad response/inconsistent message index: %d\n", recv_id);
			for (j = 0; j < count - 2; j++)
				MSG_ERR("0x%02x vs 0x%02x\n", data[recv_id].msg[j + 1], ((char *)buf)[1 + j]);
		} else {
			MSG_OUT("Good response to message index: %d\n", recv_id);
			data[recv_id].status = 2;
		}
		if (recv_id == BTTEST_NUM - 1) {
			MSG_OUT("recieved a response to all messages, tentative success\n");
			exit(0);
		}
		recv_id++;
		return count;
	}
	orig_write_t orig_write;
	orig_write = (orig_write_t)dlsym(RTLD_NEXT, "write");
	return orig_write(fd, buf, count);
}

int timerfd_create(int clockid, int flags)
{
	orig_timerfd_create_t orig_timerfd_create;
	orig_timerfd_create = (orig_timerfd_create_t)dlsym(RTLD_NEXT, "timerfd_create");
	timer_fd = orig_timerfd_create(clockid, flags);
	return timer_fd;
}
