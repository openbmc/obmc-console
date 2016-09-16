/**
 * Copyright © 2016 IBM Corporation
 * Copyright © 2016 Raptor Engineering, LLC
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "console-server.h"

struct tty_handler {
	struct handler	handler;
	struct console	*console;
	struct poller	*poller;
	int		fd;
};

static struct tty_handler *to_tty_handler(struct handler *handler)
{
	return container_of(handler, struct tty_handler, handler);
}

static enum poller_ret tty_poll(struct handler *handler,
		int events, void __attribute__((unused)) *data)
{
	struct tty_handler *th = to_tty_handler(handler);
	uint8_t buf[4096];
	ssize_t len;

	if (!(events & POLLIN))
		return POLLER_OK;

	len = read(th->fd, buf, sizeof(buf));
	if (len <= 0) {
		th->poller = NULL;
		close(th->fd);
		return POLLER_REMOVE;
	}

	console_data_out(th->console, buf, len);

	return POLLER_OK;
}

static int tty_mirror_init(struct handler *handler,
	struct console *console __attribute__((unused)),
	struct config *config)
{
	struct tty_handler *th = to_tty_handler(handler);
	struct termios tty_terminfo;
	const char *tty_baud;
	int tty_baudrate = 115200;
	speed_t tty_enum_baudrate;

	tty_baud = config_get_value(config, "local-tty-baud");
	if (tty_baud)
		tty_baudrate = atoi(tty_baud);

	if (tty_baudrate == 19200)
		tty_enum_baudrate = B19200;
	else if (tty_baudrate == 38400)
		tty_enum_baudrate = B38400;
	else if (tty_baudrate == 57600)
		tty_enum_baudrate = B57600;
	else
		/* Default to 115200 baud */
		tty_enum_baudrate = B115200;

	/* set up serial tty */
	tcgetattr(th->fd, &tty_terminfo);
	tty_terminfo.c_iflag = 0;
	tty_terminfo.c_oflag = 0;
	tty_terminfo.c_lflag = 0;
	tty_terminfo.c_cflag = 0;
	tty_terminfo.c_cc[VMIN] = 0;
	tty_terminfo.c_cc[VTIME] = 0;
	cfsetospeed(&tty_terminfo, tty_enum_baudrate);
	cfsetispeed(&tty_terminfo, tty_enum_baudrate);
	tty_terminfo.c_cflag |= CREAD|CLOCAL|CS8;
	tcsetattr(th->fd, TCSANOW, &tty_terminfo);

	return 0;
}

static int tty_init(struct handler *handler, struct console *console,
		struct config *config)
{
	struct tty_handler *th = to_tty_handler(handler);
	const char *tty_name;
	const char *tty_type;
	char *tty_path;
	int rc, flags;

	tty_name = config_get_value(config, "local-tty");
	if (!tty_name)
		return -1;

	rc = asprintf(&tty_path, "/dev/%s", tty_name);
	if (!rc)
		return -1;

	th->fd = open(tty_path, O_RDWR|O_SYNC);
	if (th->fd < 0) {
		warn("Can't open %s; disabling local tty", tty_name);
		free(tty_path);
		return -1;
	}

	free(tty_path);

	/* initial tty setup */
	flags = fcntl(th->fd, F_GETFL, 0);
	flags |= FNDELAY;
	fcntl(th->fd, F_SETFL, flags);

	th->poller = console_register_poller(console, handler, tty_poll,
			th->fd, POLLIN, NULL);
	th->console = console;

	tty_type = config_get_value(config, "local-tty-type");
	if (!tty_type)
		return 0;

	if (strcmp(tty_type, "serial"))
		return 0;

	tty_mirror_init(handler, console, config);

	return 0;
}

static int tty_data(struct handler *handler, uint8_t *buf, size_t len)
{
	struct tty_handler *th = to_tty_handler(handler);
	return write_buf_to_fd(th->fd, buf, len);
}

static void tty_fini(struct handler *handler)
{
	struct tty_handler *th = to_tty_handler(handler);
	if (th->poller)
		console_unregister_poller(th->console, th->poller);
	close(th->fd);
}

static struct tty_handler tty_handler = {
	.handler = {
		.name		= "tty",
		.init		= tty_init,
		.data_in	= tty_data,
		.fini		= tty_fini,
	},
};

console_register_handler(&tty_handler.handler);

