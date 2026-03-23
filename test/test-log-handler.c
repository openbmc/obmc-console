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

static void test_log_rotation(void)
{
	struct log_handler lh;
	const char *log_file = "/tmp/test_binary_log.log";
	const char *rotate_file = "/tmp/test_binary_log.log.1";
	size_t maxsize = 100;
	uint8_t data[50];
	memset(data, 'A', sizeof(data));

	/* Clean up any previous test files */
	unlink(log_file);
	unlink(rotate_file);

	// Initialize log_handler
	lh.log_filename = strdup(log_file);
	lh.rotate_filename = strdup(rotate_file);
	lh.maxsize = maxsize;
	lh.pagesize = 4096;
	lh.size = 0;
	lh.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	assert(lh.fd >= 0 && "Initial setup open failed");

	/* Mock ringbuffer consumer */
	lh.rbc = NULL;

	/* 1. First write, no rotation */
	int rc = log_data(&lh, data, sizeof(data));
	assert(rc == 0 && "First write failed");
	assert(lh.size == 50 && "size after first write should be 50");

	/* 2. Check manual rotation via log_trim */
	rc = log_trim(&lh);
	assert(rc == 0 && "log_trim failed");
	assert(lh.size == 0 && "size after trim should be 0");
	assert(lh.fd >= 0 && "fd should be valid after trim");

	/* 3. Second write into the new file */
	rc = log_data(&lh, data, sizeof(data));
	assert(rc == 0 && "Second write failed");
	assert(lh.size == 50 && "size after second write should be 50");

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

static void test_log_truncation(void)
{
	struct log_handler lh;
	const char *log_file = "/tmp/test_binary_trunc.log";
	const char *rotate_file = "/tmp/test_binary_trunc.log.1";
	size_t maxsize = 10;
	uint8_t data[20];
	memset(data, 'E', sizeof(data));
	data[19] = 'F'; // Last byte is different

	unlink(log_file);
	unlink(rotate_file);

	lh.log_filename = strdup(log_file);
	lh.rotate_filename = strdup(rotate_file);
	lh.maxsize = maxsize;
	lh.pagesize = 4096;
	lh.size = 0;
	lh.fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	/* Write 20 bytes when maxsize is 10. Should truncate to last 10 bytes. */
	int rc = log_data(&lh, data, sizeof(data));
	assert(rc == 0 && "Truncation write failed");
	assert(lh.size == 10 && "Size should be truncated to maxsize");

	/* Verify file content contains the last byte 'F' */
	close(lh.fd);
	lh.fd = open(log_file, O_RDONLY);
	uint8_t read_buf[10];
	ssize_t n = read(lh.fd, read_buf, 10);
	assert(n == 10 && "Should read 10 bytes");
	assert(read_buf[9] == 'F' &&
	       "File should contain the tail of the data");

	close(lh.fd);
	free(lh.log_filename);
	free(lh.rotate_filename);
	unlink(log_file);
	unlink(rotate_file);
}

int main(void)
{
	test_log_rotation();
	test_log_truncation();
	return EXIT_SUCCESS;
}
