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
    struct dbus_handler *dh = (struct dbus_handler *)client->private;
    int idx;

    warnx("dbus_client_close: Enter. fd=%d", client->fd);

    close(client->fd);

    if (client->poller) {
        console_poller_unregister(dh->console, client->poller);
    }

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
    int rc = 0;
    int n;

    *FD = -1;

    warnx("dbus_create_socket_consumer: enter");

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
        warnx("Failed to create socket pair: %s", strerror(rc));
        return rc;
    }

    warnx("NINAD: console fds: %d %d\n", fds[0], fds[1]);

    client = malloc(sizeof(*client));
    if (client == NULL) {
        warnx("Failed to allocate client structure.\n");
        rc = ENOMEM;
        goto close_fds;
    }
    memset(client, 0, sizeof(*client));

    client->private = dh;
    client->fd = fds[0];
    client->console = dh->console;
    client->close = dbus_client_close;
    client->poller = console_poller_register(dh->console, &dh->handler,
                         client_poll, client_timeout,
                         client->fd, POLLIN | POLLHUP, client);
    client->rbc = console_ringbuffer_consumer_register(
        dh->console, client_ringbuffer_poll, client);
    if (client->rbc == NULL) {
        warnx("Failed to register a consumer.\n");
        rc = ENOMEM;
        goto free_client;
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

    warnx("NINAD: DBUS FD: %d\n", fds[1]);

    return 0;

free_client:
    free(client);
    client = NULL;
close_fds:
    close(fds[0]);
    close(fds[1]);
    return rc;
}

static int dbus_handler_init(struct handler *handler, struct console *console,
               struct config *config __attribute__((unused)))
{
    struct dbus_handler *dh = to_dbus_handler(handler);

    warnx("dbus_init: Enter.");

    dh->console = console;
    dh->clients = NULL;
    dh->n_clients = 0;

    return 0;
}

static void dbus_handler_fini(struct handler *handler)
{
    struct dbus_handler *dh = to_dbus_handler(handler);

    warnx("dbus_fini: Enter. nClients: %d", dh->n_clients);

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
