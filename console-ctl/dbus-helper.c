#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <systemd/sd-bus.h>

#include "dbus-helper.h"

void dbus_console_instances_init(struct dbus_console_instances *instances)
{
	instances->capacity = 10;
	instances->count = 0;
	instances->instances = (struct dbus_console_instance **)malloc(
		sizeof(struct dbus_console_instance *) * instances->capacity);
}

void dbus_console_instances_insert(struct dbus_console_instances *instances,
				   char *dbus_name, char *dbus_path)
{
	size_t i = instances->count;

	if (i >= instances->capacity) {
		instances->capacity *= 2;
		instances->instances =
			realloc(instances->instances,
				instances->capacity *
					sizeof(struct dbus_console_instance *));
	}

	instances->instances[i] = malloc(sizeof(struct dbus_console_instance));
	instances->instances[i]->dbus_name = malloc(strlen(dbus_name) + 1);
	instances->instances[i]->dbus_path = malloc(strlen(dbus_path) + 1);

	strcpy(instances->instances[i]->dbus_name, dbus_name);
	strcpy(instances->instances[i]->dbus_path, dbus_path);

	instances->count += 1;
}

static int parse_subtree_dict_entry_value(sd_bus_message *response,
					  char **dbus_name)
{
	int ret;
	ret = sd_bus_message_enter_container(response, 'e', "sas");

	if (ret != 1) {
		fprintf(stderr, "failed to enter struct of dbus name\n");
		return 1;
	}

	sd_bus_message_read(response, "s", dbus_name);

	ret = sd_bus_message_skip(response, "as");
	if (ret < 0) {
		fprintf(stderr, "Error: failed to skip dbus types\n");
		return ret;
	}

	sd_bus_message_exit_container(response);

	return 0;
}

static int parse_subtree_dict_entry(sd_bus_message *response,
				    struct dbus_console_instances *instances,
				    bool debug)
{
	const char *path;
	int ret = 0;

	if (debug) {
		printf("DEBUG: trying to read from dict entry\n");
	}

	ret = sd_bus_message_read(response, "s", &path);

	if (ret == 0) {
		fprintf(stderr, "Failed to read the path: %s\n",
			strerror(-ret));
		return 1;
	}

	if (debug) {
		printf("DEBUG: read path %s\n", path);
	}

	char *dbus_name = "NOT FOUND";
	ret = sd_bus_message_enter_container(response, 'a', "{sas}");

	if (ret != 1) {
		fprintf(stderr, "failed to enter container of dbus names\n");
		return 1;
	}

	ret = parse_subtree_dict_entry_value(response, &dbus_name);

	sd_bus_message_exit_container(response);

	dbus_console_instances_insert(instances, dbus_name, (char *)path);

	return ret;
}

int call_objectmapper_getsubtree(sd_bus *bus, char *dbus_interface, bool debug,
				 struct dbus_console_instances *instances)
{
	if (debug) {
		printf("DEBUG: calling objectmapper\n");
	}

	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *response = NULL;
	int ret;

	ret = sd_bus_call_method(bus, "xyz.openbmc_project.ObjectMapper",
				 "/xyz/openbmc_project/object_mapper",
				 "xyz.openbmc_project.ObjectMapper",
				 "GetSubTree", &error, &response, "sias", "/",
				 0, 1, dbus_interface);
	if (ret < 0) {
		fprintf(stderr, "Failed to issue method call: %s\n",
			error.message);
		sd_bus_error_free(&error);
		sd_bus_unref(bus);
		return -1;
	}

	if (debug) {
		printf("DEBUG: call completed, parsing response\n");
	}

	ret = sd_bus_message_enter_container(response, 'a',
					     "{sa{sas}}"); // Array of tuples
	if (ret != 1) {
		fprintf(stderr, "Failed to enter container: %s\n",
			strerror(-ret));
		sd_bus_message_unref(response);
		sd_bus_unref(bus);
		return -1;
	}

	if (debug) {
		printf("DEBUG: entered container, parsing contents\n");
	}

	dbus_console_instances_init(instances);

	while ((ret = sd_bus_message_enter_container(response, 'e',
						     "sa{sas}")) == 1) {
		ret = parse_subtree_dict_entry(response, instances, debug);

		if (ret != 0) {
			fprintf(stderr,
				"Error, exiting parsing of dbus response\n");
			return ret;
		}

		sd_bus_message_exit_container(response);
	}

	sd_bus_message_exit_container(response);
	sd_bus_message_unref(response);

	return 0;
}
