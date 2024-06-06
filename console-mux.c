#include <assert.h>
#include <err.h>
#include <errno.h>
#include <gpiod.h>
#include <stdlib.h>

#include "console-server.h"
#include "console-mux.h"
#include "config.h"

struct console_gpio {
	char *name;
	struct gpiod_line *line;
};

struct console_mux {
	struct console_gpio *mux_gpios;
	long n_mux_gpios;
};

static const char *key_mux_gpios = "mux-gpios";
static const char *key_mux_index = "mux-index";

static int find_n_mux_gpios(struct config *config)
{
	int n = 0;
	const char *config_gpio_names = config_get_value(config, key_mux_gpios);

	if (config_gpio_names == NULL) {
		return 0;
	}

	const char *current = config_gpio_names;

	while (current) {
		const char *comma = strchr(current, ',');

		if (!comma) {
			if (*current != '\0') {
				n++;
			}
			break;
		}

		n++;
		current = comma + 1;
	}

	return n;
}

static char *find_nth_mux_gpio_name(const char **config_gpio_names)
{
	const char *current = *config_gpio_names;

	const char *comma = strchr(current, ',');

	if (!comma) {
		// the last gpio name
		comma = current + strlen(current);
	}

	const size_t length = comma - current;

	char *word = calloc(length + 1, 1);

	if (word == NULL) {
		return NULL;
	}

	strncpy(word, current, length);

	*config_gpio_names = comma + 1;

	return word;
}

static int console_mux_find_gpio_by_index(struct console_server *server,
					  const char **config_gpio_names,
					  uint32_t j)
{
	if (j >= server->mux->n_mux_gpios) {
		warnx("mux gpio index out of bounds");
		return 1;
	}

	struct console_gpio *gpio = &server->mux->mux_gpios[j];

	gpio->name = find_nth_mux_gpio_name(config_gpio_names);

	if (gpio->name == NULL) {
		warnx("could not find mux gpio for index %d\n", j);
		return 1;
	}

	gpio->line = gpiod_line_find(gpio->name);

	if (gpio->line == NULL) {
		free(gpio->name);
		warnx("libgpio: could not find line %s\n", gpio->name);
		return 1;
	}

	return 0;
}

static void console_mux_release_gpio_lines(struct console_server *server,
					   long index_excl)
{
	if (index_excl < 0) {
		index_excl = server->mux->n_mux_gpios;
	}

	for (int i = 0; i < index_excl; i++) {
		struct console_gpio *gpio = &server->mux->mux_gpios[i];
		gpiod_line_release(gpio->line);
	}
}

static int console_mux_request_gpio_lines(struct console_server *server)
{
	int status = 0;

	for (int i = 0; i < server->mux->n_mux_gpios; i++) {
		struct console_gpio *gpio = &server->mux->mux_gpios[i];

		status = gpiod_line_request_output(
			gpio->line, program_invocation_short_name, 0);
		if (status != 0) {
			warnx("could not set line %s as output\n", gpio->name);
			warnx("releasing all lines already requestd\n");
			console_mux_release_gpio_lines(server, i);
			return 1;
		}
	}

	return 0;
}

static int console_gpio_set_lines(struct console *console)
{
	int status = 0;

	for (int i = 0; i < console->server->mux->n_mux_gpios; i++) {
		struct console_gpio *gpio = &console->server->mux->mux_gpios[i];

		const uint8_t value = (console->mux_index >> i) & 0x1;

		status = gpiod_line_set_value(gpio->line, value);
		if (status != 0) {
			warnx("could not set line %s\n", gpio->name);
			return 1;
		}
	}

	return 0;
}

int console_server_mux_init(struct console_server *server)
{
	int rc;
	int ngpios;

	ngpios = find_n_mux_gpios(server->config);

	if (ngpios <= 0) {
		return ngpios;
	}

	const size_t max_ngpios = sizeof(((struct console *)0)->mux_index) * 8;

	if ((size_t)ngpios > max_ngpios) {
		return 1;
	}

	server->mux = malloc(sizeof(struct console_mux));

	if (!server->mux) {
		return 1;
	}

	struct console_mux *mux = server->mux;

	mux->n_mux_gpios = ngpios;
	mux->mux_gpios =
		calloc(server->mux->n_mux_gpios, sizeof(struct console_gpio));

	const char *config_gpio_names =
		config_get_value(server->config, key_mux_gpios);

	assert(config_gpio_names != NULL);

	const char *current = config_gpio_names;

	for (int j = 0; j < mux->n_mux_gpios; j++) {
		rc = console_mux_find_gpio_by_index(server, &current, j);
		if (rc != 0) {
			for (int i = 0; i < j; i++) {
				free(server->mux->mux_gpios[i].name);
			}

			free(server->mux->mux_gpios);
			server->mux->mux_gpios = NULL;

			return rc;
		}
	}

	return console_mux_request_gpio_lines(server);
}

int console_mux_init(struct console *console, struct config *config)
{
	if (!console->server->mux) {
		return 0;
	}
	if (console->server->mux->n_mux_gpios == 0) {
		return 0;
	}

	const char *gpio_value = config_get_section_value(
		config, console->console_id, key_mux_index);

	if (gpio_value == NULL) {
		warnx("console %s does not have property %s in config\n",
		      console->console_id, key_mux_index);
		return -1;
	}

	errno = 0;
	const long gpio_value_int = strtol(gpio_value, NULL, 16);

	if (errno == ERANGE) {
		return -1;
	}

	for (int j = 0; j < console->server->mux->n_mux_gpios; j++) {
		const uint8_t value = (gpio_value_int >> j) & 0x1;
		console->mux_index |= value << j;
	}

	return 0;
}

void console_server_mux_fini(struct console_server *server)
{
	if (!server->mux) {
		return;
	}

	if (server->mux->n_mux_gpios == 0) {
		return;
	}

	console_mux_release_gpio_lines(server, -1);

	for (int j = 0; j < server->mux->n_mux_gpios; j++) {
		gpiod_line_close_chip(server->mux->mux_gpios[j].line);
		free(server->mux->mux_gpios[j].name);
	}

	if (server->mux->mux_gpios) {
		free(server->mux->mux_gpios);
		server->mux->mux_gpios = NULL;
	}
}

static int console_timestamp(char *buffer, size_t size)
{
	size_t status;
	time_t rawtime;
	struct tm *timeinfo;

	time(&rawtime);
	timeinfo = gmtime(&rawtime);

	status = strftime(buffer, size, "%Y-%m-%d %H:%M:%S UTC", timeinfo);
	return !status;
}

static int console_print_timestamped(struct console *console,
				     const char *message)
{
	int status;

#define TIMESTAMP_MAX_SIZE 32
	char buf_timestamp[TIMESTAMP_MAX_SIZE];

	status = console_timestamp(buf_timestamp, sizeof(buf_timestamp));

	if (status != 0) {
		warnx("Error: unable to print timestamp");
		return status;
	}

	char *buf;
	status = asprintf(&buf, "[obmc-console] %s %s\n", buf_timestamp,
			  message);
	if (status == -1) {
		return 1;
	}

	ringbuffer_queue(console->rb, (uint8_t *)buf, strlen(buf));

	free(buf);

	return 0;
}

int console_mux_activate(struct console *console)
{
	int status = 0;
	const bool is_active = console->server->active_console == console;

	if (console->server->mux) {
		status = console_gpio_set_lines(console);
	}

	if (status != 0) {
		warnx("Error: unable to set mux gpios.");
		return status;
	}

	if (is_active) {
		return 0;
	}

	struct console_server *server = console->server;

	for (size_t i = 0; i < server->n_consoles; i++) {
		struct console *other = server->consoles[i];
		if (other == console) {
			continue;
		}
		console_print_timestamped(other, "DISCONNECTED");
	}

	console_print_timestamped(console, "CONNECTED");
	server->active_console = console;

	return 0;
}
