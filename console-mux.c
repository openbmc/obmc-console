#include <assert.h>
#include <err.h>
#include <errno.h>
#include <gpiod.h>
#include <limits.h>
#include <stdlib.h>

#include "console-server.h"
#include "console-mux.h"
#include "config.h"
#include "util.h"

struct console_gpio {
	char *name;
	struct gpiod_line *line;
};

struct console_mux {
	struct console_gpio *mux_gpios;
	size_t n_mux_gpios;
};

static const char *key_mux_gpios = "mux-gpios";
static const char *key_mux_index = "mux-index";

static size_t strtokcnt(const char *str, const char sep)
{
	ssize_t n = 1;

	if (!str) {
		return 0;
	}

	while (*str) {
		if (*str == sep) {
			n++;
		}
		str++;
	}

	return n;
}

static size_t count_mux_gpios(const char *config_mux_gpios)
{
	return strtokcnt(config_mux_gpios, ',');
}

static char *extract_mux_gpio_name(const char **config_gpio_names)
{
	const char *current = *config_gpio_names;
	const char *comma = strchrnul(current, ',');
	const size_t length = comma - current;

	if (length == 0) {
		return NULL;
	}

	char *word = calloc(length + 1, 1);
	if (!word) {
		return NULL;
	}

	strncpy(word, current, length);

	*config_gpio_names = comma + 1;

	return word;
}

static struct console_gpio *
console_mux_find_gpio_by_index(struct console_server *server,
			       const char **config_gpio_names, uint32_t j)
{
	if (j >= server->mux->n_mux_gpios) {
		warnx("mux gpio index out of bounds");
		return NULL;
	}

	struct console_gpio *gpio = &server->mux->mux_gpios[j];

	gpio->name = extract_mux_gpio_name(config_gpio_names);
	if (gpio->name == NULL) {
		warnx("could not find mux gpio for index %d\n", j);
		return NULL;
	}

	gpio->line = gpiod_line_find(gpio->name);
	if (gpio->line == NULL) {
		free(gpio->name);
		warnx("libgpio: could not find line %s\n", gpio->name);
		return NULL;
	}

	return gpio;
}

static void console_mux_release_gpio_lines(struct console_server *server,
					   unsigned long count)
{
	debug("console mux: release gpio lines");

	assert(count <= server->mux->n_mux_gpios);

	for (unsigned long i = 0; i < count; i++) {
		struct console_gpio *gpio = &server->mux->mux_gpios[i];
		gpiod_line_release(gpio->line);
		gpiod_line_close_chip(gpio->line);

		free(gpio->name);
		gpio->name = NULL;
	}
}

static int console_mux_request_gpio_lines(struct console_server *server,
					  const char *config_gpio_names)
{
	struct console_mux *mux = server->mux;
	const char *current = config_gpio_names;
	struct console_gpio *gpio;
	int status = 0;

	debug("console mux: request gpio lines");

	for (size_t j = 0; j < mux->n_mux_gpios; j++) {
		gpio = console_mux_find_gpio_by_index(server, &current, j);
		if (gpio == NULL) {
			console_mux_release_gpio_lines(server, j);
			return -1;
		}

		status = gpiod_line_request_output(
			gpio->line, program_invocation_short_name, 0);
		if (status != 0) {
			warnx("could not set line %s as output\n", gpio->name);
			warnx("releasing all lines already requestd\n");
			console_mux_release_gpio_lines(server, j);
			return -1;
		}
	}

	return 0;
}

int console_server_mux_init(struct console_server *server)
{
	debug("console server mux init");

	int rc = 0;
	const char *config_gpio_names =
		config_get_value(server->config, key_mux_gpios);

	const size_t ngpios = count_mux_gpios(config_gpio_names);
	if (ngpios == 0) {
		return 0;
	}

	const size_t max_ngpios =
		sizeof(((struct console *)0)->mux_index) * CHAR_BIT;

	if (ngpios > max_ngpios) {
		return -1;
	}

	server->mux = malloc(sizeof(struct console_mux));
	if (!server->mux) {
		return -1;
	}

	struct console_mux *mux = server->mux;

	mux->n_mux_gpios = ngpios;
	mux->mux_gpios =
		calloc(server->mux->n_mux_gpios, sizeof(struct console_gpio));

	assert(config_gpio_names != NULL);

	rc = console_mux_request_gpio_lines(server, config_gpio_names);
	if (rc != 0) {
		free(server->mux->mux_gpios);
		server->mux->mux_gpios = NULL;
	}

	return rc;
}

void console_server_mux_fini(struct console_server *server)
{
	if (!server->mux) {
		return;
	}

	if (server->mux->n_mux_gpios == 0) {
		return;
	}

	console_mux_release_gpio_lines(server, server->mux->n_mux_gpios);

	free(server->mux->mux_gpios);
	server->mux->mux_gpios = NULL;

	free(server->mux);
	server->mux = NULL;
}

int console_mux_init(struct console *console, struct config *config)
{
	debug2("console mux init for console id: %s", console->console_id);

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
	console->mux_index = strtoul(gpio_value, NULL, 0);
	if (errno == ERANGE) {
		return -1;
	}

	return 0;
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
#define TIMESTAMP_MAX_SIZE 32
	char buf_timestamp[TIMESTAMP_MAX_SIZE];
	int status;
	char *buf;

	status = console_timestamp(buf_timestamp, sizeof(buf_timestamp));
	if (status != 0) {
		warnx("Error: unable to print timestamp");
		return status;
	}

	status = asprintf(&buf, "[obmc-console] %s %s\n", buf_timestamp,
			  message);
	if (status == -1) {
		return -1;
	}

	ringbuffer_queue(console->rb, (uint8_t *)buf, strlen(buf));

	free(buf);

	return 0;
}

static int console_mux_set_lines(struct console *console)
{
	int status = 0;

	for (size_t i = 0; i < console->server->mux->n_mux_gpios; i++) {
		struct console_gpio *gpio = &console->server->mux->mux_gpios[i];
		const uint8_t value = (console->mux_index >> i) & 0x1;

		status = gpiod_line_set_value(gpio->line, value);
		if (status != 0) {
			warnx("could not set line %s\n", gpio->name);
			return -1;
		}
	}

	return 0;
}

int console_mux_activate(struct console *console)
{
	struct console_server *server = console->server;
	const bool first_activation = server->active == NULL;
	const bool is_active = server->active == console;
	int status = 0;

	debug2("console mux activate console '%s'", console->console_id);

	if (is_active) {
		return 0;
	}

	if (server->mux) {
		status = console_mux_set_lines(console);
	}

	if (status != 0) {
		warnx("Error: unable to set mux gpios.");
		return status;
	}

	server->active = console;

	/* Don't print disconnect/connect events on startup */
	if (first_activation) {
		return 0;
	}

	for (size_t i = 0; i < server->n_consoles; i++) {
		struct console *other = server->consoles[i];
		if (other == console) {
			continue;
		}
		console_print_timestamped(other, "DISCONNECTED");

		for (long j = 0; j < other->n_handlers; j++) {
			struct handler *h = other->handlers[j];

			if (h->type->deselect) {
				h->type->deselect(h);
			}
		}
	}

	console_print_timestamped(console, "CONNECTED");

	return 0;
}
