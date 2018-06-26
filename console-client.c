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

#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "console-server.h"

enum process_rc {
	PROCESS_OK = 0,
	PROCESS_ERR,
	PROCESS_EXIT,
};

struct console_client {
	int		console_sd;
	int		fd_in;
	int		fd_out;
	bool		is_tty;
	struct termios	orig_termios;
	int		esc_str_pos;
	bool		newline;
};

static const uint8_t esc_str[] = { '~', '.' };

static enum process_rc process_tty(struct console_client *client)
{
	uint8_t e, buf[4096];
	long i;
	ssize_t len;
	int rc;

	len = read(client->fd_in, buf, sizeof(buf));
	if (len < 0)
		return PROCESS_ERR;
	if (len == 0)
		return PROCESS_EXIT;

	/* check escape sequence status */
	for (i = 0; i < len; i++) {
		/* the escape string is only valid after a newline */
		if (buf[i] == '\r') {
			client->newline = true;
			continue;
		}

		if (!client->newline)
			continue;

		e = esc_str[client->esc_str_pos];
		if (buf[i] == e) {
			client->esc_str_pos++;

			/* have we hit the end of the escape string? */
			if (client->esc_str_pos == ARRAY_SIZE(esc_str)) {

				/* flush out any data before the escape */
				if (i > client->esc_str_pos)
					write_buf_to_fd(client->console_sd,
							buf,
							i - client->esc_str_pos);

				return PROCESS_EXIT;
			}
		} else {
			/* if we're partially the way through the escape
			 * string, flush out the bytes we'd skipped */
			if (client->esc_str_pos)
				write_buf_to_fd(client->console_sd,
						esc_str, client->esc_str_pos);
			client->esc_str_pos = 0;
			client->newline = false;
		}
	}

	rc = write_buf_to_fd(client->console_sd, buf,
			     len - client->esc_str_pos);
	if (rc < 0)
		return PROCESS_ERR;

	return PROCESS_OK;
}


static int process_console(struct console_client *client)
{
	uint8_t buf[4096];
	int len, rc;

	len = read(client->console_sd, buf, sizeof(buf));
	if (len < 0) {
		warn("Can't read from server");
		return PROCESS_ERR;
	}
	if (len == 0) {
		fprintf(stderr, "Connection closed\n");
		return PROCESS_EXIT;
	}

	rc = write_buf_to_fd(client->fd_out, buf, len);
	return rc ? PROCESS_ERR : PROCESS_OK;
}

/*
 * Setup our local file descriptors for IO: use stdin/stdout, and if we're on a
 * TTY, put it in canonical mode
 */
static int client_tty_init(struct console_client *client)
{
	struct termios termios;
	int rc;

	client->fd_in = STDIN_FILENO;
	client->fd_out = STDOUT_FILENO;
	client->is_tty = isatty(client->fd_in);

	if (!client->is_tty)
		return 0;

	rc = tcgetattr(client->fd_in, &termios);
	if (rc) {
		warn("Can't get terminal attributes for console");
		return -1;
	}
	memcpy(&client->orig_termios, &termios, sizeof(client->orig_termios));
	cfmakeraw(&termios);

	rc = tcsetattr(client->fd_in, TCSANOW, &termios);
	if (rc) {
		warn("Can't set terminal attributes for console");
		return -1;
	}

	return 0;
}

static int client_init(struct console_client *client)
{
	struct sockaddr_un addr;
	int rc;

	client->console_sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (!client->console_sd) {
		warn("Can't open socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(&addr.sun_path, &console_socket_path, console_socket_path_len);

	rc = connect(client->console_sd, (struct sockaddr *)&addr,
		     sizeof(addr));
	if (rc) {
		warn("Can't connect to console server");
		close(client->console_sd);
		return -1;
	}

	return 0;
}

static void client_fini(struct console_client *client)
{
	if (client->is_tty)
		tcsetattr(client->fd_in, TCSANOW, &client->orig_termios);
	close(client->console_sd);
}

int main(void)
{
	struct console_client _client, *client;
	struct pollfd pollfds[2];
	enum process_rc prc;
	int rc;

	client = &_client;
	memset(client, 0, sizeof(*client));

	rc = client_init(client);
	if (rc)
		return EXIT_FAILURE;

	rc = client_tty_init(client);
	if (rc)
		goto out_fini;

	prc = PROCESS_OK;
	for (;;) {
		pollfds[0].fd = client->fd_in;
		pollfds[0].events = POLLIN;
		pollfds[1].fd = client->console_sd;
		pollfds[1].events = POLLIN;

		rc = poll(pollfds, 2, -1);
		if (rc < 0) {
			warn("Poll failure");
			break;
		}

		if (pollfds[0].revents)
			prc = process_tty(client);

		if (prc == PROCESS_OK && pollfds[1].revents)
			prc = process_console(client);

		rc = (prc == PROCESS_ERR) ? -1 : 0;
		if (prc != PROCESS_OK)
			break;
	}

out_fini:
	client_fini(client);
	return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}

