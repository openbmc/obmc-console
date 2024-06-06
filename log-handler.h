#pragma once

#include <stdlib.h>

#include "console-server.h"

struct log_handler {
	struct handler handler;
	struct console *console;
	struct ringbuffer_consumer *rbc;
	int fd;
	size_t size;
	size_t maxsize;
	size_t pagesize;
	char *log_filename;
	char *rotate_filename;
};

int log_init(struct handler *handler, struct console *console,
	     struct config *config);
void log_fini(struct handler *handler);
