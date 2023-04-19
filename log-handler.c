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

#include <endian.h>
#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

#include <linux/types.h>

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

static const char *default_filename = LOCALSTATEDIR "/log/obmc-console.log";
static const size_t default_logsize = 16ul * 1024ul;

static struct log_handler *to_log_handler(struct handler *handler)
{
	return container_of(handler, struct log_handler, handler);
}

static int log_trim(struct log_handler *lh)
{
	int rc;

	/* Move the log buffer file to the rotate file */
	close(lh->fd);
	rc = rename(lh->log_filename, lh->rotate_filename);
	if (rc) {
		warn("Failed to rename %s to %s", lh->log_filename,
		     lh->rotate_filename);
		/* don't return, as we need to re-open the logfile */
	}

	lh->fd = open(lh->log_filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (lh->fd < 0) {
		warn("Can't open log buffer file %s", lh->log_filename);
		return -1;
	}

	lh->size = 0;

	return 0;
}

static int log_data(struct log_handler *lh, uint8_t *buf, size_t len)
{
	int rc;

	if (len > lh->maxsize) {
		buf += len - lh->maxsize;
		len = lh->maxsize;
	}

	if (lh->size + len > lh->maxsize) {
		rc = log_trim(lh);
		if (rc)
			return rc;
	}

	rc = write_buf_to_fd(lh->fd, buf, len);
	if (rc)
		return rc;

	lh->size += len;

	return 0;
}

static enum ringbuffer_poll_ret log_ringbuffer_poll(void *arg, size_t force_len
						    __attribute__((unused)))
{
	struct log_handler *lh = arg;
	uint8_t *buf;
	size_t len;
	int rc;

	/* we log synchronously, so just dequeue everything we can, and
	 * commit straight away. */
	for (;;) {
		len = ringbuffer_dequeue_peek(lh->rbc, 0, &buf);
		if (!len)
			break;

		rc = log_data(lh, buf, len);
		if (rc)
			return RINGBUFFER_POLL_REMOVE;

		ringbuffer_dequeue_commit(lh->rbc, len);
	}

	return RINGBUFFER_POLL_OK;
}

static int log_init(struct handler *handler, struct console *console,
		    struct config *config)
{
	struct log_handler *lh = to_log_handler(handler);
	const char *filename, *logsize_str;
	size_t logsize = default_logsize;
	int rc;

	lh->console = console;
	lh->pagesize = 4096;
	lh->size = 0;
	lh->log_filename = NULL;
	lh->rotate_filename = NULL;

	logsize_str = config_get_value(config, "logsize");
	rc = config_parse_logsize(logsize_str, &logsize);
	if (logsize_str != NULL && rc) {
		logsize = default_logsize;
		warn("Invalid logsize. Default to %ukB",
		     (unsigned int)(logsize >> 10));
	}
	lh->maxsize = logsize <= lh->pagesize ? lh->pagesize + 1 : logsize;

	filename = config_get_value(config, "logfile");
	if (!filename)
		filename = default_filename;

	lh->fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (lh->fd < 0) {
		warn("Can't open log buffer file %s", filename);
		return -1;
	}

	lh->log_filename = strdup(filename);

	rc = asprintf(&lh->rotate_filename, "%s.1", filename);
	if (rc < 0) {
		warn("Failed to construct rotate filename");
		return -1;
	}

	lh->rbc = console_ringbuffer_consumer_register(console,
						       log_ringbuffer_poll, lh);

	return 0;
}

static void log_fini(struct handler *handler)
{
	struct log_handler *lh = to_log_handler(handler);
	ringbuffer_consumer_unregister(lh->rbc);
	close(lh->fd);
	free(lh->log_filename);
	free(lh->rotate_filename);
}

static struct log_handler log_handler = {
	.handler = {
		.name		= "log",
		.init		= log_init,
		.fini		= log_fini,
	},
};

console_handler_register(&log_handler.handler);
