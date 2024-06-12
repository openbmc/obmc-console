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
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#include "console-server.h"
#include "config.h"

struct tty_handler {
	struct handler handler;
	struct console *console;
	struct ringbuffer_consumer *rbc;
	struct poller *poller;
	int fd;
	int fd_flags;
	bool blocked;
};

int tty_baudrate(struct handler *handler, speed_t baudrate);

int tty_init(struct handler *handler, struct console *console,
	     struct config *config __attribute__((unused)));

void tty_fini(struct handler *handler);
