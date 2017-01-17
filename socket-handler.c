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

const size_t buffer_size_max = 100 * 1024;

struct client {
	struct poller	*poller;
	int		fd;
	uint8_t		*buf;
	size_t		buf_alloc;
	size_t		buf_len;
};

struct socket_handler {
	struct handler	handler;
	struct console	*console;
	struct poller	*poller;
	int		sd;

	struct client	**clients;
	int		n_clients;
};

static struct socket_handler *to_socket_handler(struct handler *handler)
{
	return container_of(handler, struct socket_handler, handler);
}

static void client_close(struct socket_handler *sh, struct client *client)
{
	int idx;

	close(client->fd);
	if (client->poller)
		console_unregister_poller(sh->console, client->poller);

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

static size_t client_buffer_space(struct client *client)
{
	return buffer_size_max - client->buf_len;
}

static int client_queue_data(struct client *client, uint8_t *buf, size_t len)
{
	/* we may need to allocate space, but we know that increasing by len
	 * won't exceed buffer_size_max */

	if (client->buf_len + len > client->buf_alloc) {
		if (!client->buf_alloc)
			client->buf_alloc = 2048;
		client->buf_alloc *= 2;

		if (client->buf_alloc > buffer_size_max)
			client->buf_alloc = buffer_size_max;

		client->buf = realloc(client->buf, client->buf_alloc);
	}

	memcpy(client->buf + client->buf_len, buf, len);
	client->buf_len += len;
	return 0;
}

static ssize_t send_all(int fd, void *buf, size_t len, bool block)
{
	int rc, flags;
	size_t pos;

	flags = MSG_NOSIGNAL;
	if (!block)
		flags |= MSG_DONTWAIT;

	for (pos = 0; pos < len; pos += rc) {
		rc = send(fd, buf + pos, len - pos, flags);
		if (rc < 0) {
			if (!block && (errno == EAGAIN || errno == EWOULDBLOCK))
				break;

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
	ssize_t wlen;
	size_t len;
	bool block;

	if (!client->buf_len)
		return 0;

	block = false;
	len = client->buf_len;
	if (force_len) {
		block = true;
		len = force_len;
	}

	wlen = send_all(client->fd, client->buf, len, block);
	if (wlen < 0)
		return -1;

	if (force_len && wlen < force_len)
		return -1;

	client->buf_len -= wlen;
	memmove(client->buf, client->buf + wlen, client->buf_len);

	return 0;
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
		rc = client_drain_queue(client, 0);
		if (rc)
			goto err_close;
	}

	return POLLER_OK;

err_close:
	client->poller = NULL;
	client_close(sh, client);
	return POLLER_REMOVE;
}

static int client_data(struct client *client, uint8_t *buf, size_t len)
{
	ssize_t wlen;
	size_t space;
	int rc;

	space = client_buffer_space(client);

	/* blocking send to create space in the queue for this data */
	if (len > space) {
		rc = client_drain_queue(client, len - space);
		if (rc)
			return -1;
	}

	wlen = 0;

	/* if the queue is empty, try to send without queuing */
	if (!client->buf_len) {
		wlen = send_all(client->fd, buf, len, false);
		if (wlen < 0)
			return -1;

		if (wlen == len)
			return 0;
	}

	/* queue anything unsent. this queue should always succeed, as we've
	 * created space above */
	client_queue_data(client, buf + wlen, len - wlen);

	return client_drain_queue(client, 0);
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

	client->fd = fd;
	client->poller = console_register_poller(sh->console, handler,
			client_poll, client->fd, POLLIN, client);

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

	sh->poller = console_register_poller(console, handler, socket_poll,
			sh->sd, POLLIN, NULL);

	return 0;
}

static int socket_data(struct handler *handler, uint8_t *buf, size_t len)
{
	struct socket_handler *sh = to_socket_handler(handler);
	int i, rc;

	for (i = 0; i < sh->n_clients; i++) {
		struct client *client = sh->clients[i];
		rc = client_data(client, buf, len);
		if (!rc)
			continue;

		/* if we failed to send data, close the client. This will
		 * remove it from the clients array, so skip back to the item
		 * that has taken its place
		 */
		client_close(sh, client);
		i--;
	}
	return 0;
}

static void socket_fini(struct handler *handler)
{
	struct socket_handler *sh = to_socket_handler(handler);
	int i;

	for (i = 0; i < sh->n_clients; i++)
		client_close(sh, sh->clients[i]);

	if (sh->poller)
		console_unregister_poller(sh->console, sh->poller);

	close(sh->sd);
}

static struct socket_handler socket_handler = {
	.handler = {
		.name		= "socket",
		.init		= socket_init,
		.data_in	= socket_data,
		.fini		= socket_fini,
	},
};

console_register_handler(&socket_handler.handler);

