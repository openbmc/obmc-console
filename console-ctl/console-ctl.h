#pragma once

#include <stdbool.h>
#include <stddef.h>

extern const char *control_dbus_interface;
extern const size_t dbus_obj_path_len;

int console_ctl(char *console_id, bool debug);
