#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

#include "console-ctl.h"
#include "activate-console.h"

#define dbus_obj_path_len 1024

int activate_console(struct sd_bus *bus, char *dbus_name, char *dbus_path,
		     bool debug)
{
	if (debug) {
		printf("DEBUG: activate console %s,%s,%s \n", dbus_name,
		       dbus_path, control_dbus_interface);
	}

	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int reply_status;
	int status = sd_bus_call_method(bus, dbus_name, dbus_path,
					control_dbus_interface, "Activate",
					&err, &reply, "");

	if (status < 0) {
		fprintf(stderr, "%s: Failed to issue method call: %s\n",
			__func__, err.message);
		reply_status = EXIT_FAILURE;
		goto cleanup;
	}

	status = sd_bus_message_read(reply, "i", &reply_status);

	if (status < 0) {
		warnx("dbus call 'Activate()': could not read from response");
		reply_status = EXIT_FAILURE;
		goto cleanup;
	}

cleanup:
	sd_bus_error_free(&err);
	sd_bus_message_unrefp(&reply);

	return reply_status;
}

int activate_console_by_id(struct sd_bus *bus, char *console_id, bool debug)
{
	char dbus_name_active_console[dbus_obj_path_len];
	char dbus_path_active_console[dbus_obj_path_len];
	sprintf(dbus_name_active_console, "xyz.openbmc_project.Console.%s",
		console_id);
	sprintf(dbus_path_active_console, "/xyz/openbmc_project/console/%s",
		console_id);

	return activate_console(bus, dbus_name_active_console,
				dbus_path_active_console, debug);
}
