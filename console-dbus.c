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
#include <errno.h>
#include <err.h>
#include <string.h>
#include <sys/socket.h>

#include "console-server.h"

/* size of the dbus object path length */
const size_t dbus_obj_path_len = 1024;

#define DBUS_ERR    "org.openbmc.error"
#define DBUS_NAME   "xyz.openbmc_project.Console.%s"
#define OBJ_NAME    "/xyz/openbmc_project/console/%s"
#define TTY_INTF    "xyz.openbmc_project.console"
#define ACCESS_INTF "xyz.openbmc_project.Console.Access"

static void tty_change_baudrate(struct console *console)
{
	struct handler *handler;
	int i;
	int rc;

	tty_init_termios(console);

	for (i = 0; i < console->n_handlers; i++) {
		handler = console->handlers[i];
		if (!handler->baudrate) {
			continue;
		}

		rc = handler->baudrate(handler, console->tty.uart.baud);
		if (rc) {
			warnx("Can't set terminal baudrate for handler %s",
			      handler->name);
		}
	}
}

static int method_set_baud_rate(sd_bus_message *msg, void *userdata,
				sd_bus_error *err)
{
	struct console *console = userdata;
	uint32_t baudrate;
	speed_t speed;
	int r;

	if (!console) {
		sd_bus_error_set_const(err, DBUS_ERR, "Internal error");
		return sd_bus_reply_method_return(msg, "x", 0);
	}

	r = sd_bus_message_read(msg, "u", &baudrate);
	if (r < 0) {
		sd_bus_error_set_const(err, DBUS_ERR, "Bad message");
		return sd_bus_reply_method_return(msg, "x", -EINVAL);
	}

	speed = parse_int_to_baud(baudrate);
	if (!speed) {
		warnx("Invalid baud rate: '%u'", baudrate);
		return sd_bus_reply_method_return(msg, "x", -EINVAL);
	}

	assert(console->tty.type == TTY_DEVICE_UART);
	console->tty.uart.baud = speed;
	tty_change_baudrate(console);

	return sd_bus_reply_method_return(msg, "x", r);
}

static int get_handler(sd_bus *bus __attribute__((unused)),
		       const char *path __attribute__((unused)),
		       const char *interface __attribute__((unused)),
		       const char *property __attribute__((unused)),
		       sd_bus_message *reply, void *userdata,
		       sd_bus_error *error __attribute__((unused)))
{
	struct console *console = userdata;
	uint32_t baudrate;
	int r;

	assert(console->tty.type == TTY_DEVICE_UART);
	baudrate = parse_baud_to_int(console->tty.uart.baud);
	if (!baudrate) {
		warnx("Invalid baud rate: '%d'", console->tty.uart.baud);
	}

	r = sd_bus_message_append(reply, "u", baudrate);

	return r;
}

static int method_connect(sd_bus_message *msg, void *userdata,
			  sd_bus_error *err)
{
	struct console *console = userdata;
	int rc;
	int socket_fd = -1;

	if (!console) {
		warnx("Internal error: Console pointer is null");
		sd_bus_error_set_const(err, DBUS_ERR, "Internal error");
		return sd_bus_reply_method_error(msg, err);
	}

	/* Register the consumer. */
	socket_fd = dbus_create_socket_consumer(console);
	if (socket_fd < 0) {
		rc = socket_fd;
		warnx("Failed to create socket consumer: %s", strerror(rc));
		sd_bus_error_set_const(err, DBUS_ERR,
				       "Failed to create socket consumer");
		return sd_bus_reply_method_error(msg, err);
	}

	rc = sd_bus_reply_method_return(msg, "h", socket_fd);

	/* Close the our end */
	close(socket_fd);

	return rc;
}

static const sd_bus_vtable console_tty_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("setBaudRate", "u", "x", method_set_baud_rate,
		      SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("baudrate", "u", get_handler, 0, 0),
	SD_BUS_VTABLE_END,
};

static const sd_bus_vtable console_access_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Connect", SD_BUS_NO_ARGS, "h", method_connect,
		      SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

void dbus_init(struct console *console,
	       struct config *config __attribute__((unused)))
{
	char obj_name[dbus_obj_path_len];
	char dbus_name[dbus_obj_path_len];
	int dbus_poller = 0;
	int fd;
	int r;
	size_t bytes;

	if (!console) {
		warnx("Couldn't get valid console");
		return;
	}

	r = sd_bus_default_system(&console->bus);
	if (r < 0) {
		warnx("Failed to connect to system bus: %s", strerror(-r));
		return;
	}

	/* Register support console interface */
	bytes = snprintf(obj_name, dbus_obj_path_len, OBJ_NAME,
			 console->console_id);
	if (bytes >= dbus_obj_path_len) {
		warnx("Console id '%s' is too long. There is no enough space in the buffer.",
		      console->console_id);
		return;
	}

	if (console->tty.type == TTY_DEVICE_UART) {
		/* Register tty interface */
		r = sd_bus_add_object_vtable(console->bus, NULL, obj_name,
					     TTY_INTF, console_tty_vtable,
					     console);
		if (r < 0) {
			warnx("Failed to issue method call: %s", strerror(-r));
			return;
		}
	}

	/* Register access interface */
	r = sd_bus_add_object_vtable(console->bus, NULL, obj_name, ACCESS_INTF,
				     console_access_vtable, console);
	if (r < 0) {
		warnx("Failed to issue method call: %s", strerror(-r));
		return;
	}

	bytes = snprintf(dbus_name, dbus_obj_path_len, DBUS_NAME,
			 console->console_id);
	if (bytes >= dbus_obj_path_len) {
		warnx("Console id '%s' is too long. There is no enough space in the buffer.",
		      console->console_id);
		return;
	}

	/* Finally register the bus name */
	r = sd_bus_request_name(console->bus, dbus_name,
				SD_BUS_NAME_ALLOW_REPLACEMENT |
					SD_BUS_NAME_REPLACE_EXISTING);
	if (r < 0) {
		warnx("Failed to acquire service name: %s", strerror(-r));
		return;
	}

	fd = sd_bus_get_fd(console->bus);
	if (fd < 0) {
		warnx("Couldn't get the bus file descriptor");
		return;
	}

	dbus_poller = POLLFD_DBUS;

	console->pollfds[dbus_poller].fd = fd;
	console->pollfds[dbus_poller].events = POLLIN;
}
