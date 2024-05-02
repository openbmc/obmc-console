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

struct client {
	struct socket_handler *sh;
	struct poller *poller;
	struct ringbuffer_consumer *rbc;
	int fd;
	bool blocked;
};

struct socket_handler {
	struct handler handler;
	struct console *console;
	struct poller *poller;
	int sd;

	struct client **clients;
	int n_clients;
};

int socket_init(struct handler *handler, struct console *console,
		struct config *config __attribute__((unused)));

void socket_fini(struct handler *handler);
