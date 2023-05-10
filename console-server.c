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
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include "console-server.h"

/* size of the shared backlog ringbuffer */
const size_t buffer_size = 128ul * 1024ul;

/* state shared with the signal handler */
static bool sigint;

static void usage(const char *progname)
{
	fprintf(stderr,
		"usage: %s [options] <DEVICE>\n"
		"\n"
		"Options:\n"
		"  --config <FILE>  Use FILE for configuration\n"
		"",
		progname);
}

/* populates tty_dev and tty_sysfs_devnode, using the tty kernel name */
static int tty_find_device(struct console *console)
{
	char *tty_class_device_link = NULL;
	char *tty_path_input_real = NULL;
	char *tty_device_tty_dir = NULL;
	char *tty_device_reldir = NULL;
	char *tty_path_input = NULL;
	char *tty_kname_real = NULL;
	int rc;

	/* udev may rename the tty name with a symbol link, try to resolve */
	rc = asprintf(&tty_path_input, "/dev/%s", console->tty_kname);
	if (rc < 0) {
		return -1;
	}

	tty_path_input_real = realpath(tty_path_input, NULL);
	if (!tty_path_input_real) {
		warn("Can't find realpath for /dev/%s", console->tty_kname);
		rc = -1;
		goto out_free;
	}

	tty_kname_real = basename(tty_path_input_real);
	if (!tty_kname_real) {
		warn("Can't find real name for /dev/%s", console->tty_kname);
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

	console->tty_sysfs_devnode = realpath(tty_device_reldir, NULL);
	if (!console->tty_sysfs_devnode) {
		warn("Can't find parent device for %s", tty_kname_real);
	}

	rc = asprintf(&console->tty_dev, "/dev/%s", tty_kname_real);
	if (rc < 0) {
		goto out_free;
	}

	rc = 0;

out_free:
	free(tty_class_device_link);
	free(tty_device_tty_dir);
	free(tty_device_reldir);
	free(tty_path_input);
	free(tty_path_input_real);
	return rc;
}

static int tty_set_sysfs_attr(struct console *console, const char *name,
			      int value)
{
	char *path;
	FILE *fp;
	int rc;

	rc = asprintf(&path, "%s/%s", console->tty_sysfs_devnode, name);
	if (rc < 0) {
		return -1;
	}

	fp = fopen(path, "w");
	if (!fp) {
		warn("Can't access attribute %s on device %s", name,
		     console->tty_kname);
		rc = -1;
		goto out_free;
	}
	setvbuf(fp, NULL, _IONBF, 0);

	rc = fprintf(fp, "0x%x", value);
	if (rc < 0) {
		warn("Error writing to %s attribute of device %s", name,
		     console->tty_kname);
	}
	fclose(fp);

out_free:
	free(path);
	return rc;
}

/**
 * Set termios attributes on the console tty.
 */
void tty_init_termios(struct console *console)
{
	struct termios termios;
	int rc;

	rc = tcgetattr(console->tty_fd, &termios);
	if (rc) {
		warn("Can't read tty termios");
		return;
	}

	if (console->tty_baud) {
		if (cfsetspeed(&termios, console->tty_baud) < 0) {
			warn("Couldn't set speeds for %s", console->tty_kname);
		}
	}

	/* Set console to raw mode: we don't want any processing to occur on
	 * the underlying terminal input/output.
	 */
	cfmakeraw(&termios);

	rc = tcsetattr(console->tty_fd, TCSANOW, &termios);
	if (rc) {
		warn("Can't set terminal options for %s", console->tty_kname);
	}
}

/**
 * Open and initialise the serial device
 */
static int tty_init_io(struct console *console)
{
	if (console->tty_sirq) {
		tty_set_sysfs_attr(console, "sirq", console->tty_sirq);
	}
	if (console->tty_lpc_addr) {
		tty_set_sysfs_attr(console, "lpc_address",
				   console->tty_lpc_addr);
	}

	console->tty_fd = open(console->tty_dev, O_RDWR);
	if (console->tty_fd <= 0) {
		warn("Can't open tty %s", console->tty_dev);
		return -1;
	}

	/* Disable character delay. We may want to later enable this when
	 * we detect larger amounts of data
	 */
	fcntl(console->tty_fd, F_SETFL, FNDELAY);

	tty_init_termios(console);

	console->pollfds[console->n_pollers].fd = console->tty_fd;
	console->pollfds[console->n_pollers].events = POLLIN;

	return 0;
}

static int tty_init(struct console *console, struct config *config)
{
	unsigned long parsed;
	const char *val;
	char *endp;
	int rc;

	val = config_get_value(config, "lpc-address");
	if (val) {
		errno = 0;
		parsed = strtoul(val, &endp, 0);
		if (parsed == ULONG_MAX && errno == ERANGE) {
			warn("Cannot interpret 'lpc-address' value as an "
			     "unsigned long: '%s'",
			     val);
			return -1;
		}

		if (parsed > UINT16_MAX) {
			warn("Invalid LPC address '%s'", val);
			return -1;
		}

		console->tty_lpc_addr = (uint16_t)parsed;
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
			warn("Cannot interpret 'sirq' value as an unsigned "
			     "long: '%s'",
			     val);
		}

		if (parsed > 16) {
			warn("Invalid LPC SERIRQ: '%s'", val);
		}

		console->tty_sirq = (int)parsed;
		if (endp == optarg) {
			warn("Invalid sirq: '%s'", val);
		}
	}

	val = config_get_value(config, "baud");
	if (val) {
		if (config_parse_baud(&console->tty_baud, val)) {
			warnx("Invalid baud rate: '%s'", val);
		}
	}

	if (!console->tty_kname) {
		warnx("Error: No TTY device specified");
		return -1;
	}

	rc = tty_find_device(console);
	if (rc) {
		return rc;
	}

	rc = tty_init_io(console);
	return rc;
}

int console_data_out(struct console *console, const uint8_t *data, size_t len)
{
	return write_buf_to_fd(console->tty_fd, data, len);
}

/* Read console if from config and prepare a socket name */
static int set_socket_info(struct console *console, struct config *config)
{
	ssize_t len;

	console->console_id = config_get_value(config, "socket-id");
	if (!console->console_id) {
		warnx("Error: The socket-id is not set in the config file");
		return EXIT_FAILURE;
	}

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
	/* NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
	 */
	extern struct handler *__start_handlers;
	extern struct handler *__stop_handlers;
	/* NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
	 */
	struct handler *handler;
	int i;
	int rc;

	console->n_handlers = &__stop_handlers - &__start_handlers;
	console->handlers = &__start_handlers;

	printf("%ld handler%s\n", console->n_handlers,
	       console->n_handlers == 1 ? "" : "s");

	for (i = 0; i < console->n_handlers; i++) {
		handler = console->handlers[i];

		rc = 0;
		if (handler->init) {
			rc = handler->init(handler, console, config);
		}

		handler->active = rc == 0;

		printf("  %s [%sactive]\n", handler->name,
		       handler->active ? "" : "in");
	}
}

static void handlers_fini(struct console *console)
{
	struct handler *handler;
	int i;

	for (i = 0; i < console->n_handlers; i++) {
		handler = console->handlers[i];
		if (handler->fini && handler->active) {
			handler->fini(handler);
		}
	}
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
	poller->data = data;

	/* add one to our pollers array */
	n = console->n_pollers++;
	/*
	 * We're managing an array of pointers to aggregates, so don't warn
	 * about sizeof() on a pointer type.
	 */
	/* NOLINTBEGIN(bugprone-sizeof-expression) */
	console->pollers = reallocarray(console->pollers, console->n_pollers,
					sizeof(*console->pollers));
	/* NOLINTEND(bugprone-sizeof-expression) */

	console->pollers[n] = poller;

	/* increase pollfds array too  */
	console->pollfds = reallocarray(
	    console->pollfds, (MAX_INTERNAL_POLLFD + console->n_pollers),
	    sizeof(*console->pollfds));

	/* shift the end pollfds up by one */
	memcpy(&console->pollfds[n + 1], &console->pollfds[n],
	       sizeof(*console->pollfds) * MAX_INTERNAL_POLLFD);

	console->pollfds[n].fd = fd;
	console->pollfds[n].events = (short)(events & 0x7fff);

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
	 * We're managing an array of pointers to aggregates, so don't warn
	 * about sizeof() on a pointer type.
	 */
	/* NOLINTBEGIN(bugprone-sizeof-expression) */
	memmove(&console->pollers[i], &console->pollers[i + 1],
		sizeof(*console->pollers) * (console->n_pollers - i));

	console->pollers = reallocarray(console->pollers, console->n_pollers,
					sizeof(*console->pollers));
	/* NOLINTEND(bugprone-sizeof-expression) */

	/* ... and the pollfds array */
	memmove(&console->pollfds[i], &console->pollfds[i + 1],
		sizeof(*console->pollfds) *
		    (MAX_INTERNAL_POLLFD + console->n_pollers - i));

	console->pollfds = reallocarray(
	    console->pollfds, (MAX_INTERNAL_POLLFD + console->n_pollers),
	    sizeof(*console->pollfds));

	free(poller);
}

void console_poller_set_events(struct console *console, struct poller *poller,
			       int events)
{
	int i;

	/* find the entry in our pollers array */
	for (i = 0; i < console->n_pollers; i++) {
		if (console->pollers[i] == poller) {
			break;
		}
	}

	console->pollfds[i].events = (short)(events & 0x7fff);
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
		pollfd = &console->pollfds[i];
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
		sigint = true;
	}
}

int run_console(struct console *console)
{
	sighandler_t sighandler_save = signal(SIGINT, sighandler);
	struct timeval tv;
	long timeout;
	ssize_t rc;

	rc = 0;

	for (;;) {
		uint8_t buf[4096];

		BUILD_ASSERT(sizeof(buf) <= buffer_size);

		if (sigint) {
			fprintf(stderr, "Received interrupt, exiting\n");
			break;
		}

		rc = get_current_time(&tv);
		if (rc) {
			warn("Failed to read current time");
			break;
		}

		timeout = get_poll_timeout(console, &tv);

		rc = poll(console->pollfds,
			  console->n_pollers + MAX_INTERNAL_POLLFD,
			  (int)timeout);

		if (rc < 0) {
			if (errno == EINTR) {
				continue;
			}
			warn("poll error");
			break;
		}

		/* process internal fd first */
		if (console->pollfds[console->n_pollers].revents) {
			rc = read(console->tty_fd, buf, sizeof(buf));
			if (rc <= 0) {
				warn("Error reading from tty device");
				rc = -1;
				break;
			}
			rc = ringbuffer_queue(console->rb, buf, rc);
			if (rc) {
				break;
			}
		}

		if (console->pollfds[console->n_pollers + 1].revents) {
			sd_bus_process(console->bus, NULL);
		}

		/* ... and then the pollers */
		rc = call_pollers(console, &tv);
		if (rc) {
			break;
		}
	}

	signal(SIGINT, sighandler_save);
	sd_bus_unref(console->bus);

	return rc ? -1 : 0;
}
static const struct option options[] = {
    {"config", required_argument, 0, 'c'},
    {0, 0, 0, 0},
};

int main(int argc, char **argv)
{
	const char *config_filename = NULL;
	const char *config_tty_kname = NULL;
	struct console *console;
	struct config *config;
	int rc;

	rc = -1;

	for (;;) {
		int c;
		int idx;

		c = getopt_long(argc, argv, "c:", options, &idx);
		if (c == -1) {
			break;
		}

		switch (c) {
		case 'c':
			config_filename = optarg;
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

	console = malloc(sizeof(struct console));
	memset(console, 0, sizeof(*console));
	console->pollfds =
	    calloc(MAX_INTERNAL_POLLFD, sizeof(*console->pollfds));
	console->rb = ringbuffer_init(buffer_size);

	config = config_init(config_filename);
	if (!config) {
		warnx("Can't read configuration, exiting.");
		goto out_free;
	}

	if (!config_tty_kname) {
		config_tty_kname = config_get_value(config, "upstream-tty");
	}

	if (!config_tty_kname) {
		warnx("No TTY device specified");
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	if (set_socket_info(console, config)) {
		return EXIT_FAILURE;
	}

	console->tty_kname = config_tty_kname;

	rc = tty_init(console, config);
	if (rc) {
		goto out_config_fini;
	}

	dbus_init(console, config);

	handlers_init(console, config);

	rc = run_console(console);

	handlers_fini(console);

out_config_fini:
	config_fini(config);

out_free:
	free(console->pollers);
	free(console->pollfds);
	free(console->tty_sysfs_devnode);
	free(console->tty_dev);
	free(console);

	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
