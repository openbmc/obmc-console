/**
 * Copyright © 2024 Google
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

#include <ctype.h>
#include <endian.h>
#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>
#include <unistd.h>
#include <syslog.h>

#include <sys/mman.h>

#include <linux/types.h>

#include "console-server.h"

/* syslog data buffering size threshold 1KB */
#define SYSLOG_HANDLER_BUF_SIZE_THRESHOLD 1024

#define min(a, b) ((a) < (b) ? (a) : (b))

static struct timeval const syslog_ts_low_precision = {
	.tv_sec = 1,
	.tv_usec = 0,
};
static struct timeval const syslog_ts_high_precision = {
	.tv_sec = 0,
	.tv_usec = 100000, /* 100 ms */
};
static struct timeval const *syslog_handler_timeout = &syslog_ts_low_precision;

struct syslog_handler {
	struct handler handler;
	struct console *console;
	struct poller *poller;
	struct ringbuffer_consumer *rbc;
	char *syslogid;
	int pipefd[2];
	size_t curser;
	char line[SYSLOG_HANDLER_BUF_SIZE_THRESHOLD + 1];
};

static struct syslog_handler *to_syslog_handler(struct handler *handler)
{
	return container_of(handler, struct syslog_handler, handler);
}

/*
 * Log the data line by line, line longer than 1K will be break into multiple lines.
 * Take '\n','\r' or '\0' as line break, filter out non-printable characters.
 */
static void syslog_data_as_canonical_line(struct syslog_handler *lh,
					  const uint8_t *data,
					  size_t to_log_len)
{
	const char *p = (const char *)data;
	const char *end = p + to_log_len;
	char *line = lh->line;
	size_t wi = lh->curser;
	while (p < end) {
		if (isprint(*p)) {
			line[wi++] = *p;
		} else {
			if ((*p == '\0' || *p == '\n' || *p == '\r') && wi) {
				/* log non-empty line */
				line[wi] = '\0';
				syslog(LOG_INFO, "%s", line);
				wi = 0;
			}
		}
		p++;
		/* force line break */
		if (wi == SYSLOG_HANDLER_BUF_SIZE_THRESHOLD) {
			line[wi] = '\0';
			syslog(LOG_INFO, "%s", line);
			wi = 0;
		}
	}
	lh->curser = wi;
}

static void syslog_drain_queue(struct syslog_handler *lh, size_t to_drain_len)
{
	uint8_t *buf;
	size_t drained_len = 0;

	while (to_drain_len) {
		size_t len =
			ringbuffer_dequeue_peek(lh->rbc, drained_len, &buf);
		if (len == 0) {
			break;
		}
		len = min(to_drain_len, len);
		syslog_data_as_canonical_line(lh, buf, len);
		ringbuffer_dequeue_commit(lh->rbc, len);
		drained_len += len;
		to_drain_len -= len;
	}
}

static enum ringbuffer_poll_ret syslog_ringbuffer_poll(void *arg,
						       size_t force_len)
{
	struct syslog_handler *lh = arg;
	size_t len;

	if (force_len) {
		/* Drain only force_len when blocking ringbuf enque console input */
		syslog_drain_queue(lh, force_len);
		return RINGBUFFER_POLL_OK;
	}

	len = ringbuffer_len(lh->rbc);
	if (len < SYSLOG_HANDLER_BUF_SIZE_THRESHOLD) {
		/* Buffer the console data unitl more than 1K or idle 1 second
		 to push the data to syslog */
		console_poller_set_timeout(lh->console, lh->poller,
					   syslog_handler_timeout);
		return RINGBUFFER_POLL_OK;
	}
	syslog_drain_queue(lh, len);
	return RINGBUFFER_POLL_OK;
}

static enum poller_ret dummy_poll(struct handler *handler, int events,
				  void *data)
{
	/* shall not get called */
	warn("Unexpected dummy_poll call handler = %p, event = 0x%8X, data = %p",
	     (void *)handler, events, data);
	return POLLER_EXIT;
}

static enum poller_ret accum_timeout(struct handler *handler,
				     void *data __attribute__((unused)))
{
	struct syslog_handler *lh = to_syslog_handler(handler);
	size_t len = ringbuffer_len(lh->rbc);

	syslog_drain_queue(lh, len);
	return POLLER_OK;
}

static int syslog_init(struct handler *handler, struct console *console,
		       struct config *config)
{
	struct syslog_handler *lh = to_syslog_handler(handler);
	const char *syslogid;
	const char *ts_high_precision;

	lh->console = console;
	lh->poller = NULL;
	lh->rbc = NULL;
	lh->syslogid = NULL;
	lh->curser = 0;

	syslogid = config_get_value(config, "syslogid");
	if (syslogid == NULL) {
		warnx("syslogid is not configured, not emit console log to syslog");
		return -1;
	}

	syslog_handler_timeout = &syslog_ts_low_precision;
	ts_high_precision =
		config_get_value(config, "syslog_ts_high_precision");
	if (ts_high_precision && !strcasecmp(ts_high_precision, "true")) {
		syslog_handler_timeout = &syslog_ts_high_precision;
	}

	/* regsiter console_poller used for timeout only so create a dummy pipe */
	if (pipe(lh->pipefd) == -1) {
		warn("syslog handler create pipe failed");
		return -1;
	}

	lh->syslogid = strdup(syslogid);
	openlog(lh->syslogid, LOG_NOWAIT, LOG_USER);

	lh->poller = console_poller_register(console, handler, dummy_poll,
					     accum_timeout, lh->pipefd[0], 0,
					     NULL);

	lh->rbc = console_ringbuffer_consumer_register(
		console, syslog_ringbuffer_poll, lh);

	return 0;
}

static void syslog_fini(struct handler *handler)
{
	struct syslog_handler *lh = to_syslog_handler(handler);

	ringbuffer_consumer_unregister(lh->rbc);
	console_poller_unregister(lh->console, lh->poller);
	closelog();
	close(lh->pipefd[0]);
	close(lh->pipefd[1]);
	free(lh->syslogid);
}

static struct syslog_handler syslog_handler = {
	.handler = {
		.name		= "syslog",
		.init		= syslog_init,
		.fini		= syslog_fini,
	},
};

console_handler_register(&syslog_handler.handler);
