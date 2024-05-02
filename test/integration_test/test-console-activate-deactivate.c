#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "console-ctl/activate-console.h"
#include "console-ctl/console-ctl.h"

#include "console-server-test-util.h"

const char *should_be_in_log = "should-be-in-log";
const char *should_not_be_in_log = "should-not-be-in-log";
const char *test_console_id = "test";

static bool test_activate_deactivate_console_dbus(struct sd_bus *bus)
{
	FILE *master = NULL;
	char *slave_filename = NULL;

	create_pty(&master, &slave_filename);

	if (master == NULL || slave_filename == NULL) {
		fprintf(stderr, "Error: could not create pty\n");
		return false;
	}

	//create multiple consoles in that console-server
	//and only one of them should be active and have the logs
	pid_t pid;
	const char *console_ids[] = { "testconsole1", "testconsole2" };
	int status = fork_off_console_server(bus, slave_filename,
					     (char **)console_ids, 2, &pid);
	if (status != 0) {
		fclose(master);
		return false;
	}

	status = activate_console_by_id(bus, "testconsole1", false);
	if (status != 0) {
		kill(pid, SIGINT);
	}
	assert(status == 0);

	fprintf(master, "%s\n", "testconsole1 active");

	status = activate_console_by_id(bus, "testconsole2", false);
	if (status != 0) {
		kill(pid, SIGINT);
	}
	assert(status == 0);

	fprintf(master, "%s\n", "testconsole2 active");

	// sleep to allow for writing logs
	sleep(1);

	kill(pid, SIGINT);

	char *buf;

	// assert log file for testconsole1
	buf = read_console_log_file("testconsole1");

	assert_contains(buf, "testconsole1 active");
	assert_contains(buf, " DISCONNECTED");
	assert_not_contains(buf, "testconsole2 active");

	free(buf);

	// assert log file for testconsole2
	buf = read_console_log_file("testconsole2");

	assert_contains(buf, " CONNECTED");
	assert_contains(buf, "testconsole2 active");
	assert_not_contains(buf, "testconsole1 active");

	free(buf);
	fclose(master);

	return true;
}

int main(void)
{
	printf("\n\nTEST: activate/deactivate\n");
	run_with_dbus(test_activate_deactivate_console_dbus);

	printf("\nTEST PASSED\n");

	return EXIT_SUCCESS;
}
