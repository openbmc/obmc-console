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

static bool
test_console_ctl_deactivates_conflicting_console_id_dbus(struct sd_bus *bus)
{
	FILE *master = NULL;
	char *slave_filename = NULL;
	bool active1;
	bool active2;
	int status;

	create_pty(&master, &slave_filename);

	if (master == NULL || slave_filename == NULL) {
		fprintf(stderr, "Error: could not create pty\n");
		return false;
	}

	pid_t pid1;
	// fork off one console server, but with multiple consoles
	const char *console_ids[] = { "testconsole1", "testconsole2" };
	status = fork_off_console_server(bus, slave_filename,
					 (char **)console_ids, 2, &pid1);
	if (status != 0) {
		fclose(master);
		return false;
	}

	active1 = dbus_get_active_state(bus, "testconsole1", &status);
	if (status != 0) {
		fclose(master);
		return false;
	}
	active2 = dbus_get_active_state(bus, "testconsole2", &status);
	if (status != 0) {
		fclose(master);
		return false;
	}
	assert(active1 == true);
	assert(active2 == false);

	// activate the inactive console
	console_ctl_bus(bus, "testconsole2", false);

	active1 = dbus_get_active_state(bus, "testconsole1", &status);
	if (status != 0) {
		fclose(master);
		return false;
	}
	active2 = dbus_get_active_state(bus, "testconsole2", &status);
	if (status != 0) {
		fclose(master);
		return false;
	}
	assert(active1 == false);
	assert(active2 == true);

	kill(pid1, SIGINT);
	fclose(master);
	return true;
}

int main(void)
{
	printf("\n\nTEST: activation of one muxed console should deactivate the others\n");
	run_with_dbus(test_console_ctl_deactivates_conflicting_console_id_dbus);

	printf("\nTEST PASSED\n");

	return EXIT_SUCCESS;
}
