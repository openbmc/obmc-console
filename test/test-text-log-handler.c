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
#include <locale.h>

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

static void test_no_split_on_rotation(void)
{
	struct text_log_handler lh;
	const char *log_file = "/tmp/test_no_split.log";
	const char *rotate_file = "/tmp/test_no_split.log.1";
	unlink(log_file);
	unlink(rotate_file);

	/* Setup handler with small maxsize so one entry causes rotation */
	lh.log_filename = strdup(log_file);
	lh.rotate_filename = strdup(rotate_file);
	lh.maxsize = 50;
	lh.pagesize = 4096;
	lh.size = 0;
	lh.line_count = 0;
	lh.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(lh.fd >= 0 && "open failed");

	/* Prefill with 20 bytes so adding a ~39-byte entry forces rotation first */
	uint8_t prefill[20];
	memset(prefill, 'A', sizeof(prefill));
	int rc = text_logger_write_data(&lh, prefill, sizeof(prefill));
	assert(rc == 0 && lh.size == 20);

	/* Prepare a short line and flush it as a single entry */
	memcpy(lh.linebuf, "hello", 5);
	lh.linebuf_len = 5;

	rc = flush_line(&lh);
	assert(rc == 0);

	/* Rotated file should have only the old content (20 'A's) */
	struct stat st_rot = { 0 };
	int s = stat(rotate_file, &st_rot);
	assert(s == 0 && st_rot.st_size == 20);

	int fr = open(rotate_file, O_RDONLY);
	assert(fr >= 0);
	uint8_t checkA[20];
	ssize_t nr = read(fr, checkA, sizeof(checkA));
	assert(nr == 20);
	for (int i = 0; i < 20; i++) {
		assert(checkA[i] == 'A');
	}
	close(fr);

	/* New log file should start with the full entry (timestamp + "hello\n")
	   Compute expected timestamp length for line_count 0 */
	char ts_buf[TIMESTAMP_BUF_SIZE];
	ssize_t ts_len = generate_timestamp_text(ts_buf, sizeof(ts_buf), 0);
	assert(ts_len > 0);
	size_t expected = (size_t)ts_len + 5 + 1;

	struct stat st_log = { 0 };
	s = stat(log_file, &st_log);
	assert(s == 0 && (size_t)st_log.st_size == expected);

	int fl = open(log_file, O_RDONLY);
	assert(fl >= 0);
	char *full = malloc(expected);
	nr = read(fl, full, expected);
	assert(nr == (ssize_t)expected);
	/* Verify it ends with "hello\n" */
	assert(expected >= 6);
	assert(memcmp(full + expected - 6, "hello\n", 6) == 0);
	close(fl);
	free(full);

	close(lh.fd);
	free(lh.log_filename);
	free(lh.rotate_filename);
	unlink(log_file);
	unlink(rotate_file);
}

static void test_persist_line_counter(void)
{
	struct text_log_handler lh1;
	const char *log_file = "/tmp/test_persist.log";
	const char *rotate_file = "/tmp/test_persist.log.1";
	unlink(log_file);
	unlink(rotate_file);

	lh1.log_filename = strdup(log_file);
	lh1.rotate_filename = strdup(rotate_file);
	lh1.maxsize = (size_t)(64 * 1024);
	lh1.pagesize = 4096;
	lh1.size = 0;
	lh1.line_count = 0;
	lh1.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(lh1.fd >= 0);

	memcpy(lh1.linebuf, "first", 5);
	lh1.linebuf_len = 5;
	assert(flush_line(&lh1) == 0);
	memcpy(lh1.linebuf, "second", 6);
	lh1.linebuf_len = 6;
	assert(flush_line(&lh1) == 0);
	close(lh1.fd);
	free(lh1.log_filename);
	free(lh1.rotate_filename);

	/* Simulate a new process with fresh handler; it should pick up last count */
	struct text_log_handler lh2;
	lh2.log_filename = strdup(log_file);
	lh2.rotate_filename = strdup(rotate_file);
	lh2.maxsize = (size_t)(64 * 1024);
	lh2.pagesize = 4096;
	lh2.line_count = 0;
	lh2.fd = open(log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
	assert(lh2.fd >= 0);
	/* Determine current size */
	off_t pos = lseek(lh2.fd, 0, SEEK_END);
	lh2.size = (size_t)pos;

	/* Initialize persistent counter from existing file */
	init_persistent_line_count(&lh2);
	memcpy(lh2.linebuf, "third", 5);
	lh2.linebuf_len = 5;
	assert(flush_line(&lh2) == 0);
	close(lh2.fd);

	/* Read back last line and check it contains " 0000002 " */
	int fd = open(log_file, O_RDONLY);
	assert(fd >= 0);
	struct stat st = { 0 };
	assert(fstat(fd, &st) == 0);
	size_t to_read = st.st_size < 4096 ? st.st_size : 4096;
	char *buf = malloc(to_read + 1);
	assert(buf);
	ssize_t n = pread(fd, buf, to_read, (off_t)(st.st_size - to_read));
	assert(n == (ssize_t)to_read);
	buf[n] = '\0';
	assert(strstr(buf, " 0000002 ") != NULL);
	free(buf);
	close(fd);

	unlink(log_file);
	unlink(rotate_file);
	free(lh2.log_filename);
	free(lh2.rotate_filename);
}

static void test_persist_line_counter_large(void)
{
	struct text_log_handler lh2;
	const char *log_file = "/tmp/test_persist_large.log";
	const char *rotate_file = "/tmp/test_persist_large.log.1";
	unlink(log_file);
	unlink(rotate_file);

	/* Seed file with a single line having a large counter (>= 10,000,000) */
	int fdw = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(fdw >= 0);
	char ts_buf[TIMESTAMP_BUF_SIZE];
	ssize_t ts_len =
		generate_timestamp_text(ts_buf, sizeof(ts_buf), 10000000);
	assert(ts_len > 0);
	const char *msg = "seed\n";
	ssize_t wr = write(fdw, ts_buf, (size_t)ts_len);
	assert(wr == ts_len);
	wr = write(fdw, msg, strlen(msg));
	assert(wr == (ssize_t)strlen(msg));
	close(fdw);

	/* Fresh handler should read last counter and set next value */
	lh2.log_filename = strdup(log_file);
	lh2.rotate_filename = strdup(rotate_file);
	lh2.maxsize = (size_t)(64 * 1024);
	lh2.pagesize = 4096;
	lh2.line_count = 0;
	lh2.fd = open(log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
	assert(lh2.fd >= 0);
	off_t pos = lseek(lh2.fd, 0, SEEK_END);
	lh2.size = (size_t)pos;

	init_persistent_line_count(&lh2);
	memcpy(lh2.linebuf, "next", 4);
	lh2.linebuf_len = 4;
	assert(flush_line(&lh2) == 0);
	close(lh2.fd);

	int fdr = open(log_file, O_RDONLY);
	assert(fdr >= 0);
	struct stat st = { 0 };
	assert(fstat(fdr, &st) == 0);
	size_t to_read = st.st_size < 4096 ? st.st_size : 4096;
	char *buf = malloc(to_read + 1);
	assert(buf);
	ssize_t n = pread(fdr, buf, to_read, (off_t)(st.st_size - to_read));
	assert(n == (ssize_t)to_read);
	buf[n] = '\0';
	assert(strstr(buf, " 10000001 ") != NULL);
	free(buf);
	close(fdr);

	unlink(log_file);
	unlink(rotate_file);
	free(lh2.log_filename);
	free(lh2.rotate_filename);
}

static void test_persist_line_counter_skip_malformed_tail(void)
{
	struct text_log_handler lh1;
	const char *log_file = "/tmp/test_persist_skip.log";
	const char *rotate_file = "/tmp/test_persist_skip.log.1";
	unlink(log_file);
	unlink(rotate_file);

	lh1.log_filename = strdup(log_file);
	lh1.rotate_filename = strdup(rotate_file);
	lh1.maxsize = (size_t)(64 * 1024);
	lh1.pagesize = 4096;
	lh1.size = 0;
	lh1.line_count = 0;
	lh1.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(lh1.fd >= 0);

	memcpy(lh1.linebuf, "first", 5);
	lh1.linebuf_len = 5;
	assert(flush_line(&lh1) == 0);
	memcpy(lh1.linebuf, "second", 6);
	lh1.linebuf_len = 6;
	assert(flush_line(&lh1) == 0);
	close(lh1.fd);
	free(lh1.log_filename);
	free(lh1.rotate_filename);

	/* Append a malformed last line without timestamp */
	int fa = open(log_file, O_WRONLY | O_APPEND);
	assert(fa >= 0);
	const char *bad = "notimestamp tail\n";
	ssize_t wr = write(fa, bad, strlen(bad));
	assert(wr == (ssize_t)strlen(bad));
	close(fa);

	/* New handler should skip malformed tail and recover from previous line (counter 1) */
	struct text_log_handler lh2;
	lh2.log_filename = strdup(log_file);
	lh2.rotate_filename = strdup(rotate_file);
	lh2.maxsize = (size_t)(64 * 1024);
	lh2.pagesize = 4096;
	lh2.line_count = 0;
	lh2.fd = open(log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
	assert(lh2.fd >= 0);
	off_t pos = lseek(lh2.fd, 0, SEEK_END);
	lh2.size = (size_t)pos;

	init_persistent_line_count(&lh2);
	/* Next should be 2 */
	memcpy(lh2.linebuf, "third", 5);
	lh2.linebuf_len = 5;
	assert(flush_line(&lh2) == 0);
	close(lh2.fd);

	int fd = open(log_file, O_RDONLY);
	assert(fd >= 0);
	struct stat st = { 0 };
	assert(fstat(fd, &st) == 0);
	size_t to_read = st.st_size < 4096 ? st.st_size : 4096;
	char *buf = malloc(to_read + 1);
	assert(buf);
	ssize_t n = pread(fd, buf, to_read, (off_t)(st.st_size - to_read));
	assert(n == (ssize_t)to_read);
	buf[n] = '\0';
	assert(strstr(buf, " 0000002 ") != NULL);
	free(buf);
	close(fd);

	unlink(log_file);
	unlink(rotate_file);
	free(lh2.log_filename);
	free(lh2.rotate_filename);
}

int main(void)
{
	setlocale(LC_TIME, "C");
	test_sanitize_to_ascii_ansi();
	test_generate_timestamp_text();
	test_text_log_rotation();
	test_text_log_truncation();
	test_no_split_on_rotation();
	test_persist_line_counter();
	test_persist_line_counter_large();
	test_persist_line_counter_skip_malformed_tail();
	return EXIT_SUCCESS;
}
