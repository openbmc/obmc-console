#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <assert.h>

#include "console-server-test-util.h"

static bool test_console_logs_to_file(void)
{
	FILE *master = NULL;
	struct sd_bus *bus;
	char *slave_filename = NULL;
	char console_id[200];
	int rc;

	randomize_console_id(console_id, "testlogtofile");

	create_pty(&master, &slave_filename);

	if (master == NULL || slave_filename == NULL) {
		fprintf(stderr, "Error: could not create pty\n");
		return false;
	}

	pid_t pid;
	const char *console_ids[] = { console_id };

	int status = fork_off_console_server(&bus, slave_filename,
					     (char **)console_ids, 1, &pid);
	if (status != 0) {
		fclose(master);
		return false;
	}

	const char *log_line = "console-should-log-to-file";

	rc = fprintf(master, "%s\n", log_line);
	assert(rc == (int)strlen(log_line) + 1);

	assert(fflush(master) == 0);

	// sleep to allow for writing logs
	sleep(1);

	kill(pid, SIGINT);

	char *buf = read_console_log_file(console_id);

	assert_contains(buf, (char *)log_line);

	free(buf);
	fclose(master);

	sd_bus_flush_close_unref(bus);

	return true;
}

int main(void)
{
	printf("\n\nTEST: console logs to file as expected\n");
	assert(test_console_logs_to_file() == true);

	printf("\nTEST PASSED\n");

	return EXIT_SUCCESS;
}
