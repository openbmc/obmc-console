#pragma once

struct config;
struct console;
struct console_server;

int console_server_mux_init(struct console_server *server);
int console_mux_init(struct console *console, struct config *config);
void console_server_mux_fini(struct console_server *server);

int console_mux_activate_console(struct console *console);
