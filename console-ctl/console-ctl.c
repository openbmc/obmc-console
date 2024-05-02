#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

#include "dbus-helper.h"

const char *control_dbus_interface = "xyz.openbmc_project.Console.Control";
const size_t dbus_obj_path_len = 1024;

static int activate_console(struct sd_bus *bus, char *dbus_name,
			    char *dbus_path, bool arg, bool debug)
{
	if (debug) {
		printf("DEBUG: %s console %s,%s,%s \n",
		       (arg) ? "activate" : "deactivate", dbus_name, dbus_path,
		       control_dbus_interface);
	}

	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int status = sd_bus_call_method(bus, dbus_name, dbus_path,
					control_dbus_interface, "Activate",
					&err, &reply, "b", arg ? 1 : 0);

	if (status < 0) {
		fprintf(stderr, "Failed to issue method call: %s\n",
			err.message);
		sd_bus_error_free(&err);
		return EXIT_FAILURE;
	}

	int reply_status;
	status = sd_bus_message_read(reply, "i", &reply_status);

	if (status < 0) {
		warnx("dbus call 'Activate(%d)': could not read from response",
		      arg);
		return EXIT_FAILURE;
	}

	return reply_status;
}

static char *get_console_id_from_dbus_path(char *dbus_path)
{
	if (dbus_path == NULL) {
		return NULL;
	}

	size_t i = strlen(dbus_path) - 1;
	while (dbus_path[i] != '/' && (i > 0)) {
		i--;
	}
	return dbus_path + i + 1;
}

// returns 0 on success
static int get_device_property(sd_bus *bus, char *dbus_name, char *dbus_path,
			       char **device_str)
{
	(void)bus;
	struct sd_bus *bus2;
	int status;

	status = sd_bus_open_system(&bus2);
	if (status < 0) {
		warnx("Failed to connect to system bus: %s", strerror(-status));
		sd_bus_unref(bus2);
		return EXIT_FAILURE;
	}

	sd_bus_error error = SD_BUS_ERROR_NULL;
	status = sd_bus_get_property_string(bus2, dbus_name, dbus_path,
					    control_dbus_interface, "Device",
					    &error, device_str);

	if (status < 0) {
		fprintf(stderr, "Error: Failed to get 'Device' property: %s\n",
			error.message);
		sd_bus_unref(bus2);
		return 1;
	}

	if (*device_str == NULL) {
		sd_bus_unref(bus2);
		return 1;
	}

	sd_bus_unref(bus2);
	return (status < 0) ? 1 : 0;
}

// returns 0 on success
static int maybe_deactivate_console(struct sd_bus *bus,
				    struct dbus_console_instance *instance,
				    char *console_id, char *device_str,
				    bool debug)
{
	int status = 0;
	char *other_device_str;
	// continue here if the console in question has same 'console_id'
	char *other_console_id =
		get_console_id_from_dbus_path(instance->dbus_path);

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

	status = get_device_property(bus, instance->dbus_name,
				     instance->dbus_path, &other_device_str);

	if (status != 0) {
		fprintf(stderr, "Error reading 'Device' property\n");
		return status;
	}

	if (strcmp(device_str, other_device_str) != 0) {
		// skip this console server, since it is for another device
		if (debug) {
			printf("DEBUG: skip deactivating %s\n",
			       other_console_id);
			printf("DEBUG: skipping since it's for other device %s\n",
			       other_device_str);
		}
		return 0;
	}

	return activate_console(bus, instance->dbus_name, instance->dbus_path,
				false, debug);
}

static int deactivate_other_consoles(struct sd_bus *bus,
				     struct dbus_console_instances *instances,
				     char *console_id, char *device_str,
				     bool debug)
{
	if (debug) {
		printf("DEBUG: deactivating other consoles using %s\n",
		       device_str);
		printf("DEBUG: found %zu instances using %s\n",
		       instances->count, device_str);
	}

	for (size_t i = 0; i < instances->count; i++) {
		struct dbus_console_instance *instance =
			instances->instances[i];

		maybe_deactivate_console(bus, instance, console_id, device_str,
					 debug);
	}

	return 0;
}

static int console_ctl_bus(struct sd_bus *bus, char *console_id, bool debug)
{
	char dbus_name_active_console[dbus_obj_path_len];
	char dbus_path_active_console[dbus_obj_path_len];
	int status;

	sprintf(dbus_name_active_console, "xyz.openbmc_project.Console.%s",
		console_id);
	sprintf(dbus_path_active_console, "/xyz/openbmc_project/console/%s",
		console_id);

	// get the 'Device' property of that dbus id.
	char *device_str = NULL;
	status = get_device_property(bus, dbus_name_active_console,
				     dbus_path_active_console, &device_str);

	if (status != 0) {
		warnx("Error: could not read 'Device' property on %s %s %s",
		      dbus_name_active_console, dbus_path_active_console,
		      control_dbus_interface);
		return 1;
	}

	if (debug) {
		printf("DEBUG: our console device: %s\n", device_str);
	}

	// query all dbus service with console server control interface
	struct dbus_console_instances instances;
	status = call_objectmapper_getsubtree(
		bus, (char *)control_dbus_interface, debug, &instances);

	if (status != 0) {
		return status;
	}

	// deactivate other consoles using the same device
	status = deactivate_other_consoles(bus, &instances, console_id,
					   device_str, debug);

	if (status != 0) {
		return status;
	}

	// activate the selected console
	status = activate_console(bus, dbus_name_active_console,
				  dbus_path_active_console, true, debug);

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
