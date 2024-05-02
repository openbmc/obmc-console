#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "console-ctl/console-ctl.h"

#include "console-server-test-util.h"

static bool
test_console_ctl_deactivates_conflicting_console_id_dbus(struct sd_bus *bus)
{
	char console_id_1[200];
	randomize_console_id(console_id_1, "consoleado1");
	char console_id_2[200];
	randomize_console_id(console_id_2, "consoleado2");

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
	const char *console_ids[] = { console_id_1, console_id_2 };
	status = fork_off_console_server(bus, slave_filename,
					 (char **)console_ids, 2, &pid1);
	if (status != 0) {
		fclose(master);
		return false;
	}

	active1 = dbus_get_active_state(bus, console_id_1, &status);
	if (status != 0) {
		fclose(master);
		return false;
	}
	active2 = dbus_get_active_state(bus, console_id_2, &status);
	if (status != 0) {
		fclose(master);
		return false;
	}
	assert(active1 == true);
	assert(active2 == false);

	// activate the inactive console
	console_ctl_bus(bus, console_id_2, false);

	active1 = dbus_get_active_state(bus, console_id_1, &status);
	if (status != 0) {
		fclose(master);
		return false;
	}
	active2 = dbus_get_active_state(bus, console_id_2, &status);
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
