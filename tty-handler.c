/**
 * Copyright © 2016 IBM Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "console-server.h"

struct tty_handler {
	struct handler			handler;
	struct console			*console;
	struct ringbuffer_consumer	*rbc;
	struct poller			*poller;
	int				fd;
};

static struct tty_handler *to_tty_handler(struct handler *handler)
{
	return container_of(handler, struct tty_handler, handler);
}

static int tty_drain_queue(struct tty_handler *th, size_t force_len)
{
	size_t len, total_len;
	ssize_t wlen;
	uint8_t *buf;
	int flags;

	/* if we're forcing data, we need to clear non-blocking mode */
	if (force_len) {
		flags = fcntl(th->fd, F_GETFL, 0);
		if (flags & O_NONBLOCK) {
			flags &= ~O_NONBLOCK;
			fcntl(th->fd, F_SETFL, flags);
		}
	}

	total_len = 0;

	for (;;) {
		len = ringbuffer_dequeue_peek(th->rbc, total_len, &buf);
		if (!len)
			break;

		/* write as little as possible while blocking */
		if (force_len && force_len < total_len + len)
			len = force_len - total_len;

		wlen = write(th->fd, buf, len);
		if (wlen < 0) {
			if (errno == EINTR)
				continue;
			if ((errno == EAGAIN || errno == EWOULDBLOCK)
					&& !force_len)
				break;
			warn("failed writing to local tty; disabling");
			return -1;
		}

		total_len += wlen;

		if (force_len && total_len >= force_len)
			break;
	}

	ringbuffer_dequeue_commit(th->rbc, total_len);

	if (force_len)
		fcntl(th->fd, F_SETFL, flags | O_NONBLOCK);

	return 0;
}

static enum ringbuffer_poll_ret tty_ringbuffer_poll(void *arg, size_t force_len)
{
	struct tty_handler *th = arg;
	int rc;

	rc = tty_drain_queue(th, force_len);
	if (rc) {
		console_poller_unregister(th->console, th->poller);
		return RINGBUFFER_POLL_REMOVE;
	}

	return RINGBUFFER_POLL_OK;
}

static enum poller_ret tty_poll(struct handler *handler,
		int events, void __attribute__((unused)) *data)
{
	struct tty_handler *th = to_tty_handler(handler);
	uint8_t buf[4096];
	ssize_t len;
	int rc;

	if (events & POLLIN) {
		len = read(th->fd, buf, sizeof(buf));
		if (len <= 0) {
			th->poller = NULL;
			close(th->fd);
			return POLLER_REMOVE;
		}

		console_data_out(th->console, buf, len);
	}

	if (events & POLLOUT) {
		rc = tty_drain_queue(th, 0);
		if (rc) {
			ringbuffer_consumer_unregister(th->rbc);
			return POLLER_REMOVE;
		}
	}

	return POLLER_OK;
}

static int tty_init(struct handler *handler, struct console *console,
		struct config *config __attribute__((unused)))
{
	struct tty_handler *th = to_tty_handler(handler);
	const char *tty_name;
	char *tty_path;
	int rc;

	tty_name = config_get_value(config, "local-tty");
	if (!tty_name)
		return -1;

	rc = asprintf(&tty_path, "/dev/%s", tty_name);
	if (!rc)
		return -1;

	th->fd = open(tty_path, O_RDWR | O_NONBLOCK);
	if (th->fd < 0) {
		warn("Can't open %s; disabling local tty", tty_name);
		free(tty_path);
		return -1;
	}

	free(tty_path);

	th->console = console;
	th->poller = console_poller_register(console, handler, tty_poll,
			th->fd, POLLIN, NULL);
	th->rbc = console_ringbuffer_consumer_register(console,
			tty_ringbuffer_poll, th);

	return 0;
}

static void tty_fini(struct handler *handler)
{
	struct tty_handler *th = to_tty_handler(handler);
	if (th->poller)
		console_poller_unregister(th->console, th->poller);
	close(th->fd);
}

static struct tty_handler tty_handler = {
	.handler = {
		.name		= "tty",
		.init		= tty_init,
		.fini		= tty_fini,
	},
};

console_handler_register(&tty_handler.handler);

