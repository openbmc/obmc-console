/**
 * Copyright © 2023 IBM Corporation
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

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "console-server.h"

#define SOCKET_HANDLER_PKT_SIZE 512
/* Set poll() timeout to 4000 uS, or 4 mS */
#define SOCKET_HANDLER_PKT_US_TIMEOUT 4000

struct client {
    struct dbus_handler *dh;
    struct ringbuffer_consumer *rbc;
    int fd;
};

struct dbus_handler {
    struct handler handler;
    struct console *console;

    struct client **clients;
    int n_clients;
};

static struct dbus_handler *to_dbus_handler(struct handler *handler)
{
    return container_of(handler, struct dbus_handler, handler);
}

static void dbus_client_close(struct client *client)
{
    struct dbus_handler *dh = client->dh;
    int idx;

    warn("dbus_client_close: Enter.");

    close(client->fd);

    if (client->rbc) {
        ringbuffer_consumer_unregister(client->rbc);
    }

    for (idx = 0; idx < dh->n_clients; idx++) {
        if (dh->clients[idx] == client) {
            break;
        }
    }

    assert(idx < dh->n_clients);

    free(client);
    client = NULL;

    dh->n_clients--;
    /*
     * We're managing an array of pointers to aggregates, so don't warn about sizeof() on a
     * pointer type.
     */
    /* NOLINTBEGIN(bugprone-sizeof-expression) */
    memmove(&dh->clients[idx], &dh->clients[idx + 1],
        sizeof(*dh->clients) * (dh->n_clients - idx));
    dh->clients =
        reallocarray(dh->clients, dh->n_clients, sizeof(*dh->clients));
    /* NOLINTEND(bugprone-sizeof-expression) */
}

static ssize_t dbus_send_all(struct client *client, void *buf, size_t len)
{
    int fd;
    int flags;
    ssize_t rc;
    size_t pos;

    warn("dbus_send_all: Enter. len = %d", len);

    if (len > SSIZE_MAX) {
        return -EINVAL;
    }

    fd = client->fd;

    flags = MSG_NOSIGNAL;

    for (pos = 0; pos < len; pos += rc) {
        rc = send(fd, (char *)buf + pos, len - pos, flags);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }

            return -1;
        }
        if (rc == 0) {
            return -1;
        }
    }

    warn("dbus_send_all: exit:. pos = %d", pos);
    return (ssize_t)pos;
}

/* Drain the queue to the socket and update the queue buffer. If force_len is
 * set, send at least that many bytes from the queue, possibly while blocking
 */
static int client_drain_queue(struct client *client, size_t force_len)
{
    uint8_t *buf;
    ssize_t wlen;
    size_t len;
    size_t total_len;

    total_len = 0;
    wlen = 0;

    warn("client_drain_queue: Enter. force_len: %d", force_len);

    for (;;) {
        len = ringbuffer_dequeue_peek(client->rbc, total_len, &buf);
        if (!len) {
            break;
        }

        wlen = dbus_send_all(client, buf, len);
        if (wlen <= 0) {
            break;
        }

        total_len += wlen;

        if (force_len && total_len >= force_len) {
            break;
        }
    }

    if (wlen < 0) {
        return -1;
    }

    if (force_len && total_len < force_len) {
        return -1;
    }

    ringbuffer_dequeue_commit(client->rbc, total_len);
    warn("client_drain_queue: exit. total_len: %d", total_len);
    return 0;
}

static enum ringbuffer_poll_ret client_ringbuffer_poll(void *arg,
                               size_t force_len)
{
    struct client *client = arg;
    int rc;

    warn("ringbuffer_poll_ret: enter. force_len: %d", force_len);

    rc = client_drain_queue(client, force_len);
    if (rc) {
        client->rbc = NULL;
        dbus_client_close(client);
        return RINGBUFFER_POLL_REMOVE;
    }

    return RINGBUFFER_POLL_OK;
}

/* Create socket pair and register one end as consume and return
 * the other end to the caller.
 * Return zero on sucess and return socket FD through the FD and
 * on error return the error code.
 */
int dbus_create_socket_consumer(struct console *console, int *FD)
{
    struct dbus_handler *dh = NULL;
    struct client *client;
    int fds[2];
    int i;
    int rc;
    int n;

    *FD = -1;

    warn("dbus_create_socket_consumer: enter");

    for (i = 0; i < console->n_handlers; i++) {
        if (strcmp(console->handlers[i]->name, "dbus-handler") == 0) {
            dh = to_dbus_handler(console->handlers[i]);
            break;
        }
    }

    if (!dh) {
        return ENOSYS;
    }

    /* Create a socketpair */
    rc = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    if (rc < 0) {
        rc = errno;
        warn("Failed to create socket pair: %s", strerror(rc));
        return rc;
    }

    warnx("NINAD: console fds: %d %d\n", fds[0], fds[1]);

    client = malloc(sizeof(*client));
    if (client == NULL) {
        return ENOMEM;
    }
    memset(client, 0, sizeof(*client));

    client->dh = dh;
    client->fd = fds[0];
    client->rbc = console_ringbuffer_consumer_register(
        dh->console, client_ringbuffer_poll, client);
    if (client->rbc == NULL) {
        return ENOMEM;
    }

    n = dh->n_clients++;

    /*
     * We're managing an array of pointers to aggregates, so don't warn about sizeof() on a
     * pointer type.
     */
    /* NOLINTBEGIN(bugprone-sizeof-expression) */
    dh->clients =
        reallocarray(dh->clients, dh->n_clients, sizeof(*dh->clients));
    /* NOLINTEND(bugprone-sizeof-expression) */
    dh->clients[n] = client;

    /* Return the second FD to caller. */
    *FD = fds[1];

    return 0;
}

static int dbus_handler_init(struct handler *handler, struct console *console,
               struct config *config __attribute__((unused)))
{
    struct dbus_handler *dh = to_dbus_handler(handler);

    warn("dbus_init: Enter.");

    dh->console = console;
    dh->clients = NULL;
    dh->n_clients = 0;

    return 0;
}

static void dbus_handler_fini(struct handler *handler)
{
    struct dbus_handler *dh = to_dbus_handler(handler);

    warn("dbus_fini: Enter. nClients: %d", dh->n_clients);

    while (dh->n_clients) {
        dbus_client_close(dh->clients[0]);
    }
}

static struct dbus_handler dbus_handler = {
    .handler = {
        .name       = "dbus-handler",
        .init       = dbus_handler_init,
        .fini       = dbus_handler_fini,
    },
};

console_handler_register(&dbus_handler.handler);
