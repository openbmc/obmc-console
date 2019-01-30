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

#define _GNU_SOURCE

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
#include <limits.h>
#include <termios.h>

#include <sys/types.h>
#include <poll.h>

#include "console-server.h"

#define DBUS_ERR "org.openbmc.error"
#define DBUS_NAME "xyz.openbmc_project.console"
#define OBJ_NAME "/xyz/openbmc_project/console"

struct console {
	const char	*tty_kname;
	char		*tty_sysfs_devnode;
	char		*tty_dev;
	int		tty_sirq;
	int		tty_lpc_addr;
	speed_t		tty_baud;
	int		tty_fd;

	struct ringbuffer	*rb;

	struct handler	**handlers;
	int		n_handlers;

	struct poller	**pollers;
	int		n_pollers;

	struct pollfd	*pollfds;
	struct sd_bus	*bus;
};

struct poller {
	struct handler	*handler;
	void		*data;
	poller_fn_t	fn;
	bool		remove;
};

/* we have two extra entry in the pollfds array for the VUART tty */
enum internal_pollfds {
	POLLFD_HOSTTTY = 0,
	POLLFD_DBUS = 1,
	MAX_INTERNAL_POLLFD = 2,
};

/* size of the shared backlog ringbuffer */
const size_t buffer_size = 128 * 1024;

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
	char *tty_class_device_link;
	char *tty_device_tty_dir;
	char *tty_device_reldir;
	char *tty_path_input;
	char *tty_path_input_real;
	char *tty_kname_real;
	int rc;

	tty_class_device_link = NULL;
	tty_device_tty_dir = NULL;
	tty_device_reldir = NULL;
	tty_path_input = NULL;
	tty_path_input_real = NULL;
	tty_kname_real = NULL;

	/* udev may rename the tty name with a symbol link, try to resolve */
	rc = asprintf(&tty_path_input, "/dev/%s", console->tty_kname);
	if (rc < 0)
		return -1;

	tty_path_input_real = realpath(tty_path_input, NULL);
	if (!tty_path_input_real) {
		warn("Can't find realpath for /dev/%s", console->tty_kname);
		goto out_free;
	}

	tty_kname_real = basename(tty_path_input_real);
	if (!tty_kname_real) {
		warn("Can't find real name for /dev/%s", console->tty_kname);
		goto out_free;
	}

	rc = asprintf(&tty_class_device_link,
			"/sys/class/tty/%s", tty_kname_real);
	if (rc < 0)
		goto out_free;

	tty_device_tty_dir = realpath(tty_class_device_link, NULL);
	if (!tty_device_tty_dir) {
		warn("Can't query sysfs for device %s", tty_kname_real);
		goto out_free;
	}

	rc = asprintf(&tty_device_reldir, "%s/../../", tty_device_tty_dir);
	if (rc < 0)
		goto out_free;

	console->tty_sysfs_devnode = realpath(tty_device_reldir, NULL);
	if (!console->tty_sysfs_devnode)
		warn("Can't find parent device for %s", tty_kname_real);

	rc = asprintf(&console->tty_dev, "/dev/%s", tty_kname_real);
	if (rc < 0)
		goto out_free;

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
	if (rc < 0)
		return -1;

	fp = fopen(path, "w");
	if (!fp) {
		warn("Can't access attribute %s on device %s",
				name, console->tty_kname);
		rc = -1;
		goto out_free;
	}
	setvbuf(fp, NULL, _IONBF, 0);

	rc = fprintf(fp, "0x%x", value);
	if (rc < 0)
		warn("Error writing to %s attribute of device %s",
				name, console->tty_kname);
	fclose(fp);



out_free:
	free(path);
	return rc;
}

/**
 * Set termios attributes on the console tty.
 */
static void tty_init_termios(struct console *console)
{
	struct termios termios;
	int rc;

	rc = tcgetattr(console->tty_fd, &termios);
	if (rc) {
		warn("Can't read tty termios");
		return;
	}

	if (console->tty_baud) {
		if (cfsetspeed(&termios, console->tty_baud) < 0)
			warn("Couldn't set speeds for %s", console->tty_kname);
	}

	/* Set console to raw mode: we don't want any processing to occur on
	 * the underlying terminal input/output.
	 */
	cfmakeraw(&termios);

	rc = tcsetattr(console->tty_fd, TCSANOW, &termios);
	if (rc)
		warn("Can't set terminal options for %s", console->tty_kname);
}


static void tty_change_baudrate(struct console *console)
{
	struct handler *handler;
	int i, rc;

	tty_init_termios(console);

	for (i = 0; i < console->n_handlers; i++) {
		handler = console->handlers[i];
		if (!handler->baudrate)
			continue;

		rc = handler->baudrate(handler, console->tty_baud);
		if (rc)
			warn("Can't set terminal baudrate for handler %s",
			     handler->name);
	}
}

/**
 * Open and initialise the serial device
 */
static int tty_init_io(struct console *console)
{
	if (console->tty_sirq)
		tty_set_sysfs_attr(console, "sirq", console->tty_sirq);
	if (console->tty_lpc_addr)
		tty_set_sysfs_attr(console, "lpc_address",
				console->tty_lpc_addr);

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
	const char *val;
	char *endp;
	int rc;

	val = config_get_value(config, "lpc-address");
	if (val) {
		console->tty_lpc_addr = strtoul(val, &endp, 0);
		if (endp == optarg) {
			warn("Invalid LPC address: '%s'", val);
			return -1;
		}
	}

	val = config_get_value(config, "sirq");
	if (val) {
		console->tty_sirq = strtoul(val, &endp, 0);
		if (endp == optarg)
			warn("Invalid sirq: '%s'", val);
	}

	val = config_get_value(config, "baud");
	if (val) {
		if (config_parse_baud(&console->tty_baud, val))
			warnx("Invalid baud rate: '%s'", val);
	}

	if (!console->tty_kname) {
		warnx("Error: No TTY device specified");
		return -1;
	}

	rc = tty_find_device(console);
	if (rc)
		return rc;

	rc = tty_init_io(console);
	return rc;
}


int console_data_out(struct console *console, const uint8_t *data, size_t len)
{
	return write_buf_to_fd(console->tty_fd, data, len);
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

	console->tty_baud = speed;
	tty_change_baudrate(console);

	return sd_bus_reply_method_return(msg, "x", r);
}

static int get_handler(sd_bus *bus, const char *path, const char *interface,
		       const char *property, sd_bus_message *reply, void *userdata,
		       sd_bus_error *error) {
	struct console *console = userdata;
	uint32_t baudrate;
	int r;

	baudrate = parse_baud_to_int(console->tty_baud);
	if (!baudrate)
		warnx("Invalid baud rate: '%d'", console->tty_baud);

	r = sd_bus_message_append(reply, "u", baudrate);

	return r;
}

static const sd_bus_vtable console_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("setBaudRate", "u", "x", method_set_baud_rate,
		      SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("baudrate", "u", get_handler, 0, 0),
	SD_BUS_VTABLE_END,};

static void dbus_init(struct console *console, struct config *config)
{
	int dbus_poller = 0;
	int fd, r;

	if (!console) {
		warn("Couldn't get valid console\n");
		return;
	}

	r = sd_bus_default_system(&console->bus);
	if (r < 0) {
		warn("Failed to connect to system bus: %s\n", strerror(-r));
		return;
	}

	r = sd_bus_add_object_vtable(console->bus, NULL, OBJ_NAME, DBUS_NAME,
				     console_vtable, console);
	if (r < 0) {
		warn("Failed to issue method call: %s\n", strerror(-r));
		return;
	}

	r = sd_bus_request_name(console->bus, DBUS_NAME, SD_BUS_NAME_ALLOW_REPLACEMENT
				|SD_BUS_NAME_REPLACE_EXISTING);
	if (r < 0) {
		warn("Failed to acquire service name: %s\n", strerror(-r));
		return;
	}

	fd = sd_bus_get_fd(console->bus);
	if (fd < 0) {
		warn("Couldn't get the bus file descriptor\n");
		return;
	}

	dbus_poller = POLLFD_DBUS;

	console->pollfds[dbus_poller].fd = fd;
	console->pollfds[dbus_poller].events = POLLIN;
}

static void handlers_init(struct console *console, struct config *config)
{
	extern struct handler *__start_handlers, *__stop_handlers;
	struct handler *handler;
	int i, rc;

	console->n_handlers = &__stop_handlers - &__start_handlers;
	console->handlers = &__start_handlers;

	printf("%d handler%s\n", console->n_handlers,
			console->n_handlers == 1 ? "" : "s");

	for (i = 0; i < console->n_handlers; i++) {
		handler = console->handlers[i];

		rc = 0;
		if (handler->init)
			rc = handler->init(handler, console, config);

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
		if (handler->fini && handler->active)
			handler->fini(handler);
	}
}

struct ringbuffer_consumer *console_ringbuffer_consumer_register(
		struct console *console,
		ringbuffer_poll_fn_t poll_fn, void *data)
{
	return ringbuffer_consumer_register(console->rb, poll_fn, data);
}

struct poller *console_poller_register(struct console *console,
		struct handler *handler, poller_fn_t poller_fn,
		int fd, int events, void *data)
{
	struct poller *poller;
	int n;

	poller = malloc(sizeof(*poller));
	poller->remove = false;
	poller->handler = handler;
	poller->fn = poller_fn;
	poller->data = data;

	/* add one to our pollers array */
	n = console->n_pollers++;
	console->pollers = realloc(console->pollers,
			sizeof(*console->pollers) * console->n_pollers);

	console->pollers[n] = poller;

	/* increase pollfds array too  */
	console->pollfds = realloc(console->pollfds,
			sizeof(*console->pollfds) *
				(MAX_INTERNAL_POLLFD + console->n_pollers));

	/* shift the end pollfds up by one */
	memcpy(&console->pollfds[n+1],
		&console->pollfds[n],
		sizeof(*console->pollfds) * MAX_INTERNAL_POLLFD);

	console->pollfds[n].fd = fd;
	console->pollfds[n].events = events;

	return poller;
}

void console_poller_unregister(struct console *console,
		struct poller *poller)
{
	int i;

	/* find the entry in our pollers array */
	for (i = 0; i < console->n_pollers; i++)
		if (console->pollers[i] == poller)
			break;

	assert(i < console->n_pollers);

	console->n_pollers--;

	/* remove the item from the pollers array... */
	memmove(&console->pollers[i], &console->pollers[i+1],
			sizeof(*console->pollers)
				* (console->n_pollers - i));

	console->pollers = realloc(console->pollers,
			sizeof(*console->pollers) * console->n_pollers);

	/* ... and the pollfds array */
	memmove(&console->pollfds[i], &console->pollfds[i+1],
			sizeof(*console->pollfds) *
				(MAX_INTERNAL_POLLFD + console->n_pollers - i));

	console->pollfds = realloc(console->pollfds,
			sizeof(*console->pollfds) *
				(MAX_INTERNAL_POLLFD + console->n_pollers));


	free(poller);
}

void console_poller_set_events(struct console *console, struct poller *poller,
		int events)
{
	int i;

	/* find the entry in our pollers array */
	for (i = 0; i < console->n_pollers; i++)
		if (console->pollers[i] == poller)
			break;

	console->pollfds[i].events = events;
}

static int call_pollers(struct console *console)
{
	struct poller *poller;
	struct pollfd *pollfd;
	enum poller_ret prc;
	int i, rc;

	rc = 0;

	/*
	 * Process poll events by iterating through the pollers and pollfds
	 * in-step, calling any pollers that we've found revents for.
	 */
	for (i = 0; i < console->n_pollers; i++) {
		poller = console->pollers[i];
		pollfd = &console->pollfds[i];

		if (!pollfd->revents)
			continue;

		prc = poller->fn(poller->handler, pollfd->revents,
				poller->data);
		if (prc == POLLER_EXIT)
			rc = -1;
		else if (prc == POLLER_REMOVE)
			poller->remove = true;
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
		if (!removed)
			break;
	}

	return rc;
}

static void sighandler(int signal)
{
	if (signal == SIGINT)
		sigint = true;
}

int run_console(struct console *console)
{
	sighandler_t sighandler_save;
	int rc;

	sighandler_save = signal(SIGINT, sighandler);

	rc = 0;

	for (;;) {
		uint8_t buf[4096];

		BUILD_ASSERT(sizeof(buf) <= buffer_size);

		if (sigint) {
			fprintf(stderr, "Received interrupt, exiting\n");
			break;
		}

		rc = poll(console->pollfds,
				console->n_pollers + MAX_INTERNAL_POLLFD, -1);
		if (rc < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				warn("poll error");
				break;
			}
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
			if (rc)
				break;
		}

		if (console->pollfds[console->n_pollers + 1].revents) {
			sd_bus_process(console->bus, NULL);
		}

		/* ... and then the pollers */
		rc = call_pollers(console);
		if (rc)
			break;
	}

	signal(SIGINT, sighandler_save);
	sd_bus_unref(console->bus);

	return rc ? -1 : 0;
}
static const struct option options[] = {
	{ "config",	required_argument,	0, 'c'},
	{ 0,  0, 0, 0},
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
		int c, idx;

		c = getopt_long(argc, argv, "c:", options, &idx);
		if (c == -1)
			break;

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

	if (optind >= argc) {
		warnx("Required argument <DEVICE> missing");
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	config_tty_kname = argv[optind];

	console = malloc(sizeof(struct console));
	memset(console, 0, sizeof(*console));
	console->pollfds = calloc(MAX_INTERNAL_POLLFD,
			sizeof(*console->pollfds));
	console->rb = ringbuffer_init(buffer_size);

	config = config_init(config_filename);
	if (!config) {
		warnx("Can't read configuration, exiting.");
		goto out_free;
	}

	console->tty_kname = config_tty_kname;

	rc = tty_init(console, config);
	if (rc)
		goto out_config_fini;

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
