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

int console_gpio_mux_init(struct console *console, struct config *config);
void console_gpio_mux_fini(struct console *console);

int console_gpio_activate_console(struct console *console);
