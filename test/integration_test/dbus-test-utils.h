#pragma once

#include <systemd/sd-bus.h>

int block_on_dbus_console_id(struct sd_bus *bus, char *console_id);
