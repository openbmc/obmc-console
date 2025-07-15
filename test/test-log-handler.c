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

static void test_log_rotation(void)
{
	const char *log_file = "/tmp/test_log.log";	 // Changed to /tmp
	const char *rotate_file = "/tmp/test_log.log.1"; // Changed to /tmp
	size_t size = 0;
	int line_count = 0;
	size_t maxsize = 100;
	uint8_t data[50];
	memset(data, 'A', sizeof(data));

	/* Clean up any previous test files */
	unlink(log_file);
	unlink(rotate_file);

	// Initial setup: ensure the log_file exists, log_data will open/manage fds
	int setup_fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(setup_fd >= 0 && "Initial setup open failed");
	close(setup_fd); // Close it, log_data will manage its own fds now.

	/* 1. First write, no rotation */
	int fd_for_call_1 =
		open(log_file, O_WRONLY | O_APPEND, 0644); // Open before call
	assert(fd_for_call_1 >= 0 && "Open for first log_data call failed");
	int log_data_rc = log_data(&fd_for_call_1, &size, maxsize, log_file,
				   rotate_file, data, sizeof(data),
				   &line_count);
	assert(log_data_rc == 0 && "First log_data call failed");
	struct stat st_temp_1;
	assert(fstat(fd_for_call_1, &st_temp_1) == 0 &&
	       "fstat failed after first log_data");
	size = st_temp_1.st_size;
	close(fd_for_call_1); // Close after call and fstat
	assert(size == 50 && "size after first write should be 50");

	/* 2. Second write, should trigger rotation */
	int fd_for_call_2 =
		open(log_file, O_WRONLY | O_APPEND, 0644); // Open before call
	assert(fd_for_call_2 >= 0 && "Open for second log_data call failed");
	log_data_rc = log_data(&fd_for_call_2, &size, maxsize, log_file,
			       rotate_file, data, sizeof(data), &line_count);
	assert(log_data_rc == 0 && "Second log_data call failed");
	struct stat st_temp_2;
	assert(fstat(fd_for_call_2, &st_temp_2) == 0 &&
	       "fstat failed after second log_data");
	size = st_temp_2.st_size;
	close(fd_for_call_2); // Close after call and fstat
	assert(size == 50 &&
	       "size after second write should be 50 (after rotation and rewrite)");

	/* 3. Third write, should trigger rotation */
	int fd_for_call_3 =
		open(log_file, O_WRONLY | O_APPEND, 0644); // Open before call
	assert(fd_for_call_3 >= 0 && "Open for third log_data call failed");
	log_data_rc = log_data(&fd_for_call_3, &size, maxsize, log_file,
			       rotate_file, data, sizeof(data), &line_count);
	assert(log_data_rc == 0 && "Third log_data call failed");
	struct stat st_temp_3;
	assert(fstat(fd_for_call_3, &st_temp_3) == 0 &&
	       "fstat failed after third log_data");
	size = st_temp_3.st_size;
	close(fd_for_call_3); // Close after call and fstat
	assert(size == 50 && "size after third write should be 50");

	/* Check that rotated file exists and has the correct size */
	struct stat st;
	int stat_rc = stat(rotate_file, &st);
	assert(stat_rc == 0 && "stat failed for rotate_file");
	assert(st.st_size == 50 && "rotated file size should be 50");

	unlink(log_file);
	unlink(rotate_file);
}


int main(void)
{
	test_prevent_log_data_exceeded_maxsize();
	test_parse_binary_string();
	test_sanitize_to_ascii_ansi();
	test_generate_timestamp_text();
	test_log_rotation();

	return EXIT_SUCCESS;
}
