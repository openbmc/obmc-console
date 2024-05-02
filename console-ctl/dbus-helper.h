#pragma once

#include <stdbool.h>
#include <systemd/sd-bus.h>

struct dbus_console_instances {
	char **console_ids;
	size_t count;
	size_t capacity;
};

int get_conflicting_console_ids(char *console_id,
				struct dbus_console_instances *instances,
				bool debug);

void dbus_console_instances_free(struct dbus_console_instances *instances);
