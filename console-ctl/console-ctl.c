#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

#include "activate-console.h"

const char *control_dbus_interface = "xyz.openbmc_project.Console.Control";
#define dbus_obj_path_len 1024

int console_ctl_bus(struct sd_bus *bus, char *console_id, bool debug)
{
	char dbus_name_active_console[dbus_obj_path_len];
	char dbus_path_active_console[dbus_obj_path_len];

	sprintf(dbus_name_active_console, "xyz.openbmc_project.Console.%s",
		console_id);
	sprintf(dbus_path_active_console, "/xyz/openbmc_project/console/%s",
		console_id);

	// activate the selected console
	return activate_console_by_id(bus, console_id, debug);
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
