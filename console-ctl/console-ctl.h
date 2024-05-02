#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <systemd/sd-bus.h>

extern const char *control_dbus_interface;
extern const size_t dbus_obj_path_len;

int console_ctl(char *console_id, bool debug);
int console_ctl_bus(struct sd_bus *bus, char *console_id, bool debug);
