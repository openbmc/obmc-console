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

#include "../console-server.h" // For struct console, enum ringbuffer_poll_ret, and struct ringbuffer_consumer
#include "../log-handler.c"

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

static void test_prevent_log_data_exceeded_maxsize(void)
{
	size_t maxsize = 100;
	uint8_t buffer[200];
	uint8_t *buf = buffer;
	size_t len = 150;

	prevent_log_data_exceeded_maxsize(&buf, &len, maxsize);

	assert(len == 100 && "len should be 100");
	assert(buf == buffer + 50 && "buf pointer should be moved by 50");
}

static void test_parse_binary_string(void)
{
	int out;

	assert(parse_binary_string("1", &out) == 0 && out == 1 &&
	       "parse_binary_string('1') should return 0 and out=1");
	assert(parse_binary_string("0", &out) == 0 && out == 0 &&
	       "parse_binary_string('0') should return 0 and out=0");

	/* Invalid inputs */
	assert(parse_binary_string("2", &out) == -1 &&
	       "parse_binary_string('2') should return -1");
	assert(parse_binary_string("-1", &out) == -1 &&
	       "parse_binary_string('-1') should return -1");
	assert(parse_binary_string("abc", &out) == -1 &&
	       "parse_binary_string('abc') should return -1");
	assert(parse_binary_string(" 1", &out) == -1 &&
	       "parse_binary_string(' 1') should return -1");
	assert(parse_binary_string("1 ", &out) == -1 &&
	       "parse_binary_string('1 ') should return -1");
	assert(parse_binary_string("", &out) == -1 &&
	       "parse_binary_string('') should return -1");
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

static void test_binary_log_rotation(void)
{
	struct binary_logger logger;
	const char *log_file = "/tmp/test_binary_log.log";
	const char *rotate_file = "/tmp/test_binary_log.log.1";
	size_t maxsize = 100;
	uint8_t data[50];
	memset(data, 'A', sizeof(data));

	/* Clean up any previous test files */
	unlink(log_file);
	unlink(rotate_file);

	// Initialize logger
	logger.log_filename = strdup(log_file);
	logger.rotate_filename = strdup(rotate_file);
	logger.maxsize = maxsize;
	logger.size = 0;
	logger.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(logger.fd >= 0 && "Initial setup open failed");

	/* 1. First write, no rotation */
	int rc = binary_logger_write_data(&logger, data, sizeof(data));
	assert(rc == 0 && "First binary_logger_write_data call failed");
	assert(logger.size == 50 && "size after first write should be 50");

	/* 2. Second write, should trigger rotation */
	rc = binary_logger_write_data(&logger, data, sizeof(data));
	assert(rc == 0 && "Second binary_logger_write_data call failed");
	assert(logger.size == 50 &&
	       "size after second write should be 50 (after rotation and rewrite)");

	/* 3. Third write, should trigger rotation */
	rc = binary_logger_write_data(&logger, data, sizeof(data));
	assert(rc == 0 && "Third binary_logger_write_data call failed");
	assert(logger.size == 50 && "size after third write should be 50");

	/* Check that rotated file exists and has the correct size */
	struct stat st;
	int stat_rc = stat(rotate_file, &st);
	assert(stat_rc == 0 && "stat failed for rotate_file");
	assert(st.st_size == 50 && "rotated file size should be 50");

	close(logger.fd);
	free(logger.log_filename);
	free(logger.rotate_filename);
	unlink(log_file);
	unlink(rotate_file);
}

static void test_text_log_rotation(void)
{
	struct text_logger logger;
	const char *log_file = "/tmp/test_text_log.log";
	const char *rotate_file = "/tmp/test_text_log.log.1";
	size_t maxsize = 100;
	uint8_t data[50];
	memset(data, 'B', sizeof(data));

	/* Clean up any previous test files */
	unlink(log_file);
	unlink(rotate_file);

	// Initialize logger
	logger.log_filename = strdup(log_file);
	logger.rotate_filename = strdup(rotate_file);
	logger.maxsize = maxsize;
	logger.size = 0;
	logger.line_count = 0;
	logger.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(logger.fd >= 0 && "Initial setup open failed");

	/* 1. First write, no rotation */
	int rc = text_logger_write_data(&logger, data, sizeof(data));
	assert(rc == 0 && "First text_logger_write_data call failed");
	assert(logger.size == 50 && "size after first write should be 50");
	assert(logger.line_count == 0 && "line_count should still be 0");

	/* 2. Second write, should trigger rotation */
	rc = text_logger_write_data(&logger, data, sizeof(data));
	assert(rc == 0 && "Second text_logger_write_data call failed");
	assert(logger.size == 50 &&
	       "size after second write should be 50 (after rotation and rewrite)");
	assert(logger.line_count == 0 && "line_count should be reset to 0");

	/* Check that rotated file exists and has the correct size */
	struct stat st;
	int stat_rc = stat(rotate_file, &st);
	assert(stat_rc == 0 && "stat failed for rotate_file");
	assert(st.st_size == 50 && "rotated file size should be 50");

	close(logger.fd);
	free(logger.log_filename);
	free(logger.rotate_filename);
	unlink(log_file);
	unlink(rotate_file);
}

int main(void)
{
	test_prevent_log_data_exceeded_maxsize();
	test_parse_binary_string();
	test_sanitize_to_ascii_ansi();
	test_generate_timestamp_text();
	test_binary_log_rotation();
	test_text_log_rotation();

	return EXIT_SUCCESS;
}
