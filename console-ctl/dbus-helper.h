#pragma once

#include <stdbool.h>
#include <systemd/sd-bus.h>

struct dbus_console_instances {
	struct dbus_console_instance **instances;
	size_t count;
	size_t capacity;
};

struct dbus_console_instance {
	char *dbus_name;
	char *dbus_path;
};

// function will look for dbus paths implementing 'dbus_interface'
// function will populate 'instances'
// returns 0 on success
int call_objectmapper_getsubtree(sd_bus *bus, char *dbus_interface, bool debug,
				 struct dbus_console_instances *instances);
