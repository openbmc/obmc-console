#pragma once

#include <termios.h>
#include <stdint.h>
#include <stdlib.h>

extern const char *config_default_filename;

/* config API */
struct config;
const char *config_get_value(struct config *config, const char *name);
struct config *config_init(const char *filename);
const char *config_resolve_console_id(struct config *config,
				      const char *id_arg);
void config_fini(struct config *config);

int config_parse_baud(speed_t *speed, const char *baud_string);

uint32_t parse_baud_to_int(speed_t speed);

speed_t parse_int_to_baud(uint32_t baud);

int config_parse_bytesize(const char *size_str, size_t *size);
