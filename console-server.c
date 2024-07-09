/**
 * Console server process for OpenBMC
 *
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

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <getopt.h>
#include <glob.h>
#include <limits.h>
#include <time.h>
#include <termios.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <poll.h>

#include "console-server.h"
#include "config.h"

#define DEV_PTS_PATH "/dev/pts"

/* default size of the shared backlog ringbuffer */
const size_t default_buffer_size = 128ul * 1024ul;

/* state shared with the signal handler */
static volatile sig_atomic_t sigint;

static void usage(const char *progname)
{
	fprintf(stderr,
		"usage: %s [options] <DEVICE>\n"
		"\n"
		"Options:\n"
		"  --config <FILE>\tUse FILE for configuration\n"
		"  --console-id <NAME>\tUse NAME in the UNIX domain socket address\n"
		"",
		progname);
}

static bool console_server_pollfd_reclaimable(struct pollfd *p)
{
	return p->fd == -1 && p->events == 0 && p->revents == ~0;
}

static ssize_t
console_server_find_released_pollfd(struct console_server *server)
{
	for (size_t i = 0; i < server->capacity_pollfds; i++) {
		struct pollfd *p = &server->pollfds[i];
		if (console_server_pollfd_reclaimable(p)) {
			return (ssize_t)i;
		}
	}
	return -1;
}

// returns the index of that pollfd in server->pollfds
// we cannot return a pointer because 'realloc' may move server->pollfds
ssize_t console_server_request_pollfd(struct console_server *server, int fd,
				      short int events)
{
	ssize_t index;
	struct pollfd *pollfd;

	index = console_server_find_released_pollfd(server);

	if (index < 0) {
		const size_t newcap = server->capacity_pollfds + 1;

		struct pollfd *newarr = reallocarray(server->pollfds, newcap,
						     sizeof(struct pollfd));
		if (newarr == NULL) {
			return -1;
		}
		server->pollfds = newarr;

		index = (ssize_t)server->capacity_pollfds;

		server->capacity_pollfds = newcap;
	}

	pollfd = &server->pollfds[index];
	pollfd->fd = fd;
	pollfd->events = events;
	pollfd->revents = 0;

	return index;
}

int console_server_release_pollfd(struct console_server *server,
				  size_t pollfd_index)
{
	if (pollfd_index >= server->capacity_pollfds) {
		return -1;
	}

	struct pollfd *pfd = &server->pollfds[pollfd_index];

	// mark pollfd as reclaimable

	// ignore this file descriptor when calling 'poll'
	// https://www.man7.org/linux/man-pages/man2/poll.2.html
	pfd->fd = -1;
	pfd->events = 0;
	pfd->revents = ~0;

	return 0;
}

/* populates console->tty.dev and console->tty.sysfs_devnode, using the tty kernel name */
static int tty_find_device(struct console_server *server)
{
	char *tty_class_device_link = NULL;
	char *tty_path_input_real = NULL;
	char *tty_device_tty_dir = NULL;
	char *tty_vuart_lpc_addr = NULL;
	char *tty_device_reldir = NULL;
	char *tty_sysfs_devnode = NULL;
	char *tty_kname_real = NULL;
	char *tty_path_input = NULL;
	int rc;

	server->tty.type = TTY_DEVICE_UNDEFINED;

	assert(server->tty.kname);
	if (!strlen(server->tty.kname)) {
		warnx("TTY kname must not be empty");
		rc = -1;
		goto out_free;
	}

	if (server->tty.kname[0] == '/') {
		tty_path_input = strdup(server->tty.kname);
		if (!tty_path_input) {
			rc = -1;
			goto out_free;
		}
	} else {
		rc = asprintf(&tty_path_input, "/dev/%s", server->tty.kname);
		if (rc < 0) {
			goto out_free;
		}
	}

	/* udev may rename the tty name with a symbol link, try to resolve */
	tty_path_input_real = realpath(tty_path_input, NULL);
	if (!tty_path_input_real) {
		warn("Can't find realpath for %s", tty_path_input);
		rc = -1;
		goto out_free;
	}

	/*
	 * Allow hooking obmc-console-server up to PTYs for testing
	 *
	 * https://amboar.github.io/notes/2023/05/02/testing-obmc-console-with-socat.html
	 */
	if (!strncmp(DEV_PTS_PATH, tty_path_input_real, strlen(DEV_PTS_PATH))) {
		server->tty.type = TTY_DEVICE_PTY;
		server->tty.dev = strdup(server->tty.kname);
		rc = server->tty.dev ? 0 : -1;
		goto out_free;
	}

	tty_kname_real = basename(tty_path_input_real);
	if (!tty_kname_real) {
		warn("Can't find real name for %s", server->tty.kname);
		rc = -1;
		goto out_free;
	}

	rc = asprintf(&tty_class_device_link, "/sys/class/tty/%s",
		      tty_kname_real);
	if (rc < 0) {
		goto out_free;
	}

	tty_device_tty_dir = realpath(tty_class_device_link, NULL);
	if (!tty_device_tty_dir) {
		warn("Can't query sysfs for device %s", tty_kname_real);
		rc = -1;
		goto out_free;
	}

	rc = asprintf(&tty_device_reldir, "%s/../../", tty_device_tty_dir);
	if (rc < 0) {
		goto out_free;
	}

	tty_sysfs_devnode = realpath(tty_device_reldir, NULL);
	if (!tty_sysfs_devnode) {
		warn("Can't find parent device for %s", tty_kname_real);
	}

	rc = asprintf(&server->tty.dev, "/dev/%s", tty_kname_real);
	if (rc < 0) {
		goto out_free;
	}

	// Default to non-VUART
	server->tty.type = TTY_DEVICE_UART;

	/* Arbitrarily pick an attribute to differentiate UART vs VUART */
	if (tty_sysfs_devnode) {
		rc = asprintf(&tty_vuart_lpc_addr, "%s/lpc_address",
			      tty_sysfs_devnode);
		if (rc < 0) {
			goto out_free;
		}

		rc = access(tty_vuart_lpc_addr, F_OK);
		if (!rc) {
			server->tty.type = TTY_DEVICE_VUART;
			server->tty.vuart.sysfs_devnode =
				strdup(tty_sysfs_devnode);
		}
	}

	rc = 0;

out_free:
	free(tty_vuart_lpc_addr);
	free(tty_class_device_link);
	free(tty_sysfs_devnode);
	free(tty_device_tty_dir);
	free(tty_device_reldir);
	free(tty_path_input);
	free(tty_path_input_real);
	return rc;
}

static int tty_set_sysfs_attr(struct console_server *server, const char *name,
			      int value)
{
	char *path;
	FILE *fp;
	int rc;

	assert(server->tty.type == TTY_DEVICE_VUART);

	if (!server->tty.vuart.sysfs_devnode) {
		return -1;
	}

	rc = asprintf(&path, "%s/%s", server->tty.vuart.sysfs_devnode, name);
	if (rc < 0) {
		return -1;
	}

	fp = fopen(path, "w");
	if (!fp) {
		warn("Can't access attribute %s on device %s", name,
		     server->tty.kname);
		rc = -1;
		goto out_free;
	}
	setvbuf(fp, NULL, _IONBF, 0);

	rc = fprintf(fp, "0x%x", value);
	if (rc < 0) {
		warn("Error writing to %s attribute of device %s", name,
		     server->tty.kname);
	}
	fclose(fp);

out_free:
	free(path);
	return rc;
}

/**
 * Set termios attributes on the console tty.
 */
void tty_init_termios(struct console_server *server)
{
	struct termios termios;
	int rc;

	rc = tcgetattr(server->tty.fd, &termios);
	if (rc) {
		warn("Can't read tty termios");
		return;
	}

	if (server->tty.type == TTY_DEVICE_UART && server->tty.uart.baud) {
		if (cfsetspeed(&termios, server->tty.uart.baud) < 0) {
			warn("Couldn't set speeds for %s", server->tty.kname);
		}
	}

	/* Set console to raw mode: we don't want any processing to occur on
	 * the underlying terminal input/output.
	 */
	cfmakeraw(&termios);

	rc = tcsetattr(server->tty.fd, TCSANOW, &termios);
	if (rc) {
		warn("Can't set terminal options for %s", server->tty.kname);
	}
}

/**
 * Open and initialise the serial device
 */
static void tty_init_vuart_io(struct console_server *server)
{
	assert(server->tty.type == TTY_DEVICE_VUART);

	if (server->tty.vuart.sirq) {
		tty_set_sysfs_attr(server, "sirq", server->tty.vuart.sirq);
	}

	if (server->tty.vuart.lpc_addr) {
		tty_set_sysfs_attr(server, "lpc_address",
				   server->tty.vuart.lpc_addr);
	}
}

static int tty_init_io(struct console_server *server)
{
	server->tty.fd = open(server->tty.dev, O_RDWR);
	if (server->tty.fd <= 0) {
		warn("Can't open tty %s", server->tty.dev);
		return -1;
	}

	/* Disable character delay. We may want to later enable this when
	 * we detect larger amounts of data
	 */
	fcntl(server->tty.fd, F_SETFL, FNDELAY);

	tty_init_termios(server);

	ssize_t index =
		console_server_request_pollfd(server, server->tty.fd, POLLIN);

	if (index < 0) {
		return -1;
	}

	server->tty_pollfd_index = (size_t)index;

	return 0;
}

static int tty_init_vuart(struct console_server *server, struct config *config)
{
	unsigned long parsed;
	const char *val;
	char *endp;

	assert(server->tty.type == TTY_DEVICE_VUART);

	val = config_get_value(config, "lpc-address");
	if (val) {
		errno = 0;
		parsed = strtoul(val, &endp, 0);
		if (parsed == ULONG_MAX && errno == ERANGE) {
			warn("Cannot interpret 'lpc-address' value as an unsigned long: '%s'",
			     val);
			return -1;
		}

		if (parsed > UINT16_MAX) {
			warn("Invalid LPC address '%s'", val);
			return -1;
		}

		server->tty.vuart.lpc_addr = (uint16_t)parsed;
		if (endp == optarg) {
			warn("Invalid LPC address: '%s'", val);
			return -1;
		}
	}

	val = config_get_value(config, "sirq");
	if (val) {
		errno = 0;
		parsed = strtoul(val, &endp, 0);
		if (parsed == ULONG_MAX && errno == ERANGE) {
			warn("Cannot interpret 'sirq' value as an unsigned long: '%s'",
			     val);
		}

		if (parsed > 16) {
			warn("Invalid LPC SERIRQ: '%s'", val);
		}

		server->tty.vuart.sirq = (int)parsed;
		if (endp == optarg) {
			warn("Invalid sirq: '%s'", val);
		}
	}

	return 0;
}

static int tty_init(struct console_server *server, struct config *config,
		    const char *tty_arg)
{
	const char *val;
	int rc;

	if (tty_arg) {
		server->tty.kname = tty_arg;
	} else if ((val = config_get_value(config, "upstream-tty"))) {
		server->tty.kname = val;
	} else {
		warnx("Error: No TTY device specified");
		return -1;
	}

	rc = tty_find_device(server);
	if (rc) {
		return rc;
	}

	switch (server->tty.type) {
	case TTY_DEVICE_VUART:
		rc = tty_init_vuart(server, config);
		if (rc) {
			return rc;
		}

		tty_init_vuart_io(server);
		break;
	case TTY_DEVICE_UART:
		val = config_get_value(config, "baud");
		if (val) {
			if (config_parse_baud(&server->tty.uart.baud, val)) {
				warnx("Invalid baud rate: '%s'", val);
			}
		}
		break;
	case TTY_DEVICE_PTY:
		break;
	case TTY_DEVICE_UNDEFINED:
	default:
		warnx("Cannot configure unrecognised TTY device");
		return -1;
	}

	return tty_init_io(server);
}

static void tty_fini(struct console_server *server)
{
	if (server->tty.type == TTY_DEVICE_VUART) {
		free(server->tty.vuart.sysfs_devnode);
	}
	free(server->tty.dev);
}

static int write_to_path(const char *path, const char *data)
{
	int rc = 0;
	FILE *f = fopen(path, "w");
	if (!f) {
		return -1;
	}

	if (fprintf(f, "%s", data) < 0) {
		rc = -1;
	}

	if (fclose(f)) {
		rc = -1;
	}

	return rc;
}

#define ASPEED_UART_ROUTING_PATTERN                                            \
	"/sys/bus/platform/drivers/aspeed-uart-routing/*.uart-routing"

static void uart_routing_init(struct config *config)
{
	const char *muxcfg;
	const char *p;
	size_t buflen;
	char *sink;
	char *source;
	char *muxdir;
	char *path;
	glob_t globbuf;

	muxcfg = config_get_value(config, "aspeed-uart-routing");
	if (!muxcfg) {
		return;
	}

	/* Find the driver's sysfs directory */
	if (glob(ASPEED_UART_ROUTING_PATTERN, GLOB_ERR | GLOB_NOSORT, NULL,
		 &globbuf) != 0) {
		warn("Couldn't find uart-routing driver directory, cannot apply config");
		return;
	}
	if (globbuf.gl_pathc != 1) {
		warnx("Found %zd uart-routing driver directories, cannot apply config",
		      globbuf.gl_pathc);
		goto out_free_glob;
	}
	muxdir = globbuf.gl_pathv[0];

	/*
	 * Rather than faff about tracking a bunch of separate buffer sizes,
	 * just use one (worst-case) size for all of them -- +2 for a trailing
	 * NUL and a '/' separator to construct the sysfs file path.
	 */
	buflen = strlen(muxdir) + strlen(muxcfg) + 2;

	sink = malloc(buflen);
	source = malloc(buflen);
	path = malloc(buflen);
	if (!path || !sink || !source) {
		warnx("Out of memory applying uart routing config");
		goto out_free_bufs;
	}

	p = muxcfg;
	while (*p) {
		ssize_t bytes_scanned;

		if (sscanf(p, " %[^:/ \t]:%[^: \t] %zn", sink, source,
			   &bytes_scanned) != 2) {
			warnx("Invalid syntax in aspeed uart config: '%s' not applied",
			      p);
			break;
		}
		p += bytes_scanned;

		/*
		 * Check that the sink name looks reasonable before proceeding
		 * (there are other writable files in the same directory that
		 * we shouldn't be touching, such as 'driver_override' and
		 * 'uevent').
		 */
		if (strncmp(sink, "io", strlen("io")) != 0 &&
		    strncmp(sink, "uart", strlen("uart")) != 0) {
			warnx("Skipping invalid uart routing name '%s' (must be ioN or uartN)",
			      sink);
			continue;
		}

		snprintf(path, buflen, "%s/%s", muxdir, sink);
		if (write_to_path(path, source)) {
			warn("Failed to apply uart-routing config '%s:%s'",
			     sink, source);
		}
	}

out_free_bufs:
	free(path);
	free(source);
	free(sink);
out_free_glob:
	globfree(&globbuf);
}

int console_data_out(struct console *console, const uint8_t *data, size_t len)
{
	return write_buf_to_fd(console->server->tty.fd, data, len);
}

/* Prepare a socket name */
static int set_socket_info(struct console *console, struct config *config,
			   const char *console_id)
{
	ssize_t len;

	/* Get console id */
	console->console_id = config_resolve_console_id(config, console_id);

	/* Get the socket name/path */
	len = console_socket_path(console->socket_name, console->console_id);
	if (len < 0) {
		warn("Failed to set socket path: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	/* Socket name is not a null terminated string hence save the length */
	console->socket_name_len = len;

	return 0;
}

static void handlers_init(struct console *console, struct config *config)
{
	/* NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
	extern const struct handler_type *const __start_handlers;
	extern const struct handler_type *const __stop_handlers;
	/* NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
	size_t n_types;
	int j = 0;
	size_t i;

	n_types = &__stop_handlers - &__start_handlers;
	console->handlers = calloc(n_types, sizeof(struct handler *));
	if (!console->handlers) {
		err(EXIT_FAILURE, "malloc(handlers)");
	}

	printf("%zu handler type%s\n", n_types, n_types == 1 ? "" : "s");

	for (i = 0; i < n_types; i++) {
		const struct handler_type *type = &__start_handlers[i];
		struct handler *handler;

		/* Should be picked up at build time by
		 * console_handler_register, but check anyway
		 */
		if (!type->init || !type->fini) {
			errx(EXIT_FAILURE,
			     "invalid handler type %s: no init() / fini()",
			     type->name);
		}

		handler = type->init(type, console, config);

		printf("  console '%s': handler %s [%sactive]\n",
		       console->console_id, type->name, handler ? "" : "in");

		if (handler) {
			handler->type = type;
			console->handlers[j++] = handler;
		}
	}

	console->n_handlers = j;
}

static void handlers_fini(struct console *console)
{
	struct handler *handler;
	int i;

	for (i = 0; i < console->n_handlers; i++) {
		handler = console->handlers[i];
		handler->type->fini(handler);
	}

	free(console->handlers);
	console->handlers = NULL;
	console->n_handlers = 0;
}

static int get_current_time(struct timeval *tv)
{
	struct timespec t;
	int rc;

	/*
	 * We use clock_gettime(CLOCK_MONOTONIC) so we're immune to
	 * local time changes. However, a struct timeval is more
	 * convenient for calculations, so convert to that.
	 */
	rc = clock_gettime(CLOCK_MONOTONIC, &t);
	if (rc) {
		return rc;
	}

	tv->tv_sec = t.tv_sec;
	tv->tv_usec = t.tv_nsec / 1000;

	return 0;
}

struct ringbuffer_consumer *
console_ringbuffer_consumer_register(struct console *console,
				     ringbuffer_poll_fn_t poll_fn, void *data)
{
	return ringbuffer_consumer_register(console->rb, poll_fn, data);
}

struct poller *console_poller_register(struct console *console,
				       struct handler *handler,
				       poller_event_fn_t poller_fn,
				       poller_timeout_fn_t timeout_fn, int fd,
				       int events, void *data)
{
	struct poller *poller;
	long n;

	poller = malloc(sizeof(*poller));
	poller->remove = false;
	poller->handler = handler;
	poller->event_fn = poller_fn;
	poller->timeout_fn = timeout_fn;
	timerclear(&poller->timeout);
	poller->data = data;

	/* add one to our pollers array */
	n = console->n_pollers++;
	/*
	 * We're managing an array of pointers to aggregates, so don't warn about sizeof() on a
	 * pointer type.
	 */
	/* NOLINTBEGIN(bugprone-sizeof-expression) */
	console->pollers = reallocarray(console->pollers, console->n_pollers,
					sizeof(*console->pollers));
	/* NOLINTEND(bugprone-sizeof-expression) */

	console->pollers[n] = poller;

	ssize_t index = console_server_request_pollfd(console->server, fd,
						      (short)(events & 0x7fff));
	if (index < 0) {
		fprintf(stderr, "Error requesting pollfd\n");
		free(poller);
		return NULL;
	}
	poller->pollfd_index = index;

	return poller;
}

void console_poller_unregister(struct console *console, struct poller *poller)
{
	int i;

	/* find the entry in our pollers array */
	for (i = 0; i < console->n_pollers; i++) {
		if (console->pollers[i] == poller) {
			break;
		}
	}

	assert(i < console->n_pollers);

	console->n_pollers--;

	/*
	 * Remove the item from the pollers array...
	 *
	 * We're managing an array of pointers to aggregates, so don't warn about sizeof() on a
	 * pointer type.
	 */
	/* NOLINTBEGIN(bugprone-sizeof-expression) */
	memmove(&console->pollers[i], &console->pollers[i + 1],
		sizeof(*console->pollers) * (console->n_pollers - i));

	if (console->n_pollers == 0) {
		free(console->pollers);
		console->pollers = NULL;
	} else {
		console->pollers = reallocarray(console->pollers,
						console->n_pollers,
						sizeof(*console->pollers));
	}
	/* NOLINTEND(bugprone-sizeof-expression) */

	console_server_release_pollfd(console->server, poller->pollfd_index);

	free(poller);
}

void console_poller_set_events(struct console *console, struct poller *poller,
			       int events)
{
	console->server->pollfds[poller->pollfd_index].events =
		(short)(events & 0x7fff);
}

void console_poller_set_timeout(struct console *console __attribute__((unused)),
				struct poller *poller, const struct timeval *tv)
{
	struct timeval now;
	int rc;

	rc = get_current_time(&now);
	if (rc) {
		return;
	}

	timeradd(&now, tv, &poller->timeout);
}

static long get_poll_timeout(struct console *console, struct timeval *cur_time)
{
	struct timeval *earliest;
	struct timeval interval;
	struct poller *poller;
	int i;

	earliest = NULL;

	for (i = 0; i < console->n_pollers; i++) {
		poller = console->pollers[i];

		if (poller->timeout_fn && timerisset(&poller->timeout) &&
		    (!earliest ||
		     (earliest && timercmp(&poller->timeout, earliest, <)))) {
			// poller is buffering data and needs the poll
			// function to timeout.
			earliest = &poller->timeout;
		}
	}

	if (earliest) {
		if (timercmp(earliest, cur_time, >)) {
			/* recalculate the timeout period, time period has
			 * not elapsed */
			timersub(earliest, cur_time, &interval);
			return ((interval.tv_sec * 1000) +
				(interval.tv_usec / 1000));
		} /* return from poll immediately */
		return 0;

	} /* poll indefinitely */
	return -1;
}

static int call_pollers(struct console *console, struct timeval *cur_time)
{
	struct poller *poller;
	struct pollfd *pollfd;
	enum poller_ret prc;
	int i;
	int rc;

	rc = 0;

	/*
	 * Process poll events by iterating through the pollers and pollfds
	 * in-step, calling any pollers that we've found revents for.
	 */
	for (i = 0; i < console->n_pollers; i++) {
		poller = console->pollers[i];
		pollfd = &console->server->pollfds[poller->pollfd_index];
		if (pollfd->fd < 0) {
			// pollfd has already been released
			continue;
		}

		prc = POLLER_OK;

		/* process pending events... */
		if (pollfd->revents) {
			prc = poller->event_fn(poller->handler, pollfd->revents,
					       poller->data);
			if (prc == POLLER_EXIT) {
				rc = -1;
			} else if (prc == POLLER_REMOVE) {
				poller->remove = true;
			}
		}

		if ((prc == POLLER_OK) && poller->timeout_fn &&
		    timerisset(&poller->timeout) &&
		    timercmp(&poller->timeout, cur_time, <=)) {
			/* One of the ringbuffer consumers is buffering the
			data stream. The amount of idle time the consumer
			desired has expired.  Process the buffered data for
			transmission. */
			timerclear(&poller->timeout);
			prc = poller->timeout_fn(poller->handler, poller->data);
			if (prc == POLLER_EXIT) {
				rc = -1;
			} else if (prc == POLLER_REMOVE) {
				poller->remove = true;
			}
		}
	}

	/**
	 * Process deferred removals; restarting each time we unregister, as
	 * the array will have changed
	 */
	for (;;) {
		bool removed = false;

		for (i = 0; i < console->n_pollers; i++) {
			poller = console->pollers[i];
			if (poller->remove) {
				console_poller_unregister(console, poller);
				removed = true;
				break;
			}
		}
		if (!removed) {
			break;
		}
	}

	return rc;
}

static void sighandler(int signal)
{
	if (signal == SIGINT) {
		sigint = 1;
	}
}

static int run_console_iteration(struct console *console)
{
	uint8_t buf[4096];
	struct timeval tv;
	long timeout;
	ssize_t rc;

	if (console->rb->size < sizeof(buf)) {
		fprintf(stderr, "Ringbuffer size should be greater than %zuB\n",
			sizeof(buf));
		return -1;
	}

	if (sigint) {
		fprintf(stderr, "Received interrupt, exiting\n");
		return -1;
	}

	rc = get_current_time(&tv);
	if (rc) {
		warn("Failed to read current time");
		return -1;
	}

	timeout = get_poll_timeout(console, &tv);

	rc = poll(console->server->pollfds, console->server->capacity_pollfds,
		  (int)timeout);

	if (rc < 0) {
		if (errno == EINTR) {
			return 0;
		}
		warn("poll error");
		return -1;
	}

	/* process internal fd first */
	if (console->server->pollfds[console->server->tty_pollfd_index].revents) {
		rc = read(console->server->tty.fd, buf, sizeof(buf));
		if (rc <= 0) {
			warn("Error reading from tty device");
			return -1;
		}
		rc = ringbuffer_queue(console->rb, buf, rc);
		if (rc) {
			return -1;
		}
	}

	if (console->server->pollfds[console->dbus_pollfd_index].revents) {
		sd_bus_process(console->bus, NULL);
	}

	/* ... and then the pollers */
	rc = call_pollers(console, &tv);
	if (rc) {
		return -1;
	}
	return 0;
}

int run_console(struct console *console)
{
	sighandler_t sighandler_save = signal(SIGINT, sighandler);
	ssize_t rc = 0;

	for (;;) {
		rc = run_console_iteration(console);
		if (rc) {
			break;
		}
	}

	signal(SIGINT, sighandler_save);
	sd_bus_unref(console->bus);

	return rc ? -1 : 0;
}
static const struct option options[] = {
	{ "config", required_argument, 0, 'c' },
	{ "console-id", required_argument, 0, 'i' },
	{ 0, 0, 0, 0 },
};

int main(int argc, char **argv)
{
	size_t buffer_size = default_buffer_size;
	const char *config_filename = NULL;
	const char *config_tty_kname = NULL;
	const char *buffer_size_str = NULL;
	const char *console_id = NULL;
	struct console_server server;
	struct console *console;
	struct config *config;
	int rc;

	for (;;) {
		int c;
		int idx;

		c = getopt_long(argc, argv, "c:i:", options, &idx);
		if (c == -1) {
			break;
		}

		switch (c) {
		case 'c':
			config_filename = optarg;
			break;
		case 'i':
			console_id = optarg;
			break;
		case 'h':
		case '?':
			usage(argv[0]);
			return EXIT_SUCCESS;
		}
	}

	if (optind < argc) {
		config_tty_kname = argv[optind];
	}

	config = config_init(config_filename);

	memset(&server, 0, sizeof(struct console_server));

	console = malloc(sizeof(struct console));
	memset(console, 0, sizeof(*console));

	server.active_console = console;
	console->server = &server;

	server.pollfds = NULL;
	server.capacity_pollfds = 0;

	buffer_size_str = config_get_value(config, "ringbuffer-size");
	if (buffer_size_str) {
		rc = config_parse_bytesize(buffer_size_str, &buffer_size);
		if (rc) {
			warn("Invalid ringbuffer-size. Default to %zukB",
			     buffer_size >> 10);
		}
	}
	console->rb = ringbuffer_init(buffer_size);
	if (!console->rb) {
		rc = -1;
		goto out_config_fini;
	}

	if (set_socket_info(console, config, console_id)) {
		rc = -1;
		goto out_ringbuffer_fini;
	}

	uart_routing_init(config);

	rc = tty_init(&server, config, config_tty_kname);
	if (rc) {
		goto out_ringbuffer_fini;
	}

	rc = dbus_init(console, config);
	if (rc) {
		goto out_tty_fini;
	}

	handlers_init(console, config);

	rc = run_console(console);

	handlers_fini(console);

out_tty_fini:
	tty_fini(&server);

out_ringbuffer_fini:
	ringbuffer_fini(console->rb);

out_config_fini:
	config_fini(config);

	free(console->pollers);
	free(server.pollfds);
	free(console);

	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
