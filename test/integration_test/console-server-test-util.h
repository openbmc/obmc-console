#pragma once

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <fcntl.h>

#include <systemd/sd-bus.h>

void assert_contains(char *buf, char *str);

void assert_not_contains(char *buf, char *str);

char *console_server_process_n_consoles(char *slave_filename,
					char **console_ids,
					size_t n_console_ids);

void create_pty(FILE **master, char **slave_filename);
int create_pty2(int *masterfd, char **slave_filename);

char *read_console_log_file(char *console_id);

int fork_off_console_server(struct sd_bus *bus, char *slave_filename,
			    char **console_ids, size_t n_console_ids,
			    pid_t *pid);

bool dbus_get_active_state(struct sd_bus *bus, char *console_id, int *status);

void run_with_dbus(bool (*test)(struct sd_bus *));
