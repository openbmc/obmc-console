/**
 * Copyright © 2016 IBM Corporation
 * Copyright © 2025 Google Corporation
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

#include <asm-generic/errno-base.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <endian.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <systemd/sd-daemon.h>

#include "console-server.h"

#define SOCKET_HANDLER_PKT_SIZE 512
/* Set poll() timeout to 4000 uS, or 4 mS */
#define SOCKET_HANDLER_PKT_US_TIMEOUT 4000

#define CONSOLE_PII_PREFIX_MACHINE "obmc-console-pii-machine"
#define CONSOLE_PII_PREFIX_USER	   "obmc-console-pii-user"

struct pii_client_list {
	struct pii_client_list *prev;
	struct pii_client_list *next;
};

static void pii_client_list_init(struct pii_client_list *node)
{
	node->next = node;
	node->prev = node;
}

struct pii_client {
	struct pii_client_list list;
	struct pii_handler *ph;
	struct poller *poller;
	struct ringbuffer_consumer *rbc;
	int fd;
	bool blocked;
	enum pii_data_t client_role;
	bool enabled;
	uint8_t *pending_data;
	uint8_t *pending_data_ptr;
	size_t pending_len;
};

struct pii_handler {
	struct handler handler;
	struct console *console;
	struct pii_client_list client_list;

	sd_bus_slot *pii_watcher;
	enum pii_data_t pii_state;

	int machine_sd;
	struct poller *machine_sd_poller;

	int user_sd;
	struct poller *user_sd_poller;
};

static struct timeval const socket_handler_timeout = {
	.tv_sec = 0,
	.tv_usec = SOCKET_HANDLER_PKT_US_TIMEOUT
};

static struct pii_client *to_pii_client(struct pii_client_list *list)
{
	return container_of(list, struct pii_client, list);
}

static void pii_client_list_insert(struct pii_client_list *head,
				   struct pii_client *client)
{
	struct pii_client_list *next = head->next;
	struct pii_client_list *curt = &client->list;

	curt->next = next;
	curt->prev = head;
	head->next = curt;
	next->prev = curt;
}

static struct pii_handler *to_pii_handler(struct handler *handler)
{
	return container_of(handler, struct pii_handler, handler);
}

static void pii_client_close(struct pii_client *client)
{
	struct pii_handler *ph = client->ph;
	struct pii_client_list *cur = &client->list;

	cur->prev->next = cur->next;
	cur->next->prev = cur->prev;

	close(client->fd);
	if (client->poller) {
		console_poller_unregister(ph->console, client->poller);
	}

	if (client->rbc) {
		ringbuffer_consumer_unregister(client->rbc);
	}

	free(client);
}

static void pii_client_set_blocked(struct pii_client *client, bool blocked)
{
	int events;

	if (client->blocked == blocked) {
		return;
	}

	client->blocked = blocked;

	events = POLLIN;
	if (client->blocked) {
		events |= POLLOUT;
	}

	console_poller_set_events(client->ph->console, client->poller, events);
}

static ssize_t pii_client_send_all(struct pii_client *client, void *buf,
				   size_t len, bool block)
{
	int fd;
	int flags;
	ssize_t rc;
	size_t pos;

	if (len > SSIZE_MAX) {
		return -EINVAL;
	}

	fd = client->fd;

	flags = MSG_NOSIGNAL;
	if (!block) {
		flags |= MSG_DONTWAIT;
	}

	for (pos = 0; pos < len; pos += rc) {
		rc = send(fd, (char *)buf + pos, len - pos, flags);
		if (rc < 0) {
			if (!block &&
			    (errno == EAGAIN || errno == EWOULDBLOCK)) {
				pii_client_set_blocked(client, true);
				break;
			}

			if (errno == EINTR) {
				continue;
			}

			return -1;
		}
		if (rc == 0) {
			return -1;
		}
	}

	return (ssize_t)pos;
}

static int pii_client_send_pending_data(struct pii_client *client)
{
	ssize_t wlen = 0;

	while (client->pending_len) {
		wlen = pii_client_send_all(client, client->pending_data_ptr,
					   client->pending_len, false);
		if (wlen <= 0) {
			break;
		}
		client->pending_data_ptr += wlen;
		client->pending_len -= wlen;
	}

	if (wlen < 0) {
		return -1;
	}

	return 0;
}

/* Drain the queue to the socket and update the queue buffer. If force_len is
 * set, send at least that many bytes from the queue, possibly while blocking
 */
static int pii_client_drain_queue(struct pii_client *client, size_t force_len)
{
	uint8_t *buf;
	ssize_t wlen;
	size_t len;
	size_t total_len;
	bool block;

	total_len = 0;
	wlen = 0;
	block = (force_len > 0);

	/* if we're already blocked, no need try unblock write */
	if (!block && client->blocked) {
		return 0;
	}

	if (!client->enabled) {
		if (client->pending_len) {
			return pii_client_send_pending_data(client);
		}
		return 0;
	}

	for (;;) {
		len = ringbuffer_dequeue_peek(client->rbc, total_len, &buf);
		if (!len) {
			break;
		}

		wlen = pii_client_send_all(client, buf, len, block);
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
	return 0;
}

static enum ringbuffer_poll_ret pii_client_ringbuffer_poll(void *arg,
							   size_t force_len)
{
	struct pii_client *client = arg;
	size_t len;
	int rc;

	len = ringbuffer_len(client->rbc);
	if (!force_len && (len < SOCKET_HANDLER_PKT_SIZE)) {
		/* Do nothing until many small requests have accumulated, or
		 * the UART is idle for awhile (as determined by the timeout
		 * value supplied to the poll function call in console_server.c. */
		console_poller_set_timeout(client->ph->console, client->poller,
					   &socket_handler_timeout);
		return RINGBUFFER_POLL_OK;
	}

	rc = pii_client_drain_queue(client, force_len);
	if (rc) {
		client->rbc = NULL;
		pii_client_close(client);
		return RINGBUFFER_POLL_REMOVE;
	}

	return RINGBUFFER_POLL_OK;
}

static enum poller_ret
pii_client_timeout(struct handler *handler __attribute__((unused)), void *data)
{
	struct pii_client *client = data;
	int rc = 0;

	if (client->blocked) {
		/* nothing to do here, we'll call client_drain_queue when
		 * we become unblocked */
		return POLLER_OK;
	}

	rc = pii_client_drain_queue(client, 0);
	if (rc) {
		client->poller = NULL;
		pii_client_close(client);
		return POLLER_REMOVE;
	}

	return POLLER_OK;
}

static enum poller_ret pii_client_poll(struct handler *handler, int events,
				       void *data)
{
	struct pii_handler *ph = to_pii_handler(handler);
	struct pii_client *client = data;
	uint8_t buf[4096];
	ssize_t rc;

	if (events & POLLIN) {
		rc = recv(client->fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (rc < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return POLLER_OK;
			}
			goto err_close;
		}
		if (rc == 0) {
			goto err_close;
		}

		if (client->enabled) {
			console_data_out(ph->console, buf, rc);
		}
	}

	if (events & POLLOUT) {
		pii_client_set_blocked(client, false);
		rc = pii_client_drain_queue(client, 0);
		if (rc) {
			goto err_close;
		}
	}

	return POLLER_OK;

err_close:
	client->poller = NULL;
	pii_client_close(client);
	return POLLER_REMOVE;
}

static void pii_client_enable(struct pii_client *client)
{
	if (client->enabled) {
		return;
	}
	client->rbc = console_ringbuffer_consumer_register(
		client->ph->console, pii_client_ringbuffer_poll, client);
	client->enabled = true;
}

static void pii_client_free_pending_data(struct pii_client *client)
{
	if (client->pending_data) {
		free(client->pending_data);
		client->pending_data = NULL;
		client->pending_data_ptr = NULL;
		client->pending_len = 0;
	}
}

static void pii_client_save_pending_data(struct pii_client *client)
{
	static const char pii_client_disabled_message[] = "\nPII mask off\n";

	size_t total_len;
	size_t len;
	uint8_t *buf;

	/* free old pending data */
	pii_client_free_pending_data(client);
	/* allocate a buffer to save pending data and PII_CLIENT_DISABLED_MESSAGE */
	client->pending_data = malloc(ringbuffer_len(client->rbc) +
				      sizeof(pii_client_disabled_message) - 1);
	if (!client->pending_data) {
		warn("lost pending data %zu", ringbuffer_len(client->rbc));
		return;
	}

	total_len = 0;
	for (;;) {
		len = ringbuffer_dequeue_peek(client->rbc, total_len, &buf);
		if (!len) {
			break;
		}
		memcpy(client->pending_data + total_len, buf, len);
		total_len += len;
	}
	ringbuffer_dequeue_commit(client->rbc, total_len);
	/* and append PII_CLIENT_DISABLED_MESSAGE*/
	memcpy(client->pending_data + total_len, pii_client_disabled_message,
	       sizeof(pii_client_disabled_message) - 1);
	client->pending_data_ptr = client->pending_data;
	client->pending_len =
		total_len + sizeof(pii_client_disabled_message) - 1;
}

static void pii_client_disable(struct pii_client *client)
{
	if (!client->enabled) {
		return;
	}
	assert(client->rbc);
	pii_client_save_pending_data(client);
	ringbuffer_consumer_unregister(client->rbc);
	client->rbc = NULL;
	client->enabled = false;
}

static enum poller_ret pii_socket_poll(struct pii_handler *ph, int events,
				       enum pii_data_t client_role)
{
	struct pii_client *client;
	int sd;
	int fd;

	if (!(events & POLLIN)) {
		return POLLER_OK;
	}
	assert(client_role == PII_DATA_MACHINE || client_role == PII_DATA_USER);
	sd = (client_role == PII_DATA_MACHINE) ? ph->machine_sd : ph->user_sd;
	fd = accept(sd, NULL, NULL);
	if (fd < 0) {
		return POLLER_OK;
	}

	client = malloc(sizeof(*client));
	memset(client, 0, sizeof(*client));
	client->client_role = client_role;
	client->ph = ph;
	client->fd = fd;
	if (ph->pii_state == PII_DATA_UNKNOWN ||
	    client->client_role == ph->pii_state) {
		pii_client_enable(client);
	} else {
		pii_client_disable(client);
	}
	client->poller = console_poller_register(ph->console, &ph->handler,
						 pii_client_poll,
						 pii_client_timeout, client->fd,
						 POLLIN, client);
	pii_client_list_insert(&ph->client_list, client);
	return POLLER_OK;
}

static enum poller_ret pii_machine_socket_poll(struct handler *handler,
					       int events,
					       void *data
					       __attribute__((unused)))
{
	struct pii_handler *ph = to_pii_handler(handler);
	return pii_socket_poll(ph, events, PII_DATA_MACHINE);
}

static enum poller_ret pii_user_socket_poll(struct handler *handler, int events,
					    void *data __attribute__((unused)))
{
	struct pii_handler *ph = to_pii_handler(handler);
	return pii_socket_poll(ph, events, PII_DATA_USER);
}

int pii_socket_path(socket_path_t sun_path, size_t *len, const char *id,
		    enum pii_data_t pii_data)
{
	int rc;
	const char *path_template;

	if (!id) {
		errno = EINVAL;
		return -1;
	}

	switch (pii_data) {
	case PII_DATA_MACHINE:
		path_template = CONSOLE_PII_PREFIX_MACHINE ".%s";
		break;
	case PII_DATA_USER:
		path_template = CONSOLE_PII_PREFIX_USER ".%s";
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	rc = snprintf(sun_path + 1, sizeof(socket_path_t) - 1, path_template,
		      id);
	if (rc < 0) {
		return rc;
	}

	if ((size_t)rc > (sizeof(socket_path_t) - 1)) {
		errno = ENOSPC;
		return -1;
	}

	sun_path[0] = '\0';
	if (len) {
		*len = rc + 1 /* Capture NUL prefix */;
	}
	return 0;
}

static int pii_setup_socket(struct pii_handler *ph, enum pii_data_t pii_data)
{
	struct sockaddr_un addr;
	size_t addrlen;
	size_t len;
	int sd;
	int rc;

	assert(ph);
	assert(pii_data == PII_DATA_MACHINE || pii_data == PII_DATA_USER);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	rc = pii_socket_path(addr.sun_path, &len, ph->console->console_id,
			     pii_data);
	if (rc < 0) {
		warn("Failed to configure socket for pii_data(%d): (%d)",
		     pii_data, rc);
		return -1;
	}

	sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sd < 0) {
		warn("Can't create socket");
		return -1;
	}

	addrlen = sizeof(addr) - sizeof(addr.sun_path) + len;

	rc = bind(sd, (struct sockaddr *)&addr, addrlen);
	if (rc) {
		socket_path_t name;
		console_socket_path_readable(&addr, addrlen, name);
		warn("Can't bind to socket path %s (terminated at first null)",
		     name);
		goto error_cleanup;
	}

	rc = listen(sd, 1);
	if (rc) {
		warn("Can't listen for incoming connections");
		goto error_cleanup;
	}

	if (pii_data == PII_DATA_MACHINE) {
		ph->machine_sd = sd;
	}
	if (pii_data == PII_DATA_USER) {
		ph->user_sd = sd;
	}

	return 0;

error_cleanup:
	close(sd);
	return -1;
}

static const char *systemd_svc = "org.freedesktop.systemd1";
static const char *systemd_unit_obj_prefix = "/org/freedesktop/systemd1/unit";
static const char *dbus_properties_interface =
	"org.freedesktop.DBus.Properties";
static const char *systemd_unit_interface = "org.freedesktop.systemd1.Unit";
static const char *properties_changed_signal = "PropertiesChanged";
static const char *active_state_property = "ActiveState";

static enum pii_data_t pii_state_from_signal(sd_bus_message *m)
{
	int rc;
	const char *property;
	const char *active_state = NULL;

	rc = sd_bus_message_enter_container(m, 'a', "{sv}");
	if (rc < 0) {
		warnx("Failed to enter changed properties array: %s",
		      strerror(-rc));
		return PII_DATA_UNKNOWN;
	}
	rc = 0;
	while (sd_bus_message_read(m, "{sv}", &property, "v") > 0) {
		if (strcmp(property, "ActiveState") == 0) {
			rc = sd_bus_message_read_basic(m, 's', &active_state);
			break;
		}
		// Skip unrelated properties
		sd_bus_message_skip(m, "v");
	}
	sd_bus_message_exit_container(m);

	if (rc < 0) {
		warnx("Failed to read ActiveState value: %s\n", strerror(-rc));
		return PII_DATA_UNKNOWN;
	}

	if (!active_state) {
		return PII_DATA_UNKNOWN;
	}

	if (strcasecmp(active_state, "active") == 0) {
		return PII_DATA_USER;
	}

	if (strcasecmp(active_state, "inactive") == 0) {
		return PII_DATA_MACHINE;
	}

	return PII_DATA_UNKNOWN;
}

static int pii_signal_handler(sd_bus_message *message, void *userdata,
			      sd_bus_error *ret_error)
{
	struct pii_handler *ph = userdata;
	enum pii_data_t new_pii_state;
	struct pii_client_list *pos;
	struct pii_client_list *n;

	assert(ph);

	if (ret_error && sd_bus_error_is_set(ret_error)) {
		warnx("Error in pii signal: %s - %s\n", ret_error->name,
		      ret_error->message);
		return -1;
	}

	new_pii_state = pii_state_from_signal(message);
	if (new_pii_state == PII_DATA_UNKNOWN) {
		return 0;
	}

	ph->pii_state = new_pii_state;

	for (pos = ph->client_list.next, n = pos->next; pos != &ph->client_list;
	     pos = n, n = pos->next) {
		struct pii_client *client = to_pii_client(pos);
		if (client->client_role == new_pii_state) {
			pii_client_enable(client);
			continue;
		}
		pii_client_disable(client);
		if (pii_client_send_pending_data(client) < 0) {
			warn("Send pending data to client(%p) failed, close it",
			     (void *)client);
			pii_client_close(client);
		}
	}

	return 0;
}

static int pii_setup_watcher(struct pii_handler *ph, const char *unit_obj_path)
{
	int rc;
	assert(ph);
	assert(unit_obj_path);

	rc = sd_bus_match_signal(ph->console->bus, &ph->pii_watcher,
				 systemd_svc, unit_obj_path,
				 dbus_properties_interface,
				 properties_changed_signal, pii_signal_handler,
				 ph);

	if (rc < 0) {
		warnx("Failed to subscribe signal %s of %s: %s\n",
		      properties_changed_signal, unit_obj_path, strerror(-rc));
		return -1;
	}

	return 0;
}

static int pii_get_pii_state(struct pii_handler *ph, const char *unit_obj_path)
{
	int rc;
	char *property_value = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	assert(ph);
	assert(unit_obj_path);
	ph->pii_state = PII_DATA_UNKNOWN;

	rc = sd_bus_get_property_string(ph->console->bus, systemd_svc,
					unit_obj_path, systemd_unit_interface,
					active_state_property, &error,
					&property_value);
	if (rc < 0) {
		warnx("get property %s of %s failed %s", active_state_property,
		      unit_obj_path, error.message);
		goto cleanup;
	}
	if (property_value && strcasecmp("active", property_value) == 0) {
		ph->pii_state = PII_DATA_USER;
	}
	if (property_value && strcasecmp("inactive", property_value) == 0) {
		ph->pii_state = PII_DATA_MACHINE;
	}

cleanup:
	if (property_value) {
		free(property_value);
	}
	sd_bus_error_free(&error);

	return (rc < 0) ? -1 : 0;
}

static void pii_cleanup(struct pii_handler *ph)
{
	struct pii_client_list *pos;

	if (!ph) {
		return;
	}

	pos = ph->client_list.next;
	while (pos != &ph->client_list) {
		struct pii_client *client = to_pii_client(pos);
		pos = pos->next;
		pii_client_close(client);
	}

	if (ph->user_sd_poller) {
		console_poller_unregister(ph->console, ph->user_sd_poller);
		ph->user_sd_poller = NULL;
		close(ph->user_sd);
	}

	if (ph->machine_sd_poller) {
		console_poller_unregister(ph->console, ph->user_sd_poller);
		ph->machine_sd_poller = NULL;
		close(ph->machine_sd);
	}

	if (ph->pii_watcher) {
		sd_bus_slot_unref(ph->pii_watcher);
		ph->pii_watcher = NULL;
	}
}

static int pii_init(struct handler *handler, struct console *console,
		    struct config *config __attribute__((unused)))
{
	int rc;
	const char *pii_notifier;
	struct pii_handler *ph = to_pii_handler(handler);
	char *unit_obj_path = NULL;

	ph->console = console;
	ph->pii_watcher = NULL;
	ph->pii_state = PII_DATA_UNKNOWN;
	ph->machine_sd = 0;
	ph->machine_sd_poller = NULL;
	ph->user_sd = 0;
	ph->user_sd_poller = NULL;
	pii_client_list_init(&ph->client_list);

	pii_notifier = config_get_value(config, "pii_notifier");
	if (!pii_notifier) {
		warnx("No PII notifier defined, no PII filtering enforced");
	}

	if (pii_notifier) {
		rc = sd_bus_path_encode(systemd_unit_obj_prefix, pii_notifier,
					&unit_obj_path);
		if (rc < 0) {
			warnx("Encode %s failed: %s", pii_notifier,
			      strerror(-rc));
			goto init_error;
		}
		rc = pii_get_pii_state(ph, unit_obj_path);
		if (rc < 0) {
			goto init_error;
		}
		rc = pii_setup_watcher(ph, unit_obj_path);
		if (rc < 0) {
			goto init_error;
		}
		if (unit_obj_path) {
			free(unit_obj_path);
		}
	}

	rc = pii_setup_socket(ph, PII_DATA_MACHINE);
	if (rc < 0) {
		goto init_error;
	}

	rc = pii_setup_socket(ph, PII_DATA_USER);
	if (rc < 0) {
		goto init_error;
	}

	ph->machine_sd_poller = console_poller_register(console, handler,
							pii_machine_socket_poll,
							NULL, ph->machine_sd,
							POLLIN, NULL);

	ph->user_sd_poller = console_poller_register(console, handler,
						     pii_user_socket_poll, NULL,
						     ph->user_sd, POLLIN, NULL);

	return 0;

init_error:
	if (unit_obj_path) {
		free(unit_obj_path);
	}
	pii_cleanup(ph);
	return -1;
}

static void pii_fini(struct handler *handler)
{
	struct pii_handler *ph = to_pii_handler(handler);
	pii_cleanup(ph);
}

static struct pii_handler pii_handler = {
	.handler = {
		.name		= "pii",
		.init		= pii_init,
		.fini		= pii_fini,
	},
};

console_handler_register(&pii_handler.handler);
