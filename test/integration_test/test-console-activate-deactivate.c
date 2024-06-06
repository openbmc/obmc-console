#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "console-server-test-util.h"

int main(void)
{
	printf("\n\nTEST: activate/deactivate\n");

	struct sd_bus *bus;
	char console_id_1[200];
	randomize_console_id(console_id_1, "testconsole1ad");
	char console_id_2[200];
	randomize_console_id(console_id_2, "testconsole2ad");

	FILE *master = NULL;
	char *slave_filename = NULL;

	create_pty(&master, &slave_filename);

	if (master == NULL || slave_filename == NULL) {
		fprintf(stderr, "Error: could not create pty\n");
		return EXIT_FAILURE;
	}

	//create multiple consoles in that console-server
	//and only one of them should be active and have the logs
	pid_t pid;
	const char *console_ids[] = { console_id_1, console_id_2 };
	int status = fork_off_console_server(&bus, slave_filename,
					     (char **)console_ids, 2, &pid);
	if (status != 0) {
		fclose(master);
		return EXIT_FAILURE;
	}

	status = activate_console_by_id(bus, console_id_1);
	if (status != 0) {
		kill(pid, SIGINT);
	}
	assert(status == 0);

	fprintf(master, "%s\n", "console1 active");

	status = activate_console_by_id(bus, console_id_2);
	if (status != 0) {
		kill(pid, SIGINT);
	}
	assert(status == 0);

	fprintf(master, "%s\n", "console2 active");

	// sleep to allow for writing logs
	sleep(1);

	kill(pid, SIGINT);

	char *buf;

	// assert log file for testconsole1
	buf = read_console_log_file(console_id_1);

	assert_contains(buf, "console1 active");
	assert_contains(buf, " DISCONNECTED");
	assert_not_contains(buf, "console2 active");

	free(buf);

	// assert log file for testconsole2
	buf = read_console_log_file(console_id_2);

	assert_contains(buf, " CONNECTED");
	assert_contains(buf, "console2 active");
	assert_not_contains(buf, "console1 active");

	free(buf);
	fclose(master);

	sd_bus_flush_close_unref(bus);

	printf("\nTEST PASSED\n");

	return EXIT_SUCCESS;
}
