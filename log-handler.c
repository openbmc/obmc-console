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

#include "console-server.h"
#include "config.h"

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

struct binary_logger {
	int fd;
	size_t size;
	size_t maxsize;
	char *log_filename;
	char *rotate_filename;
};

struct text_logger {
	int fd;
	int line_count;
	size_t size;
	size_t maxsize;
	char *log_filename;
	char *rotate_filename;
	struct ansi_state ansi_st;
	char ascii_buf[8192];
	char linebuf[8192];
	size_t linebuf_len;
};

struct log_handler {
	struct handler handler;
	struct console *console;
	struct ringbuffer_consumer *rbc;
	size_t pagesize;
	struct binary_logger binary_log;
	struct text_logger text_log;
	bool is_text_log_enabled;
};

static const char *default_filename = LOCALSTATEDIR "/log/obmc-console.log";
static const char *default_filename_timestamp = LOCALSTATEDIR
	"/log/obmc-console-timestamp.log";

static const size_t default_logsize = 16ul * 1024ul;

static struct log_handler *to_log_handler(struct handler *handler)
{
	return container_of(handler, struct log_handler, handler);
}

/**
 * @brief Rotates a single log file by renaming it and creating a new empty one.
 *
 * This helper function performs a log file rotation. It first closes the
 * existing open file descriptor, then renames the current log file to a
 * backup name (e.g., "console.log" -> "console.log.1"). Finally, it
 * re-opens the original filename as a new, empty file for subsequent logging.
 *
 * @param[in] filename        - The path to the current log file.
 * @param[in] rotate_filename - The path to rename the old log file to.
 * @param[in] old_fd          - The file descriptor of the currently open log file to be closed.
 *
 * @return The new file descriptor for the re-created file on success,
 *         or -1 on failure.
 */
static int rotate_single_file(const char *filename, const char *rotate_filename,
			      int old_fd)
{
	int rc;

	/* close old fd (ignore errors) */
	if (old_fd >= 0) {
		close(old_fd);
	}

	/* rename old -> rotate */
	rc = rename(filename, rotate_filename);
	if (rc) {
		warn("Failed to rename %s to %s, errno: %d\n", filename,
		     rotate_filename, errno);
	}

	/* open new empty file */
	int new_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (new_fd < 0) {
		warn("Can't open log file %s, errno: %d\n", filename, errno);
		return -1;
	}

	return new_fd;
}

static int binary_logger_trim(struct binary_logger *logger)
{
	logger->fd = rotate_single_file(logger->log_filename,
					logger->rotate_filename, logger->fd);

	if (logger->fd < 0) {
		return -1;
	}

	logger->size = 0;

	return 0;
}

static int text_logger_trim(struct text_logger *logger)
{
	logger->fd = rotate_single_file(logger->log_filename,
					logger->rotate_filename, logger->fd);

	if (logger->fd < 0) {
		return -1;
	}

	logger->size = 0;
	logger->line_count = 0;

	return 0;
}

static void prevent_log_data_exceeded_maxsize(uint8_t **buf, size_t *len,
					      size_t maxsize)
{
	if (*len > maxsize) {
		*buf += (*len - maxsize);
		*len = maxsize;
	}
}

int binary_logger_write_data(struct binary_logger *logger, uint8_t *buf,
			     size_t len)
{
	prevent_log_data_exceeded_maxsize(&buf, &len, logger->maxsize);

	if (logger->size + len >= logger->maxsize) {
		if (binary_logger_trim(logger) < 0) {
			warn("binary_logger_trim failed, errno: %d", errno);
			return -1;
		}
	}

	int write_rc = write_buf_to_fd(logger->fd, buf, len);
	if (write_rc < 0) {
		warn("write_buf_to_fd failed, rc: %d, errno: %d", write_rc,
		     errno);
		return -1;
	}

	logger->size += len;
	return 0;
}

int text_logger_write_data(struct text_logger *logger, uint8_t *buf, size_t len)
{
	prevent_log_data_exceeded_maxsize(&buf, &len, logger->maxsize);

	if (logger->size + len >= logger->maxsize) {
		if (text_logger_trim(logger) < 0) {
			warn("text_logger_trim failed, errno: %d", errno);
			return -1;
		}
	}

	int write_rc = write_buf_to_fd(logger->fd, buf, len);
	if (write_rc < 0) {
		warn("write_buf_to_fd failed, rc: %d, errno: %d", write_rc,
		     errno);
		return -1;
	}

	logger->size += len;
	return 0;
}

int parse_binary_string(const char *str, int *out)
{
	if (strcmp(str, "0") == 0) {
		*out = 0;
		return 0;
	}
	if (strcmp(str, "1") == 0) {
		*out = 1;
		return 0;
	}

	return -1;
}

/**
 * @brief Sanitizes a byte stream by removing ANSI escape codes.
 *
 * This function processes an input byte stream and filters out all ANSI escape
 * sequences, writing only printable ASCII characters (0x20-0x7E) and
 * newlines ('\n') to the output buffer.
 *
 * It uses a state machine to correctly parse escape sequences that may be
 * split across multiple calls. The caller is responsible for preserving the
 * state struct between calls when processing a continuous stream.
 *
 * @param[in]  in          - Pointer to the input byte buffer.
 * @param[in]  in_len      - Length of the input buffer.
 * @param[out] out         - Pointer to the output buffer for the sanitized characters.
 * @param[in]  out_max_len - Maximum capacity of the output buffer.
 * @param[in,out] st       - Pointer to the ANSI parser state struct, which must be
 *                         preserved across calls.
 *
 * @return The number of bytes written to the output buffer.
 */
static size_t sanitize_to_ascii_ansi(const uint8_t *in, size_t in_len,
				     char *out, size_t out_max_len,
				     struct ansi_state *st)
{
	size_t out_len = 0;

	for (size_t i = 0; i < in_len && out_len < out_max_len - 1; i++) {
		uint8_t c = in[i];

		switch (st->mode) {
		/* ============================
         * NORMAL MODE
         * ============================ */
		case ANSI_NORMAL:
			if (c == 0x1B) { // ESC
				st->mode = ANSI_ESC;
				continue;
			}
			if ((c >= 32 && c <= 126) ||
			    c == '\n') { // printable ASCII and line break
				out[out_len++] = (char)c;
			}
			continue;

		/* ============================
         * ESC was seen
         * ============================ */
		case ANSI_ESC:
			switch (c) {
			case '[':
				st->mode = ANSI_CSI;
				continue; // CSI
			case ']':
				st->mode = ANSI_OSC;
				continue; // OSC
			case 'P':
				st->mode = ANSI_DCS;
				continue; // DCS
			case 'X':
				st->mode = ANSI_SOS;
				continue; // SOS
			case '^':
				st->mode = ANSI_PM;
				continue; // PM
			case '_':
				st->mode = ANSI_APC;
				continue; // APC

			/* ESC \  (string terminator) shouldn't happen here */
			default:
				st->mode =
					ANSI_NORMAL; // treat as two-byte ESC sequence
				continue;
			}

		/* ============================
         * CSI: ESC [ ... final (0x40–0x7E)
         * ============================ */
		case ANSI_CSI:
			if (c >= 0x40 && c <= 0x7E) {
				st->mode = ANSI_NORMAL; // CSI finished
			}
			continue;

		/* ============================
         * OSC: ESC ] ... BEL(0x07) or ESC \
         * ============================ */
		case ANSI_OSC:
			if (c == 0x07) { // BEL → end
				st->mode = ANSI_NORMAL;
				continue;
			}
			if (c == 0x1B) { // ESC → maybe terminator
				st->mode = ANSI_STRING_END;
				continue;
			}
			continue;

		/* ============================
         * DCS, SOS, PM, APC share the same terminator: ESC \
         * ============================ */
		case ANSI_DCS:
		case ANSI_SOS:
		case ANSI_PM:
		case ANSI_APC:
			if (c == 0x1B) {
				st->mode = ANSI_STRING_END;
				continue;
			}
			continue;

		/* ============================
         * ESC seen inside a string-type sequence (OSC/DCS/etc)
         * Expecting '\'
         * ============================ */
		case ANSI_STRING_END:
			if (c == '\\') {
				st->mode = ANSI_NORMAL; // string terminated
			} else {
				// Not a terminator; stay in the string (rare case)
				// ECMA-48 says ignore and go back to sequence state
				st->mode = ANSI_OSC; // best-effort fallback
			}
			continue;
		}
	}

	return out_len;
}

/**
 * @brief Generates a timestamp string with a line count.
 *
 * This function generates a formatted timestamp string and writes it to the provided buffer.
 * The timestamp format is "Www Mmm dd hh:mm:ss yyyy" (e.g., "Wed Nov 19 14:30:05 2025"),
 * followed by a seven-digit line count.
 *
 * @param[out] buf
 *     A pointer to the buffer to store the generated timestamp string.
 *
 * @param[in] buf_len
 *     The size of the buffer in bytes.
 *
 * @param[in] line_count
 *     The line count to be included in the timestamp.
 *
 * @return The number of bytes written to the buffer on success,
 *         -1 if strftime or snprintf fails.
 */
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

static void flush_line(struct text_logger *logger)
{
	if (logger->fd < 0) {
		return;
	}

	char ts_buf[TIMESTAMP_BUF_SIZE];
	ssize_t ts_len = generate_timestamp_text(ts_buf, sizeof(ts_buf),
						 logger->line_count);

	/* write timestamp */
	if (ts_len > 0) {
		if (text_logger_write_data(logger, (uint8_t *)ts_buf, ts_len)) {
			return;
		}
	}

	/* write line payload */
	if (logger->linebuf_len > 0) {
		if (text_logger_write_data(logger, (uint8_t *)logger->linebuf,
					   logger->linebuf_len)) {
			return;
		}
	}

	/* write newline */
	if (text_logger_write_data(logger, (uint8_t *)"\n", 1)) {
		return;
	}

	logger->linebuf_len = 0;
	logger->line_count++;
}

static void append_char_and_maybe_flush(struct text_logger *logger, char c)
{
	if (logger->fd < 0) {
		return;
	}

	/* Newline triggers a flush */
	if (c == '\n') {
		flush_line(logger);
		return;
	}

	/* If buffer is FULL, flush before writing */
	if (logger->linebuf_len >= sizeof(logger->linebuf) - 1) {
		flush_line(logger);
	}

	logger->linebuf[logger->linebuf_len++] = c;
}

static enum ringbuffer_poll_ret log_ringbuffer_poll(void *arg, size_t force_len
						    __attribute__((unused)))
{
	struct log_handler *lh = arg;
	uint8_t *buf;
	size_t len;
	int rc;

	for (;;) {
		len = ringbuffer_dequeue_peek(lh->rbc, 0, &buf);
		if (!len) {
			break;
		}

		/* 1. binary log always write*/
		rc = binary_logger_write_data(&lh->binary_log, buf, len);
		if (rc) {
			return RINGBUFFER_POLL_REMOVE;
		}

		/* 2. timestamped text log */
		if (lh->is_text_log_enabled && lh->text_log.fd >= 0) {
			size_t offset = 0;

			while (offset < len) {
				size_t chunk_size = len - offset;
				if (chunk_size > sizeof(lh->text_log.linebuf)) {
					chunk_size =
						sizeof(lh->text_log.linebuf);
				}

				size_t ascii_len = sanitize_to_ascii_ansi(
					buf + offset, chunk_size,
					lh->text_log.ascii_buf,
					sizeof(lh->text_log.ascii_buf),
					&lh->text_log.ansi_st);

				for (size_t i = 0; i < ascii_len; i++) {
					append_char_and_maybe_flush(
						&lh->text_log,
						lh->text_log.ascii_buf[i]);
				}

				offset += chunk_size;
			}
		}

		ringbuffer_dequeue_commit(lh->rbc, len);
	}

	return RINGBUFFER_POLL_OK;
}

static int binary_logger_create(struct binary_logger *logger)
{
	off_t pos;

	logger->fd =
		open(logger->log_filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (logger->fd < 0) {
		warn("Can't open binary log file %s", logger->log_filename);
		return -1;
	}

	pos = lseek(logger->fd, 0, SEEK_END);
	if (pos < 0) {
		warn("Can't query binary log position for file %s",
		     logger->log_filename);
		close(logger->fd);
		return -1;
	}

	logger->size = pos;

	if (logger->size >= logger->maxsize) {
		return binary_logger_trim(logger);
	}

	return 0;
}

static int text_logger_create(struct text_logger *logger)
{
	off_t pos;

	logger->fd =
		open(logger->log_filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (logger->fd < 0) {
		warn("Can't open text log file %s", logger->log_filename);
		return -1;
	}

	pos = lseek(logger->fd, 0, SEEK_END);
	if (pos < 0) {
		warn("Can't query text log position for file %s",
		     logger->log_filename);
		close(logger->fd);
		return -1;
	}

	logger->size = pos;
	logger->line_count =
		0; // Initialize line count for new or existing file

	if (logger->size >= logger->maxsize) {
		return text_logger_trim(logger);
	}

	return 0;
}

static struct handler *log_init(const struct handler_type *type
				__attribute__((unused)),
				struct console *console, struct config *config)
{
	struct log_handler *lh;
	const char *filename;
	const char *filename_timestamp;
	const char *logsize_str;
	int rc;
	int enable_timestamp_int = 0; // Initialize to 0

	lh = malloc(sizeof(*lh));
	if (!lh) {
		return NULL;
	}

	lh->console = console;
	lh->pagesize = 4096;
	lh->is_text_log_enabled = false;

	// Initialize binary_log fields
	lh->binary_log.fd = -1;
	lh->binary_log.log_filename = NULL;
	lh->binary_log.rotate_filename = NULL;

	// Initialize text_log fields
	lh->text_log.fd = -1;
	lh->text_log.log_filename = NULL;
	lh->text_log.rotate_filename = NULL;
	lh->text_log.ansi_st.mode = ANSI_NORMAL;
	lh->text_log.linebuf_len = 0;

	// Common logsize parsing
	logsize_str = config_get_value(config, "logsize");
	rc = config_parse_bytesize(logsize_str, &lh->binary_log.maxsize);
	if (logsize_str != NULL && rc) {
		lh->binary_log.maxsize = default_logsize;
		warn("Invalid logsize. Default to %ukB",
		     (unsigned int)(lh->binary_log.maxsize >> 10));
	}
	/* Ensure maxsize > pagesize to prevent immediate truncation */
	lh->binary_log.maxsize = lh->binary_log.maxsize <= lh->pagesize ?
					 lh->pagesize + 1 :
					 lh->binary_log.maxsize;
	lh->text_log.maxsize =
		lh->binary_log.maxsize; // Text log uses same maxsize

	// binary file setup
	filename = config_get_section_value(config, console->console_id,
					    "logfile");
	if (!filename && config_count_sections(config) == 0) {
		filename = config_get_value(config, "logfile");
	}
	if (!filename) {
		filename = default_filename;
	}
	lh->binary_log.log_filename = strdup(filename);
	rc = asprintf(&lh->binary_log.rotate_filename, "%s.1", filename);
	if (rc < 0) {
		warn("Failed to construct binary rotate filename");
		goto err_free;
	}

	rc = binary_logger_create(&lh->binary_log);
	if (rc < 0) {
		goto err_free;
	}

	// timestamp file setup
	const char *enable_timestamp_str =
		config_get_value(config, "log-timestamp");
	if (enable_timestamp_str != NULL &&
	    parse_binary_string(enable_timestamp_str, &enable_timestamp_int) ==
		    -1) {
		warn("log-timestamp only allows 0 and 1");
	} else {
		lh->is_text_log_enabled = (enable_timestamp_int == 1);
	}

	if (lh->is_text_log_enabled) {
		filename_timestamp = config_get_section_value(
			config, console->console_id, "logfile-timestamped");
		if (!filename_timestamp && config_count_sections(config) == 0) {
			filename_timestamp =
				config_get_value(config, "logfile-timestamped");
		}
		if (!filename_timestamp) {
			filename_timestamp = default_filename_timestamp;
		}
		lh->text_log.log_filename = strdup(filename_timestamp);
		rc = asprintf(&lh->text_log.rotate_filename, "%s.1",
			      filename_timestamp);
		if (rc < 0) {
			warn("Failed to construct text rotate filename");
			goto err_free;
		}

		rc = text_logger_create(&lh->text_log);
		if (rc < 0) {
			warn("timestamp log disabled due to open error");
			lh->is_text_log_enabled = false;
			free(lh->text_log.log_filename);
			free(lh->text_log.rotate_filename);
			lh->text_log.log_filename = NULL;
			lh->text_log.rotate_filename = NULL;
		}
	}

	lh->rbc = console_ringbuffer_consumer_register(console,
						       log_ringbuffer_poll, lh);
	return &lh->handler;

err_free:
	if (lh->binary_log.fd >= 0) {
		close(lh->binary_log.fd);
	}
	free(lh->binary_log.log_filename);
	free(lh->binary_log.rotate_filename);

	if (lh->text_log.fd >= 0) {
		close(lh->text_log.fd);
	}
	free(lh->text_log.log_filename);
	free(lh->text_log.rotate_filename);

	free(lh);
	return NULL;
}

static void log_fini(struct handler *handler)
{
	struct log_handler *lh = to_log_handler(handler);
	ringbuffer_consumer_unregister(lh->rbc);

	if (lh->binary_log.fd >= 0) {
		close(lh->binary_log.fd);
	}
	free(lh->binary_log.log_filename);
	free(lh->binary_log.rotate_filename);

	if (lh->is_text_log_enabled) {
		if (lh->text_log.fd >= 0) {
			close(lh->text_log.fd);
		}
		free(lh->text_log.log_filename);
		free(lh->text_log.rotate_filename);
	}

	free(lh);
}

static const struct handler_type log_handler = {
	.name = "log",
	.init = log_init,
	.fini = log_fini,
};

console_handler_register(&log_handler);
