#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

#include "dbus-helper.h"
#include "activate-console.h"

const char *control_dbus_interface = "xyz.openbmc_project.Console.Control";
#define dbus_obj_path_len 1024

// returns 0 on success
static int maybe_deactivate_console(struct sd_bus *bus, char *other_console_id,
				    char *console_id, bool debug)
{
	// continue here if the console in question has same 'console_id'

	if (other_console_id == NULL) {
		fprintf(stderr, "Error obtaining console id\n");
		return 1;
	}

	if (strcmp(console_id, other_console_id) == 0) {
		// skip our own console
		if (debug) {
			printf("DEBUG: skip deactivating %s\n",
			       other_console_id);
			printf("DEBUG: skipping our own console id\n");
		}
		return 0;
	}

	return activate_console_by_id(bus, other_console_id, false, debug);
}

static int deactivate_other_consoles(struct sd_bus *bus,
				     struct dbus_console_instances *instances,
				     char *console_id, bool debug)
{
	if (debug) {
		printf("DEBUG: found %zu conflicting console instances\n",
		       instances->count);
	}

	for (size_t i = 0; i < instances->count; i++) {
		char *other_console_id = instances->console_ids[i];

		maybe_deactivate_console(bus, other_console_id, console_id,
					 debug);
	}

	return 0;
}

int console_ctl_bus(struct sd_bus *bus, char *console_id, bool debug)
{
	char dbus_name_active_console[dbus_obj_path_len];
	char dbus_path_active_console[dbus_obj_path_len];
	int status;

	sprintf(dbus_name_active_console, "xyz.openbmc_project.Console.%s",
		console_id);
	sprintf(dbus_path_active_console, "/xyz/openbmc_project/console/%s",
		console_id);

	// get conflicting instances
	struct dbus_console_instances instances;
	status =
		get_conflicting_console_ids(bus, console_id, &instances, debug);

	if (status != 0) {
		goto exit;
	}

	// deactivate other consoles using the same device
	status = deactivate_other_consoles(bus, &instances, console_id, debug);

	if (status != 0) {
		goto exit;
	}

	// activate the selected console
	status = activate_console_by_id(bus, console_id, true, debug);
exit:
	dbus_console_instances_free(&instances);
	return status;
}

int console_ctl(char *console_id, bool debug)
{
	struct sd_bus *bus;
	int status;

	status = sd_bus_open_system(&bus);
	if (status < 0) {
		warnx("Failed to connect to system bus: %s", strerror(-status));
		return EXIT_FAILURE;
	}

	status = console_ctl_bus(bus, console_id, debug);

	sd_bus_unref(bus);

	return status;
}
