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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <endian.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "console-server.h"

struct client {
	struct socket_handler		*sh;
	struct poller			*poller;
	struct ringbuffer_consumer	*rbc;
	int				fd;
	bool				blocked;
};

struct socket_handler {
	struct handler		handler;
	struct console		*console;
	struct poller		*poller;
	int			sd;

	struct client		**clients;
	int			n_clients;
};

static struct socket_handler *to_socket_handler(struct handler *handler)
{
	return container_of(handler, struct socket_handler, handler);
}

static void client_close(struct client *client)
{
	struct socket_handler *sh = client->sh;
	int idx;

	close(client->fd);
	if (client->poller)
		console_poller_unregister(sh->console, client->poller);

	if (client->rbc)
		ringbuffer_consumer_unregister(client->rbc);

	for (idx = 0; idx < sh->n_clients; idx++)
		if (sh->clients[idx] == client)
			break;

	assert(idx < sh->n_clients);

	free(client);
	client = NULL;

	sh->n_clients--;
	memmove(&sh->clients[idx], &sh->clients[idx+1],
			sizeof(*sh->clients) * (sh->n_clients - idx));
	sh->clients = realloc(sh->clients,
			sizeof(*sh->clients) * sh->n_clients);
}

static void client_set_blocked(struct client *client, bool blocked)
{
	int events;

	if (client->blocked == blocked)
		return;

	client->blocked = blocked;

	events = POLLIN;
	if (client->blocked)
		events |= POLLOUT;

	console_poller_set_events(client->sh->console, client->poller, events);
}

static ssize_t send_all(struct client *client, void *buf,
		size_t len, bool block)
{
	int fd, rc, flags;
	size_t pos;

	fd = client->fd;

	flags = MSG_NOSIGNAL;
	if (!block)
		flags |= MSG_DONTWAIT;

	for (pos = 0; pos < len; pos += rc) {
		rc = send(fd, buf + pos, len - pos, flags);
		if (rc < 0) {
			if (!block && (errno == EAGAIN ||
						errno == EWOULDBLOCK)) {
				client_set_blocked(client, true);
				break;
			}

			if (errno == EINTR)
				continue;

			return -1;
		}
		if (rc == 0)
			return -1;
	}

	return pos;
}

/* Drain the queue to the socket and update the queue buffer. If force_len is
 * set, send at least that many bytes from the queue, possibly while blocking
 */
static int client_drain_queue(struct client *client, size_t force_len)
{
	uint8_t *buf;
	ssize_t wlen;
	size_t len, total_len;
	bool block;

	total_len = 0;
	wlen = 0;
	block = !!force_len;

	/* if we're already blocked, no need for the write */
	if (!block && client->blocked)
		return 0;

	for (;;) {
		len = ringbuffer_dequeue_peek(client->rbc, total_len, &buf);
		if (!len)
			break;

		wlen = send_all(client, buf, len, block);
		if (wlen <= 0)
			break;

		total_len += wlen;

		if (force_len && total_len >= force_len)
			break;
	}

	if (wlen < 0)
		return -1;

	if (force_len && total_len < force_len)
		return -1;

	ringbuffer_dequeue_commit(client->rbc, total_len);
	return 0;
}

static enum ringbuffer_poll_ret client_ringbuffer_poll(void *arg,
		size_t force_len)
{
	struct client *client = arg;
	int rc;

	rc = client_drain_queue(client, force_len);
	if (rc) {
		client->rbc = NULL;
		client_close(client);
		return RINGBUFFER_POLL_REMOVE;
	}

	return RINGBUFFER_POLL_OK;
}

static enum poller_ret client_poll(struct handler *handler,
		int events, void *data)
{
	struct socket_handler *sh = to_socket_handler(handler);
	struct client *client = data;
	uint8_t buf[4096];
	int rc;

	if (events & POLLIN) {
		rc = recv(client->fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (rc < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return POLLER_OK;
			else
				goto err_close;
		}
		if (rc == 0)
			goto err_close;

		console_data_out(sh->console, buf, rc);
	}

	if (events & POLLOUT) {
		client_set_blocked(client, false);
		rc = client_drain_queue(client, 0);
		if (rc)
			goto err_close;
	}

	return POLLER_OK;

err_close:
	client->poller = NULL;
	client_close(client);
	return POLLER_REMOVE;
}

static enum poller_ret socket_poll(struct handler *handler,
		int events, void __attribute__((unused)) *data)
{
	struct socket_handler *sh = to_socket_handler(handler);
	struct client *client;
	int fd, n;

	if (!(events & POLLIN))
		return POLLER_OK;

	fd = accept(sh->sd, NULL, NULL);
	if (fd < 0)
		return POLLER_OK;

	client = malloc(sizeof(*client));
	memset(client, 0, sizeof(*client));

	client->sh = sh;
	client->fd = fd;
	client->poller = console_poller_register(sh->console, handler,
			client_poll, client->fd, POLLIN, client);
	client->rbc = console_ringbuffer_consumer_register(sh->console,
			client_ringbuffer_poll, client);

	n = sh->n_clients++;
	sh->clients = realloc(sh->clients,
			sizeof(*sh->clients) * sh->n_clients);
	sh->clients[n] = client;

	return POLLER_OK;

}

static int socket_init(struct handler *handler, struct console *console,
		struct config *config __attribute__((unused)))
{
	struct socket_handler *sh = to_socket_handler(handler);
	struct sockaddr_un addr;
	int rc;

	sh->console = console;
	sh->clients = NULL;
	sh->n_clients = 0;

	sh->sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(sh->sd < 0) {
		warn("Can't create socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(&addr.sun_path, &console_socket_path, console_socket_path_len);

	rc = bind(sh->sd, (struct sockaddr *)&addr, sizeof(addr));
	if (rc) {
		warn("Can't bind to socket path %s",
				console_socket_path_readable);
		return -1;
	}

	rc = listen(sh->sd, 1);
	if (rc) {
		warn("Can't listen for incoming connections");
		return -1;
	}

	sh->poller = console_poller_register(console, handler, socket_poll,
			sh->sd, POLLIN, NULL);

	return 0;
}

static void socket_fini(struct handler *handler)
{
	struct socket_handler *sh = to_socket_handler(handler);

	while (sh->n_clients)
		client_close(sh->clients[0]);

	if (sh->poller)
		console_poller_unregister(sh->console, sh->poller);

	close(sh->sd);
}

static struct socket_handler socket_handler = {
	.handler = {
		.name		= "socket",
		.init		= socket_init,
		.fini		= socket_fini,
	},
};

console_handler_register(&socket_handler.handler);

