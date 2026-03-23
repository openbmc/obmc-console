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
#include <errno.h>
#include <sys/mman.h>
#include <linux/types.h>
#include <sys/time.h>
#include <time.h>

#include "console-server.h"
#include "config.h"
#include "util.h"

#define DEFAULT_IS_ENABLE_LOG_TIMESTAMP 0
#define TIMESTAMP_BUF_SIZE		64

enum ansi_mode {
	ANSI_NORMAL = 0,
	ANSI_ESC,	 // saw ESC
	ANSI_CSI,	 // ESC [
	ANSI_OSC,	 // ESC ]
	ANSI_DCS,	 // ESC P
	ANSI_SOS,	 // ESC X
	ANSI_PM,	 // ESC ^
	ANSI_APC,	 // ESC _
	ANSI_STRING_END, // saw ESC in string terminator mode
};

struct ansi_state {
	enum ansi_mode mode;
};

struct text_log_handler {
	struct handler handler;
	struct console *console;
	struct ringbuffer_consumer *rbc;
	int fd;
	int line_count;
	size_t size;
	size_t maxsize;
	size_t pagesize;
	char *log_filename;
	char *rotate_filename;
	struct ansi_state ansi_st;
	char ascii_buf[8192];
	char linebuf[8192];
	size_t linebuf_len;
};

static const char *default_filename_timestamp = LOCALSTATEDIR
	"/log/obmc-console-timestamp.log";

static const size_t default_logsize = 16ul * 1024ul;

static struct text_log_handler *to_text_log_handler(struct handler *handler)
{
	return container_of(handler, struct text_log_handler, handler);
}

static int text_logger_trim(struct text_log_handler *lh)
{
	lh->fd = rotate_single_file(lh->log_filename, lh->rotate_filename,
				    lh->fd);

	if (lh->fd < 0) {
		return -1;
	}

	lh->size = 0;
	lh->line_count = 0;

	return 0;
}

static int text_logger_write_data(struct text_log_handler *lh, uint8_t *buf,
				  size_t len)
{
	/* If a single write is larger than maxsize, truncate it to the tail end */
	if (len > lh->maxsize) {
		buf += len - lh->maxsize;
		len = lh->maxsize;
	}

	if (lh->size + len > lh->maxsize) {
		if (text_logger_trim(lh) < 0) {
			warn("text_logger_trim failed");
			return -1;
		}
	}

	int write_rc = write_buf_to_fd(lh->fd, buf, len);
	if (write_rc < 0) {
		return -1;
	}

	lh->size += len;
	return 0;
}

static size_t sanitize_to_ascii_ansi(const uint8_t *in, size_t in_len,
				     char *out, size_t out_max_len,
				     struct ansi_state *st)
{
	size_t out_len = 0;

	for (size_t i = 0; i < in_len && out_len < out_max_len - 1; i++) {
		uint8_t c = in[i];

		switch (st->mode) {
		case ANSI_NORMAL:
			if (c == 0x1B) { // ESC
				st->mode = ANSI_ESC;
				continue;
			}
			if ((c >= 32 && c <= 126) || c == '\n') {
				out[out_len++] = (char)c;
			}
			continue;

		case ANSI_ESC:
			switch (c) {
			case '[':
				st->mode = ANSI_CSI;
				continue;
			case ']':
				st->mode = ANSI_OSC;
				continue;
			case 'P':
				st->mode = ANSI_DCS;
				continue;
			case 'X':
				st->mode = ANSI_SOS;
				continue;
			case '^':
				st->mode = ANSI_PM;
				continue;
			case '_':
				st->mode = ANSI_APC;
				continue;
			default:
				st->mode = ANSI_NORMAL;
				continue;
			}

		case ANSI_CSI:
			if (c >= 0x40 && c <= 0x7E) {
				st->mode = ANSI_NORMAL;
			}
			continue;

		case ANSI_OSC:
			if (c == 0x07) { // BEL
				st->mode = ANSI_NORMAL;
				continue;
			}
			if (c == 0x1B) { // ESC
				st->mode = ANSI_STRING_END;
				continue;
			}
			continue;

		case ANSI_DCS:
		case ANSI_SOS:
		case ANSI_PM:
		case ANSI_APC:
			if (c == 0x1B) {
				st->mode = ANSI_STRING_END;
				continue;
			}
			continue;

		case ANSI_STRING_END:
			if (c == '\\') {
				st->mode = ANSI_NORMAL;
			} else {
				st->mode = ANSI_OSC;
			}
			continue;
		}
	}

	return out_len;
}

static ssize_t generate_timestamp_text(char *buf, size_t buf_len,
				       int line_count)
{
	struct timeval tv;
	struct tm tmp_tm;
	struct tm *tm_info;
	gettimeofday(&tv, NULL);
	tm_info = localtime_r(&tv.tv_sec, &tmp_tm);

	size_t ret = strftime(buf, buf_len, "%a %b %d %H:%M:%S %Y", tm_info);
	if (ret <= 0) {
		return -1;
	}

	int n = snprintf(buf + ret, buf_len - ret, " %07d ", line_count);
	if (n < 0) {
		return -1;
	}

	return (ssize_t)(ret + n);
}

static int flush_line(struct text_log_handler *lh)
{
	if (lh->fd < 0) {
		return -1;
	}

	char ts_buf[TIMESTAMP_BUF_SIZE];
	ssize_t ts_len =
		generate_timestamp_text(ts_buf, sizeof(ts_buf), lh->line_count);

	if (ts_len > 0) {
		if (text_logger_write_data(lh, (uint8_t *)ts_buf,
					   (size_t)ts_len)) {
			return -1;
		}
	}

	if (lh->linebuf_len > 0) {
		if (text_logger_write_data(lh, (uint8_t *)lh->linebuf,
					   lh->linebuf_len)) {
			return -1;
		}
	}

	if (text_logger_write_data(lh, (uint8_t *)"\n", 1)) {
		return -1;
	}

	lh->linebuf_len = 0;
	lh->line_count++;
	return 0;
}

static int append_char_and_maybe_flush(struct text_log_handler *lh, char c)
{
	if (lh->fd < 0) {
		return -1;
	}

	if (c == '\n') {
		return flush_line(lh);
	}

	if (lh->linebuf_len >= sizeof(lh->linebuf) - 1) {
		if (flush_line(lh)) {
			return -1;
		}
	}

	lh->linebuf[lh->linebuf_len++] = c;
	return 0;
}

static enum ringbuffer_poll_ret
text_log_ringbuffer_poll(void *arg, size_t force_len __attribute__((unused)))
{
	struct text_log_handler *lh = arg;
	uint8_t *buf;
	size_t len;
	int rc;

	for (;;) {
		len = ringbuffer_dequeue_peek(lh->rbc, 0, &buf);
		if (!len) {
			break;
		}

		if (lh->fd >= 0) {
			size_t offset = 0;

			while (offset < len) {
				size_t chunk_size = len - offset;
				if (chunk_size > sizeof(lh->linebuf)) {
					chunk_size = sizeof(lh->linebuf);
				}

				size_t ascii_len = sanitize_to_ascii_ansi(
					buf + offset, chunk_size, lh->ascii_buf,
					sizeof(lh->ascii_buf), &lh->ansi_st);

				for (size_t i = 0; i < ascii_len; i++) {
					rc = append_char_and_maybe_flush(
						lh, lh->ascii_buf[i]);
					if (rc) {
						return RINGBUFFER_POLL_REMOVE;
					}
				}

				offset += chunk_size;
			}
		}

		ringbuffer_dequeue_commit(lh->rbc, len);
	}

	return RINGBUFFER_POLL_OK;
}

static struct handler *text_log_init(const struct handler_type *type
				     __attribute__((unused)),
				     struct console *console,
				     struct config *config)
{
	struct text_log_handler *lh;
	const char *filename;
	const char *logsize_str;
	size_t logsize = default_logsize;
	int rc;

	const char *enable_timestamp_str =
		config_get_value(config, "log-timestamp");
	if (enable_timestamp_str == NULL || strcmp(enable_timestamp_str, "1")) {
		return NULL;
	}

	lh = malloc(sizeof(*lh));
	if (!lh) {
		return NULL;
	}

	lh->console = console;
	lh->fd = -1;
	lh->pagesize = sysconf(_SC_PAGESIZE);
	lh->log_filename = NULL;
	lh->rotate_filename = NULL;
	lh->ansi_st.mode = ANSI_NORMAL;
	lh->linebuf_len = 0;
	lh->line_count = 0;

	logsize_str = config_get_value(config, "logsize");
	rc = config_parse_bytesize(logsize_str, &logsize);
	if (logsize_str != NULL && rc) {
		logsize = default_logsize;
		warn("Invalid logsize. Default to %ukB",
		     (unsigned int)(logsize >> 10));
	}
	lh->maxsize = logsize <= lh->pagesize ? lh->pagesize + 1 : logsize;

	filename = config_get_section_value(config, console->console_id,
					    "logfile-timestamped");
	if (!filename && config_count_sections(config) == 0) {
		filename = config_get_value(config, "logfile-timestamped");
	}
	if (!filename) {
		filename = default_filename_timestamp;
	}

	lh->log_filename = strdup(filename);
	rc = asprintf(&lh->rotate_filename, "%s.1", filename);
	if (rc < 0) {
		warn("Failed to construct rotate filename");
		goto err_free;
	}

	lh->fd = open(lh->log_filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (lh->fd < 0) {
		warn("Can't open text log file %s", lh->log_filename);
		goto err_free;
	}

	off_t pos = lseek(lh->fd, 0, SEEK_END);
	if (pos < 0) {
		warn("Can't query text log position");
		goto err_free;
	}
	lh->size = (size_t)pos;

	if (lh->size >= lh->maxsize) {
		if (text_logger_trim(lh) < 0) {
			goto err_free;
		}
	}

	lh->rbc = console_ringbuffer_consumer_register(
		console, text_log_ringbuffer_poll, lh);
	return &lh->handler;

err_free:
	free(lh->rotate_filename);
	free(lh->log_filename);
	if (lh->fd >= 0) {
		close(lh->fd);
	}
	free(lh);
	return NULL;
}

static void text_log_fini(struct handler *handler)
{
	struct text_log_handler *lh = to_text_log_handler(handler);
	if (lh->rbc) {
		ringbuffer_consumer_unregister(lh->rbc);
	}
	if (lh->fd >= 0) {
		close(lh->fd);
	}
	free(lh->log_filename);
	free(lh->rotate_filename);
	free(lh);
}

static const struct handler_type text_log_handler = {
	.name = "text-log",
	.init = text_log_init,
	.fini = text_log_fini,
};

console_handler_register(&text_log_handler);
