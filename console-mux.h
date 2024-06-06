#pragma once

#include <gpiod.h>
#include <stdbool.h>
#include "console-server.h"

struct console_mux;

int console_mux_init(struct console *console, struct config *config);
void console_mux_fini(struct console *console);

int console_mux_activate_console(struct console *console);
