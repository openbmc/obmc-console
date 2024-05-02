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

static bool test_call_activate_dbus_method(struct sd_bus *bus)
{
	FILE *master = NULL;
	char *slave_filename = NULL;

	create_pty(&master, &slave_filename);

	if (master == NULL || slave_filename == NULL) {
		fprintf(stderr, "Error: could not create pty\n");
		return false;
	}

	pid_t pid;
	const char *console_ids[] = { "testconsole1" };
	int status = fork_off_console_server(bus, slave_filename,
					     (char **)console_ids, 1, &pid);
	if (status != 0) {
		return false;
	}

	sleep(2);

	status = activate_console_by_id(bus, "testconsole1", false);
	if (status != 0) {
		kill(pid, SIGINT);
	}
	assert(status == 0);

	kill(pid, SIGINT);

	fclose(master);
	return true;
}

int main(void)
{
	printf("\n\nTEST: call activate dbus method\n");
	run_with_dbus(test_call_activate_dbus_method);

	printf("\nTEST PASSED\n");

	return EXIT_SUCCESS;
}
