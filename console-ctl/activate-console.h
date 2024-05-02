#pragma once

#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <systemd/sd-bus.h>

int activate_console_by_id(struct sd_bus *bus, char *console_id, bool arg,
			   bool debug);
