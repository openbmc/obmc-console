// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright 2024 9elements

#pragma once

struct config;
struct console;
struct console_server;

int console_server_mux_init(struct console_server *server);
void console_server_mux_fini(struct console_server *server);
int console_mux_init(struct console *console, struct config *config);
int console_mux_activate(struct console *console);
