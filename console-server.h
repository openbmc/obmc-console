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

#pragma once

#include <bits/pthreadtypes.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <termios.h> /* for speed_t */
#include <time.h>
#include <systemd/sd-bus.h>
#include <sys/time.h>
#include <sys/un.h>

struct console;
struct config;

/* Handler API.
 *
 * Console data handlers: these implement the functions that process
 * data coming out of the main tty device.
 *
 * Handlers are registered at link time using the console_handler_register()
 * macro. We call each handler's ->init() function at startup, and ->fini() at
 * exit.
 *
 * Handlers will almost always want to register a ringbuffer consumer, which
 * provides data coming from the tty. Use cosole_register_ringbuffer_consumer()
 * for this. To send data to the tty, use console_data_out().
 *
 * If a handler needs to monitor a separate file descriptor for events, use the
 * poller API, through console_poller_register().
 */
struct handler {
	const char *name;
	int (*init)(struct handler *handler, struct console *console,
		    struct config *config);
	void (*fini)(struct handler *handler);
	int (*baudrate)(struct handler *handler, speed_t baudrate);
	bool active;
	void *data; // private data of the handler
};

int console_data_out(struct console *console, const uint8_t *data, size_t len);

enum poller_ret {
	POLLER_OK = 0,
	POLLER_REMOVE,
	POLLER_EXIT,
};

typedef char(socket_path_t)[sizeof(((struct sockaddr_un *)NULL)->sun_path)];

typedef enum poller_ret (*poller_event_fn_t)(struct handler *handler,
					     int revents, void *data);
typedef enum poller_ret (*poller_timeout_fn_t)(struct handler *handler,
					       void *data);

enum tty_device {
	TTY_DEVICE_UNDEFINED = 0,
	TTY_DEVICE_VUART,
	TTY_DEVICE_UART,
	TTY_DEVICE_PTY,
};

struct console_server_args {
	// may be NULL when using config-file
	char *console_id;

	char *config_tty_kname;

	char *config_filename;
};

struct console_server {
	struct {
		const char *kname;
		char *dev;
		int fd;
		enum tty_device type;
		union {
			struct {
				char *sysfs_devnode;
				int sirq;
				uint16_t lpc_addr;
			} vuart;
			struct {
				speed_t baud;
			} uart;
		};
	} tty;

	// All the pollfds are stored here,
	// so 'poll' can operate on them.
	// The other 'pollfd*' are just pointers to this array.
	struct pollfd *pollfds;
	size_t capacity_pollfds;

	// index into pollfds
	size_t tty_pollfd_index;

	struct config *config;

	struct console *active_console;
	struct console **consoles;
	size_t n_consoles;

	struct gpiod_chip *gpio_chip;
};

/* Console server structure */
struct console {
	// point back to the console server which
	// we are a member of
	struct console_server *server;

	const char *console_id;

	/* Socket name starts with null character hence we need length */
	socket_path_t socket_name;
	ssize_t socket_name_len;

	struct ringbuffer *rb;

	struct handler **handlers;
	long n_handlers;

	struct poller **pollers;
	long n_pollers;

	// index into (struct console_server)->pollfds
	size_t dbus_pollfd_index;

	struct sd_bus *bus;

	struct console_gpio **mux_gpios;
	long n_mux_gpios;
};

/* poller API */
struct poller {
	struct handler *handler;
	void *data;
	poller_event_fn_t event_fn;
	poller_timeout_fn_t timeout_fn;
	struct timeval timeout;
	bool remove;

	// index into (struct console_server)->pollfds
	size_t pollfd_index;
};

struct poller *console_poller_register(struct console *console,
				       struct handler *handler,
				       poller_event_fn_t poller_fn,
				       poller_timeout_fn_t timeout_fn, int fd,
				       int events, void *data);

void console_poller_unregister(struct console *console, struct poller *poller);

void console_poller_set_events(struct console *console, struct poller *poller,
			       int events);

void console_poller_set_timeout(struct console *console, struct poller *poller,
				const struct timeval *tv);

/* ringbuffer API */

enum ringbuffer_poll_ret {
	RINGBUFFER_POLL_OK = 0,
	RINGBUFFER_POLL_REMOVE,
};

typedef enum ringbuffer_poll_ret (*ringbuffer_poll_fn_t)(void *data,
							 size_t force_len);

struct ringbuffer_consumer;

struct ringbuffer {
	uint8_t *buf;
	size_t size;
	size_t tail;
	struct ringbuffer_consumer **consumers;
	int n_consumers;
};

struct ringbuffer_consumer {
	struct ringbuffer *rb;
	ringbuffer_poll_fn_t poll_fn;
	void *poll_data;
	size_t pos;
};

struct ringbuffer *ringbuffer_init(size_t size);
void ringbuffer_fini(struct ringbuffer *rb);

struct ringbuffer_consumer *
ringbuffer_consumer_register(struct ringbuffer *rb,
			     ringbuffer_poll_fn_t poll_fn, void *data);

void ringbuffer_consumer_unregister(struct ringbuffer_consumer *rbc);

int ringbuffer_queue(struct ringbuffer *rb, uint8_t *data, size_t len);

size_t ringbuffer_dequeue_peek(struct ringbuffer_consumer *rbc, size_t offset,
			       uint8_t **data);

int ringbuffer_dequeue_commit(struct ringbuffer_consumer *rbc, size_t len);

size_t ringbuffer_len(struct ringbuffer_consumer *rbc);

/* console wrapper around ringbuffer consumer registration */
struct ringbuffer_consumer *
console_ringbuffer_consumer_register(struct console *console,
				     ringbuffer_poll_fn_t poll_fn, void *data);

/* Console server API */
void tty_init_termios(struct console_server *server);

/* config API */
struct config;

/* socket paths */
ssize_t console_socket_path(socket_path_t path, const char *id);
ssize_t console_socket_path_readable(const struct sockaddr_un *addr,
				     size_t addrlen, socket_path_t path);

/* utils */
int write_buf_to_fd(int fd, const uint8_t *buf, size_t len);

/* console-dbus API */
int dbus_init(struct console *console,
	      struct config *config __attribute__((unused)));

/* socket-handler API */
int dbus_create_socket_consumer(struct console *console);

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifndef offsetof
#define offsetof(type, member) ((unsigned long)&((type *)NULL)->member)
#endif

#define container_of(ptr, type, member)                                        \
	((type *)((void *)((ptr)-offsetof(type, member))))

#define BUILD_ASSERT(c)                                                        \
	do {                                                                   \
		char __c[(c) ? 1 : -1] __attribute__((unused));                \
	} while (0)

// returns the index of that pollfd in server->pollfds
// we cannot return a pointer because 'realloc' may move server->pollfds
ssize_t console_server_request_pollfd(struct console_server *server, int fd,
				      short int events);

int console_server_release_pollfd(struct console_server *server,
				  size_t pollfd_index);

int console_server_args_init(int argc, char **argv,
			     struct console_server_args *args);
void console_server_args_fini(struct console_server_args *args);
int console_server_main(int argc, char **argv);
