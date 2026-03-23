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

int main(void)
{
	test_sanitize_to_ascii_ansi();
	test_generate_timestamp_text();
	test_text_log_rotation();
	test_text_log_truncation();
	return EXIT_SUCCESS;
}
