#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "console-server-test-util.h"

static bool test_console_logs_to_file(struct sd_bus *bus)
{
	FILE *master = NULL;
	char *slave_filename = NULL;
	char console_id[200];
	randomize_console_id(console_id, "testlogtofile");

	create_pty(&master, &slave_filename);

	if (master == NULL || slave_filename == NULL) {
		fprintf(stderr, "Error: could not create pty\n");
		return false;
	}

	pid_t pid;
	const char *console_ids[] = { console_id };

	int status = fork_off_console_server(bus, slave_filename,
					     (char **)console_ids, 1, &pid);
	if (status != 0) {
		fclose(master);
		return false;
	}

	const char *log_line = "console-should-log-to-file";

	fprintf(master, "%s\n", log_line);

	// sleep to allow for writing logs
	sleep(1);

	kill(pid, SIGINT);

	char *buf = read_console_log_file(console_id);

	assert_contains(buf, (char *)log_line);

	free(buf);
	fclose(master);

	return true;
}

int main(int argc, char *argv[])
{
	printf("\n\nTEST: console logs to file as expected\n");
	run_with_dbus(test_console_logs_to_file, argc, argv);

	printf("\nTEST PASSED\n");

	return EXIT_SUCCESS;
}
