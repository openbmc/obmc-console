/**
 * Copyright © 2016 IBM Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <termios.h> /* for speed_t */
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <iniparser/iniparser.h>

#include "config-internal.h"
#include "config.h"
#include "util.h"

static const char *config_default_filename = SYSCONFDIR "/obmc-console.conf";

const char *config_get_value(struct config *config, const char *name)
{
	char buf[CONFIG_MAX_KEY_LENGTH];
	int rc;

	if (!config->dict) {
		return NULL;
	}

	rc = snprintf(buf, CONFIG_MAX_KEY_LENGTH, ":%s", name);
	if (rc < 0) {
		return NULL;
	}

	if ((size_t)rc >= sizeof(buf)) {
		return NULL;
	}

	const char *value = iniparser_getstring(config->dict, buf, NULL);
	if (value && strlen(value) == 0) {
		return NULL;
	}

	return value;
}

struct config *config_init(const char *filename)
{
	struct config *config;
	dictionary *dict;

	if (!filename) {
		filename = config_default_filename;
	}

	if (access(filename, R_OK) == 0) {
		dict = iniparser_load(filename);
		if (!dict) {
			/* Assume this is a parse failure */
			return NULL;
		}
	} else {
		/* If a config file was explicitly specified, then lack of access is always an error */
		if (filename != config_default_filename) {
			warn("Failed to open configuration file at '%s'",
			     filename);
			return NULL;
		}

		/* For the default config path, any result other than not-present is an error */
		if (errno != ENOENT && errno != ENOTDIR) {
			warn("Failed to open configuration file at '%s'",
			     filename);
			return NULL;
		}

		/* Config not present at default path, pretend its empty */
		dict = NULL;
	}

	config = malloc(sizeof(*config));
	if (!config) {
		iniparser_freedict(dict);
		return NULL;
	}

	config->dict = dict;

	return config;
}

const char *config_get_section_value(struct config *config, const char *secname,
				     const char *name)
{
	char buf[CONFIG_MAX_KEY_LENGTH];
	int rc;

	rc = snprintf(buf, sizeof(buf), "%s:%s", secname, name);
	if (rc < 0) {
		return NULL;
	}

	if ((size_t)rc >= sizeof(buf)) {
		// error / key too long for the buffer
		warnx("config: section:key too long for buffer: '%s':'%s'",
		      secname, name);
		return NULL;
	}

	return iniparser_getstring(config->dict, buf, NULL);
}

void config_fini(struct config *config)
{
	if (!config) {
		return;
	}

	if (config->dict) {
		iniparser_freedict(config->dict);
	}

	free(config);
}

struct terminal_speed_name {
	speed_t speed;
	uint32_t baud;
	const char *name;
};

#define TERM_SPEED(x) { B##x, x, #x }

// clang-format off
static const struct terminal_speed_name terminal_speeds[] = {
	TERM_SPEED(50),
	TERM_SPEED(75),
	TERM_SPEED(110),
	TERM_SPEED(134),
	TERM_SPEED(150),
	TERM_SPEED(200),
	TERM_SPEED(300),
	TERM_SPEED(600),
	TERM_SPEED(1200),
	TERM_SPEED(1800),
	TERM_SPEED(2400),
	TERM_SPEED(4800),
	TERM_SPEED(9600),
	TERM_SPEED(19200),
	TERM_SPEED(38400),
	TERM_SPEED(57600),
	TERM_SPEED(115200),
	TERM_SPEED(230400),
	TERM_SPEED(460800),
	TERM_SPEED(500000),
	TERM_SPEED(576000),
	TERM_SPEED(921600),
	TERM_SPEED(1000000),
	TERM_SPEED(1152000),
	TERM_SPEED(1500000),
	TERM_SPEED(2000000),
	TERM_SPEED(2500000),
	TERM_SPEED(3000000),
	TERM_SPEED(3500000),
	TERM_SPEED(4000000),
};
// clang-format on

int config_parse_baud(speed_t *speed, const char *baud_string)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(terminal_speeds); i++) {
		if (strcmp(baud_string, terminal_speeds[i].name) == 0) {
			*speed = terminal_speeds[i].speed;
			return 0;
		}
	}
	return -1;
}

uint32_t parse_baud_to_int(speed_t speed)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(terminal_speeds); i++) {
		if (terminal_speeds[i].speed == speed) {
			return terminal_speeds[i].baud;
		}
	}
	return 0;
}

speed_t parse_int_to_baud(uint32_t baud)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(terminal_speeds); i++) {
		if (terminal_speeds[i].baud == baud) {
			return terminal_speeds[i].speed;
		}
	}
	return 0;
}

int config_parse_bytesize(const char *size_str, size_t *size)
{
	struct size_suffix_shift {
		/* Left shiftwidth corresponding to the suffix. */
		size_t shiftwidth;
		int unit;
	};

	const struct size_suffix_shift suffixes[] = {
		{ 10, 'k' },
		{ 20, 'M' },
		{ 30, 'G' },
	};
	const size_t num_suffixes =
		sizeof(suffixes) / sizeof(struct size_suffix_shift);
	size_t logsize;
	char *suffix;
	size_t i;

	if (!size_str) {
		return -1;
	}

	logsize = strtoul(size_str, &suffix, 0);
	if (logsize == 0 || logsize >= UINT32_MAX || suffix == size_str) {
		return -1;
	}

	/* Ignore spaces between number and suffix */
	while (*suffix && isspace(*suffix)) {
		suffix++;
	}

	for (i = 0; i < num_suffixes; i++) {
		if (*suffix == suffixes[i].unit) {
			/*
			 * If logsize overflows, probably something was wrong.
			 * Return instead of clamping to an arbitrary value.
			 */
			if (logsize > (UINT32_MAX >> suffixes[i].shiftwidth)) {
				return -1;
			}

			logsize <<= suffixes[i].shiftwidth;
			suffix++;
			break;
		}
	}

	/* Allow suffix like 'kB' */
	while (*suffix && (tolower(*suffix) == 'b' || isspace(*suffix))) {
		suffix++;
	}

	if (*suffix) {
		warn("Invalid suffix!");
		return -1;
	}

	*size = logsize;
	return 0;
}

/* Default console id if not specified on command line or in config */
#define DEFAULT_CONSOLE_ID "default"

/* Get the console id */
const char *config_resolve_console_id(struct config *config, const char *id_arg)
{
	const char *configured;

	if (id_arg) {
		return id_arg;
	}

	if ((configured = config_get_value(config, "console-id"))) {
		return configured;
	}

	return DEFAULT_CONSOLE_ID;
}

int config_count_sections(struct config *config)
{
	return iniparser_getnsec(config->dict);
}

const char *config_get_section_name(struct config *config, int i)
{
	return iniparser_getsecname(config->dict, i);
}
