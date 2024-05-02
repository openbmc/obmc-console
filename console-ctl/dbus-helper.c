#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <systemd/sd-bus-protocol.h>
#include <systemd/sd-bus.h>

#include "console-ctl.h"
#include "dbus-helper.h"

#define dbus_obj_path_len 1024

void dbus_console_instances_init(struct dbus_console_instances *instances)
{
	instances->capacity = 10;
	instances->count = 0;
	instances->console_ids = malloc(sizeof(char *) * instances->capacity);
}

void dbus_console_instances_free(struct dbus_console_instances *instances)
{
	for (size_t i = 0; i < instances->count; i++) {
		free(instances->console_ids[i]);
	}

	free(instances->console_ids);
}

void dbus_console_instances_insert(struct dbus_console_instances *instances,
				   char *console_id)
{
	size_t i = instances->count;

	if (i >= instances->capacity) {
		instances->capacity *= 2;
		instances->console_ids =
			realloc(instances->console_ids,
				instances->capacity * sizeof(char *));
	}

	instances->console_ids[i] = malloc(SD_BUS_MAXIMUM_NAME_LENGTH + 1);

	strncpy(instances->console_ids[i], console_id,
		SD_BUS_MAXIMUM_NAME_LENGTH);

	instances->count += 1;
}

int get_conflicting_console_ids(struct sd_bus *bus, char *console_id,
				struct dbus_console_instances *instances,
				bool debug)
{
	int status;

	if (debug) {
		printf("DEBUG: fetching conflicting console-ids\n");
	}

	char dbus_name[dbus_obj_path_len];
	char dbus_path[dbus_obj_path_len];
	sprintf(dbus_name, "xyz.openbmc_project.Console.%s", console_id);
	sprintf(dbus_path, "/xyz/openbmc_project/console/%s", console_id);

	dbus_console_instances_init(instances);

	sd_bus_message *reply = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	status = sd_bus_get_property(bus, dbus_name, dbus_path,
				     control_dbus_interface,
				     "ConflictingConsoleIds", &error, &reply,
				     "as");

	if (status < 0) {
		fprintf(stderr,
			"Error: Failed to get 'ConflictingConsoleIds' property: %s\n",
			error.message);
		sd_bus_unref(bus);
		return 1;
	}

	char **conflicting_ids = NULL;
	status = sd_bus_message_read_strv(reply, &conflicting_ids);
	if (status < 0) {
		fprintf(stderr, "failed to read dbus string array\n");
		return 1;
	}

	while (*conflicting_ids != NULL) {
		if (debug) {
			printf("DEBUG: inserting %s\n", *conflicting_ids);
		}
		dbus_console_instances_insert(instances, *conflicting_ids);
		conflicting_ids++;
	}

	return (status < 0) ? 1 : 0;
}
