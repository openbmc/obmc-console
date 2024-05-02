#pragma once

#include <stdbool.h>
#include <systemd/sd-bus.h>

struct dbus_console_instances {
	struct dbus_console_instance **instances;
	size_t count;
	size_t capacity;
};

struct dbus_console_instance {
	char *console_id;
};

int get_conflicting_console_ids(char *console_id,
				struct dbus_console_instances *instances,
				bool debug);
