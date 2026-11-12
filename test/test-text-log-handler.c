#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>

#include "../console-server.h"
#include "../text-log-handler.c"

/* Mock function to satisfy the linker */
struct ringbuffer_consumer *
console_ringbuffer_consumer_register(struct console *console,
				     ringbuffer_poll_fn_t poll_fn, void *data)
{
	(void)console;
	(void)poll_fn;
	(void)data;
	return NULL;
}

static void test_sanitize_to_ascii_ansi(void)
{
	struct ansi_state st;
	char out[128];
	size_t out_len;

	const char *in1 = "Hello, \x1b[31mWorld\x1b[0m!\nThis is a test.";
	st.mode = ANSI_NORMAL;
	out_len = sanitize_to_ascii_ansi((uint8_t *)in1, strlen(in1), out,
					 sizeof(out), &st);
	out[out_len] = '\0';
	assert(strcmp(out, "Hello, World!\nThis is a test.") == 0 &&
	       "sanitize_to_ascii_ansi failed for in1");

	/* Test with unterminated escape sequence */
	const char *in2 = "Partial\x1b[32";
	st.mode = ANSI_NORMAL;
	out_len = sanitize_to_ascii_ansi((uint8_t *)in2, strlen(in2), out,
					 sizeof(out), &st);
	out[out_len] = '\0';
	assert(strcmp(out, "Partial") == 0 &&
	       "sanitize_to_ascii_ansi failed for in2");

	/* Continue with the rest of the sequence */
	const char *in3 = "m and more text.";
	out_len = sanitize_to_ascii_ansi((uint8_t *)in3, strlen(in3), out,
					 sizeof(out), &st);
	out[out_len] = '\0';
	assert(strcmp(out, " and more text.") == 0 &&
	       "sanitize_to_ascii_ansi failed for in3 continuation");
	assert(st.mode == ANSI_NORMAL &&
	       "ANSI mode should be normal after completion");
}

static void test_generate_timestamp_text(void)
{
	char buf[TIMESTAMP_BUF_SIZE];
	ssize_t len;
	int line_count = 123;

	len = generate_timestamp_text(buf, sizeof(buf), line_count);
	assert(len > 0 &&
	       "Generated timestamp length should be greater than 0");

	/* Expected format: "Www Mmm dd hh:mm:ss yyyy 0000123 " */
	assert(strlen(buf) == 24 + 1 + 7 + 1 &&
	       "Generated timestamp length is incorrect"); /* date + space + number + space */
	assert(buf[len - 1] == ' ' &&
	       "Last character of timestamp should be space");
	assert(strstr(buf, " 0000123 ") != NULL &&
	       "Timestamp should contain line count");
}

static void test_text_log_rotation(void)
{
	struct text_log_handler lh;
	const char *log_file = "/tmp/test_text_log.log";
	const char *rotate_file = "/tmp/test_text_log.log.1";
	size_t maxsize = 100;
	uint8_t data[50];
	memset(data, 'B', sizeof(data));

	/* Clean up any previous test files */
	unlink(log_file);
	unlink(rotate_file);

	// Initialize log_handler
	lh.log_filename = strdup(log_file);
	lh.rotate_filename = strdup(rotate_file);
	lh.maxsize = maxsize;
	lh.pagesize = 4096;
	lh.size = 0;
	lh.line_count = 0;
	lh.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(lh.fd >= 0 && "Initial setup open failed");

	/* 1. First write, no rotation */
	int rc = text_logger_write_data(&lh, data, sizeof(data));
	assert(rc == 0 && "First text_logger_write_data call failed");
	assert(lh.size == 50 && "size after first write should be 50");

	/* 2. Second write, should trigger rotation */
	uint8_t data2[60];
	memset(data2, 'C', sizeof(data2));
	rc = text_logger_write_data(&lh, data2, sizeof(data2));
	assert(rc == 0 && "Second text_logger_write_data call failed");
	assert(lh.size == 60 &&
	       "size after second write should be 60 (after rotation and rewrite)");

	/* Check that rotated file exists and has the correct size */
	struct stat st;
	int stat_rc = stat(rotate_file, &st);
	assert(stat_rc == 0 && "stat failed for rotate_file");
	assert(st.st_size == 50 && "rotated file size should be 50");

	close(lh.fd);
	free(lh.log_filename);
	free(lh.rotate_filename);
	unlink(log_file);
	unlink(rotate_file);
}

static void test_text_log_truncation(void)
{
	struct text_log_handler lh;
	const char *log_file = "/tmp/test_text_trunc.log";
	const char *rotate_file = "/tmp/test_text_trunc.log.1";
	size_t maxsize = 10;
	uint8_t data[20];
	memset(data, 'C', sizeof(data));
	data[19] = 'D'; // Last byte is different

	unlink(log_file);
	unlink(rotate_file);

	lh.log_filename = strdup(log_file);
	lh.rotate_filename = strdup(rotate_file);
	lh.maxsize = maxsize;
	lh.pagesize = 4096;
	lh.size = 0;
	lh.line_count = 0;
	lh.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	/* Write 20 bytes when maxsize is 10. Should truncate to last 10 bytes. */
	int rc = text_logger_write_data(&lh, data, sizeof(data));
	assert(rc == 0 && "Truncation write failed");
	assert(lh.size == 10 && "Size should be truncated to maxsize");

	/* Verify file content contains the last byte 'D' */
	close(lh.fd);
	lh.fd = open(log_file, O_RDONLY);
	uint8_t read_buf[10];
	ssize_t n = read(lh.fd, read_buf, 10);
	assert(n == 10 && "Should read 10 bytes");
	assert(read_buf[9] == 'D' &&
	       "File should contain the tail of the data");

	close(lh.fd);
	free(lh.log_filename);
	free(lh.rotate_filename);
	unlink(log_file);
	unlink(rotate_file);
}

static void test_flush_line_rotation_keeps_record_together(void)
{
	struct text_log_handler lh = { 0 };
	enum {
		EXISTING_SIZE = 190,
		LINE_SIZE = 64,
		ACTIVE_SIZE = 256,
	};
	char temp_dir[] = "/tmp/test-text-log-rotation-XXXXXX";
	char log_file[128];
	char rotate_file[128];
	char filler[EXISTING_SIZE];
	char ts_buf[TIMESTAMP_BUF_SIZE];
	char active_buf[ACTIVE_SIZE];
	struct stat st;
	ssize_t ts_len;
	int rc;
	int stat_rc;

	if (mkdtemp(temp_dir) == NULL) {
		perror("mkdtemp");
		abort();
	}
	snprintf(log_file, sizeof(log_file), "%s/log", temp_dir);
	snprintf(rotate_file, sizeof(rotate_file), "%s/log.1", temp_dir);
	memset(filler, 'F', sizeof(filler));

	lh.log_filename = strdup(log_file);
	lh.rotate_filename = strdup(rotate_file);
	lh.maxsize = sizeof(active_buf);
	lh.pagesize = 4096;
	lh.size = 0;
	lh.line_count = 41;
	lh.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(lh.fd >= 0 && "Initial setup open failed");
	rc = write_buf_to_fd(lh.fd, (uint8_t *)filler, sizeof(filler));
	assert(rc == 0 && "Failed to write existing log contents");
	lh.size = sizeof(filler);
	memset(lh.linebuf, 'Z', LINE_SIZE);
	lh.linebuf_len = LINE_SIZE;
	ts_len = generate_timestamp_text(ts_buf, sizeof(ts_buf), 0);
	assert(ts_len > 0 && "Failed to generate expected timestamp");

	rc = flush_line(&lh);
	assert(rc == 0 && "flush_line failed at the rotation boundary");
	assert(lh.line_count == 1 &&
	       "Line count should restart after rotation");
	assert(lh.linebuf_len == 0 &&
	       "Line buffer should be cleared after flush");
	assert(lh.size == (size_t)ts_len + LINE_SIZE + 1 &&
	       "The complete record should be written to the new log");

	/* A following record must continue from the reset line count. */
	lh.linebuf[0] = 'Q';
	lh.linebuf_len = 1;
	rc = flush_line(&lh);
	assert(rc == 0 && "Second flush_line failed");
	assert(lh.line_count == 2 &&
	       "Line count should advance after each flush");

	close(lh.fd);
	stat_rc = stat(rotate_file, &st);
	assert(stat_rc == 0 && "Rotated log was not created");
	assert(st.st_size == (off_t)EXISTING_SIZE &&
	       "Rotated log must not contain a partial new record");

	lh.fd = open(log_file, O_RDONLY);
	assert(lh.fd >= 0 && "Could not open active log for verification");
	ssize_t n = read(lh.fd, active_buf, sizeof(active_buf) - 1);
	assert(n == (ssize_t)((size_t)ts_len + LINE_SIZE + 1 + (size_t)ts_len +
			      1 + 1) &&
	       "Active log should contain both complete records");
	active_buf[n] = '\0';
	assert(strstr(active_buf, " 0000000 ") != NULL &&
	       "First line in the new log should use line count zero");
	assert(strstr(active_buf, " 0000001 ") != NULL &&
	       "Second line in the new log should use line count one");
	assert(memcmp(active_buf + ts_len, "ZZZZZZZZ", 8) == 0 &&
	       "First record contents were not preserved");
	assert(active_buf[ts_len + LINE_SIZE] == '\n' &&
	       "First record should end with a newline");

	close(lh.fd);
	free(lh.log_filename);
	free(lh.rotate_filename);
	unlink(log_file);
	unlink(rotate_file);
	rmdir(temp_dir);
}

static void test_flush_line_empty_and_full_buffer(void)
{
	struct text_log_handler lh = { 0 };
	char temp_dir[] = "/tmp/test-text-log-boundaries-XXXXXX";
	char log_file[128];
	char rotate_file[128];
	const char *input = "A\n\nB\n";
	char ts_buf[TIMESTAMP_BUF_SIZE];
	char active_buf[16384];
	struct stat st;
	size_t newline_count = 0;
	size_t read_len = 0;
	ssize_t ts_len;
	int rc;
	int stat_rc;

	if (mkdtemp(temp_dir) == NULL) {
		perror("mkdtemp");
		abort();
	}
	snprintf(log_file, sizeof(log_file), "%s/log", temp_dir);
	snprintf(rotate_file, sizeof(rotate_file), "%s/log.1", temp_dir);
	lh.log_filename = strdup(log_file);
	lh.rotate_filename = strdup(rotate_file);
	lh.maxsize = sizeof(active_buf);
	lh.pagesize = 4096;
	lh.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(lh.fd >= 0 && "Initial setup open failed");

	for (const char *p = input; *p != '\0'; p++) {
		rc = append_char_and_maybe_flush(&lh, *p);
		assert(rc == 0 && "Failed to flush ordinary or empty line");
	}
	assert(lh.line_count == 3 &&
	       "Ordinary and empty lines should be counted");

	for (size_t i = 0; i < sizeof(lh.linebuf); i++) {
		rc = append_char_and_maybe_flush(&lh, 'X');
		assert(rc == 0 && "Failed while filling the line buffer");
	}
	assert(lh.line_count == 4 && "Full line buffer should trigger a flush");
	assert(lh.linebuf_len == 1 && lh.linebuf[0] == 'X' &&
	       "The byte after a full line should remain buffered");
	rc = flush_line(&lh);
	assert(rc == 0 && "Failed to flush the remaining buffered byte");
	assert(lh.line_count == 5 &&
	       "Final line count should include all records");

	ts_len = generate_timestamp_text(ts_buf, sizeof(ts_buf), 3);
	assert(ts_len > 0 && "Failed to generate expected timestamp");
	close(lh.fd);
	stat_rc = stat(log_file, &st);
	assert(stat_rc == 0 && "Active log was not created");
	assert(st.st_size < (off_t)sizeof(active_buf) &&
	       "Active log should fit in the verification buffer");

	lh.fd = open(log_file, O_RDONLY);
	assert(lh.fd >= 0 && "Could not open active log for verification");
	while (read_len < sizeof(active_buf) - 1) {
		ssize_t n = read(lh.fd, active_buf + read_len,
				 sizeof(active_buf) - 1 - read_len);
		if (n <= 0) {
			break;
		}
		read_len += (size_t)n;
	}
	assert(read_len == (size_t)st.st_size &&
	       "Could not read the complete active log");
	active_buf[read_len] = '\0';
	for (size_t i = 0; i < read_len; i++) {
		if (active_buf[i] == '\n') {
			newline_count++;
		}
	}
	assert(newline_count == 5 &&
	       "Each flushed record should end with a newline");
	assert(strstr(active_buf, " 0000000 A\n") != NULL &&
	       "First ordinary line was not recorded");
	assert(strstr(active_buf, " 0000001 \n") != NULL &&
	       "Empty line was not recorded");
	assert(strstr(active_buf, " 0000002 B\n") != NULL &&
	       "Third ordinary line was not recorded");
	assert(strstr(active_buf, " 0000004 X\n") != NULL &&
	       "Buffered byte after the full line was not recorded");

	char *long_marker = strstr(active_buf, " 0000003 ");
	assert(long_marker != NULL && "Full-buffer line was not recorded");
	size_t marker_offset = (size_t)(long_marker - active_buf);
	assert(marker_offset >= (size_t)ts_len - 9 &&
	       "Timestamp start is outside the log");
	char *long_start = active_buf + marker_offset - ((size_t)ts_len - 9);
	for (size_t i = 0; i < sizeof(lh.linebuf) - 1; i++) {
		assert(long_start[ts_len + i] == 'X' &&
		       "Full-buffer line contents were not preserved");
	}
	assert(long_start[ts_len + sizeof(lh.linebuf) - 1] == '\n' &&
	       "Full-buffer line should end with a newline");

	close(lh.fd);
	free(lh.log_filename);
	free(lh.rotate_filename);
	unlink(log_file);
	unlink(rotate_file);
	rmdir(temp_dir);
}

static void test_flush_line_oversize_record_preserves_timestamp(void)
{
	struct text_log_handler lh = { 0 };
	enum {
		EXISTING_SIZE = 100,
		MAX_SIZE = 8192,
	};
	char temp_dir[] = "/tmp/test-text-log-oversize-XXXXXX";
	char log_file[128];
	char rotate_file[128];
	char filler[EXISTING_SIZE];
	char active_buf[MAX_SIZE + 1];
	char ts_buf[TIMESTAMP_BUF_SIZE];
	struct stat st;
	ssize_t ts_len;
	ssize_t read_len;
	size_t total_read = 0;
	size_t original_payload_len;
	size_t payload_len;
	size_t payload_offset;
	int rc;
	int stat_rc;

	if (mkdtemp(temp_dir) == NULL) {
		perror("mkdtemp");
		abort();
	}
	snprintf(log_file, sizeof(log_file), "%s/log", temp_dir);
	snprintf(rotate_file, sizeof(rotate_file), "%s/log.1", temp_dir);
	memset(filler, 'F', sizeof(filler));

	lh.log_filename = strdup(log_file);
	lh.rotate_filename = strdup(rotate_file);
	lh.maxsize = MAX_SIZE;
	lh.pagesize = 4096;
	lh.line_count = 12;
	lh.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(lh.fd >= 0 && "Initial setup open failed");
	rc = write_buf_to_fd(lh.fd, (uint8_t *)filler, sizeof(filler));
	assert(rc == 0 && "Failed to write existing log contents");
	lh.size = sizeof(filler);
	for (size_t i = 0; i < sizeof(lh.linebuf) - 1; i++) {
		lh.linebuf[i] = (char)('A' + i % 26);
	}
	lh.linebuf_len = sizeof(lh.linebuf) - 1;
	original_payload_len = lh.linebuf_len;
	ts_len = generate_timestamp_text(ts_buf, sizeof(ts_buf), 0);
	assert(ts_len > 0 && "Failed to generate expected timestamp");

	rc = flush_line(&lh);
	assert(rc == 0 && "flush_line failed for a record larger than maxsize");
	assert(lh.line_count == 1 &&
	       "Line count should restart after rotation");
	assert(lh.size == MAX_SIZE &&
	       "Oversize record should remain within maxsize");

	close(lh.fd);
	stat_rc = stat(rotate_file, &st);
	assert(stat_rc == 0 && "Rotated log was not created");
	assert(st.st_size == (off_t)EXISTING_SIZE &&
	       "Rotated log should contain only the previous contents");

	lh.fd = open(log_file, O_RDONLY);
	assert(lh.fd >= 0 && "Could not open active log for verification");
	while (total_read < MAX_SIZE) {
		read_len = read(lh.fd, active_buf + total_read,
				MAX_SIZE - total_read);
		if (read_len <= 0) {
			break;
		}
		total_read += (size_t)read_len;
	}
	assert(total_read == MAX_SIZE &&
	       "Active log size should equal maxsize");
	active_buf[total_read] = '\0';
	assert(strstr(active_buf, " 0000000 ") == active_buf + ts_len - 9 &&
	       "Oversize record must retain timestamp and first line number");
	payload_len = MAX_SIZE - (size_t)ts_len - 1;
	payload_offset = original_payload_len - payload_len;
	for (size_t i = 0; i < payload_len; i++) {
		assert(active_buf[(size_t)ts_len + i] ==
			       lh.linebuf[payload_offset + i] &&
		       "Payload tail was not preserved");
	}
	assert(active_buf[MAX_SIZE - 1] == '\n' &&
	       "Oversize record should end with a newline");

	close(lh.fd);
	free(lh.log_filename);
	free(lh.rotate_filename);
	unlink(log_file);
	unlink(rotate_file);
	rmdir(temp_dir);
}

static void test_flush_line_exact_boundary_and_empty_record_rotation(void)
{
	struct text_log_handler lh = { 0 };
	enum {
		EXISTING_SIZE = 158,
		LINE_SIZE = 64,
		MAX_SIZE = 256,
	};
	char temp_dir[] = "/tmp/test-text-log-exact-boundary-XXXXXX";
	char log_file[128];
	char rotate_file[128];
	char filler[EXISTING_SIZE];
	char expected_ts[TIMESTAMP_BUF_SIZE];
	char active_buf[65];
	struct stat st;
	ssize_t ts_len;
	ssize_t n;
	int rc;
	int stat_rc;

	if (mkdtemp(temp_dir) == NULL) {
		perror("mkdtemp");
		abort();
	}
	snprintf(log_file, sizeof(log_file), "%s/log", temp_dir);
	snprintf(rotate_file, sizeof(rotate_file), "%s/log.1", temp_dir);
	memset(filler, 'F', sizeof(filler));

	lh.log_filename = strdup(log_file);
	lh.rotate_filename = strdup(rotate_file);
	lh.maxsize = MAX_SIZE;
	lh.pagesize = 4096;
	lh.line_count = 17;
	lh.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(lh.fd >= 0 && "Initial setup open failed");
	rc = write_buf_to_fd(lh.fd, (uint8_t *)filler, sizeof(filler));
	assert(rc == 0 && "Failed to write existing log contents");
	lh.size = sizeof(filler);
	memset(lh.linebuf, 'Y', LINE_SIZE);
	lh.linebuf_len = LINE_SIZE;
	ts_len = generate_timestamp_text(expected_ts, sizeof(expected_ts),
					 lh.line_count);
	assert(ts_len > 0 && "Failed to generate expected timestamp");
	assert(lh.size + (size_t)ts_len + LINE_SIZE + 1 == MAX_SIZE &&
	       "Test record should exactly fill the active log");

	rc = flush_line(&lh);
	assert(rc == 0 && "flush_line failed at the exact size boundary");
	assert(lh.size == MAX_SIZE &&
	       "Exact boundary should fill the active log");
	assert(lh.line_count == 18 &&
	       "Line count should advance without rotation");
	assert(access(rotate_file, F_OK) < 0 && errno == ENOENT &&
	       "Exact boundary should not rotate the active log");

	/* An empty line that follows must rotate as one timestamped record. */
	lh.linebuf_len = 0;
	rc = flush_line(&lh);
	assert(rc == 0 && "flush_line failed for an empty record at full size");
	assert(lh.line_count == 1 && "Rotation should reset the line count");
	assert(lh.size == (size_t)ts_len + 1 &&
	       "Empty record should fit in the new active log");

	close(lh.fd);
	stat_rc = stat(rotate_file, &st);
	assert(stat_rc == 0 && "Full active log was not rotated");
	assert(st.st_size == MAX_SIZE &&
	       "Rotated log should end at the exact boundary");

	lh.fd = open(log_file, O_RDONLY);
	assert(lh.fd >= 0 && "Could not open active log for verification");
	n = read(lh.fd, active_buf, sizeof(active_buf));
	assert(n == ts_len + 1 &&
	       "Active log should contain one complete empty record");
	active_buf[n] = '\0';
	assert(strstr(active_buf, " 0000000 ") == active_buf + ts_len - 9 &&
	       "Empty record should start with the reset line number");
	assert(active_buf[ts_len] == '\n' &&
	       "Empty record should end with a newline");

	close(lh.fd);
	free(lh.log_filename);
	free(lh.rotate_filename);
	unlink(log_file);
	unlink(rotate_file);
	rmdir(temp_dir);
}

int main(void)
{
	test_sanitize_to_ascii_ansi();
	test_generate_timestamp_text();
	test_text_log_rotation();
	test_text_log_truncation();
	test_flush_line_rotation_keeps_record_together();
	test_flush_line_empty_and_full_buffer();
	test_flush_line_oversize_record_preserves_timestamp();
	test_flush_line_exact_boundary_and_empty_record_rotation();
	return EXIT_SUCCESS;
}
