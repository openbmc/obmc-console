#pragma once

#include <gpiod.h>
#include <stdbool.h>
#include "console-server.h"

struct console_gpio {
	char *name;
	uint8_t value;
	unsigned int line_offset;
	struct gpiod_line *line;
};

int mux_gpios_init(struct console *console, struct config *config);
void mux_gpios_fini(struct console *console);

int set_console_gpios(struct console *console);
void release_console_gpios(struct console *console);
