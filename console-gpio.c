#include <err.h>
#include <gpiod.h>
#include <stdlib.h>

#include "console-server.h"
#include "console-gpio.h"
#include "config.h"

const char *key_mux_gpios = "mux-gpios";
const char *key_mux_index = "mux-index";

static int find_n_mux_gpios(struct config *config)
{
	int n = 0;
	char *gpio_names = (char *)config_get_value(config, key_mux_gpios);

	if (gpio_names == NULL) {
		return 0;
	}

	gpio_names = strdup(gpio_names);

	char *next = strtok(gpio_names, ",");
	for (; next != NULL;) {
		n++;
		next = strtok(NULL, ",");
	}

	free(gpio_names);

	return n;
}

static char *find_nth_mux_gpio_name(struct config *config, uint32_t j)
{
	char *gpio_names = (char *)config_get_value(config, key_mux_gpios);
	char *res = NULL;

	if (gpio_names == NULL) {
		return NULL;
	}

	char *gpio_names_dup = strdup(gpio_names);

	char *next = strtok(gpio_names_dup, ",");
	for (uint32_t i = 1; i <= j; i++) {
		next = strtok(NULL, ",");
	}

	res = strdup(next);

	free(gpio_names_dup);

	return res;
}

// returns -1 on error
static int find_nth_mux_gpio_value(struct config *config,
				   const char *console_id, uint32_t j)
{
	const char *gpio_value =
		config_get_section_value(config, console_id, key_mux_index);

	if (gpio_value == NULL) {
		warnx("console %s does not have property %s in config\n",
		      console_id, key_mux_index);
		return -1;
	}

	char *end;
	int gpio_value_int = (int)strtol(gpio_value, &end, 16);

	return (gpio_value_int >> j) & 0x1;
}

static int set_mux_gpio_from_config(struct console *console,
				    struct config *config, uint32_t j)
{
	console->mux_gpios[j] = malloc(sizeof(struct console_gpio));
	struct console_gpio *gpio = console->mux_gpios[j];

	gpio->name = find_nth_mux_gpio_name(config, j);

	if (gpio->name == NULL) {
		warnx("could not find mux gpio for index %d\n", j);
		return 1;
	}

	const int gpio_value =
		find_nth_mux_gpio_value(config, console->console_id, j);

	if (gpio_value < 0) {
		warnx("could not find mux gpio value for index %d\n", j);
		return 1;
	}

	gpio->value = gpio_value;

	gpio->line =
		gpiod_chip_find_line(console->server->gpio_chip, gpio->name);

	if (gpio->line == NULL) {
		warnx("libgpio: could not find line %s\n", gpio->name);
	}

	gpio->line_offset = gpiod_line_offset(gpio->line);

	if (console->server->debug) {
		printf("added mux gpio %s with value %d\n", gpio->name,
		       gpio->value);
	}

	return 0;
}

int mux_gpios_init(struct console *console, struct config *config)
{
	int status = 0;

	if (console->server->debug) {
		printf("setting mux gpios from config\n");
	}

	const char *const chip_path = "/dev/gpiochip0";
	console->server->gpio_chip = NULL;

	console->n_mux_gpios = find_n_mux_gpios(config);

	if (console->server->debug) {
		printf("found %ld mux gpios in config\n", console->n_mux_gpios);
	}

	if (console->n_mux_gpios == 0) {
		return 0;
	}

	if (console->server->debug) {
		printf("opening gpio chip %s\n", chip_path);
	}
	console->server->gpio_chip = gpiod_chip_open(chip_path);
	if (console->server->gpio_chip == NULL) {
		warnx("could not open gpio chip %s", chip_path);
		return 1;
	}

	console->mux_gpios =
		malloc(sizeof(struct console_gpio *) * console->n_mux_gpios);

	for (int j = 0; j < console->n_mux_gpios; j++) {
		status = set_mux_gpio_from_config(console, config, j);
		if (status != 0) {
			return status;
		}
	}

	if (console->server->active_console == console) {
		return set_console_gpios(console);
	}

	return 0;
}

void mux_gpios_fini(struct console *console)
{
	if (console->server->gpio_chip != NULL) {
		release_console_gpios(console);
		gpiod_chip_close(console->server->gpio_chip);
	}
}

void release_console_gpios(struct console *console)
{
	// release all the lines already requested

	for (int i = 0; i < console->n_mux_gpios; i++) {
		struct console_gpio *gpio = console->mux_gpios[i];
		if (console->server->debug) {
			printf("releasing gpio: %s\n", gpio->name);
		}
		gpiod_line_release(gpio->line);
	}
}

int set_console_gpios(struct console *console)
{
	int status = 0;

	if (console->server->debug) {
		printf("setting %ld gpios\n", console->n_mux_gpios);
	}

	for (int i = 0; i < console->n_mux_gpios; i++) {
		struct console_gpio *gpio = console->mux_gpios[i];

		if (console->server->debug) {
			printf("setting gpio %s=%d\n", gpio->name, gpio->value);
		}

		status = gpiod_line_request_output(gpio->line, "obmc-console",
						   gpio->value);
		if (status != 0) {
			warnx("could not set line %s as output\n", gpio->name);
			warnx("releasing all lines already requestd\n");
			release_console_gpios(console);
			return 1;
		}
	}

	return 0;
}

int console_timestamp(char *buffer, size_t size)
{
	size_t status;
	time_t rawtime;
	struct tm *timeinfo;

	time(&rawtime);
	timeinfo = gmtime(&rawtime);

	status = strftime(buffer, size, "%Y-%m-%d %H:%M:%S UTC", timeinfo);
	return !status;
}

int console_print_timestamped(struct console *console, char *message)
{
	int status;
	char buf_timestamp[80];
	status = console_timestamp((char *)buf_timestamp, 80);

	if (status != 0) {
		warnx("Error: unable to print timestamp");
		return status;
	}

	char *buf = malloc(sizeof(char) * strlen(message) + 200);
	sprintf(buf, "[obmc-console] %s %s\n", (char *)buf_timestamp, message);
	ringbuffer_queue(console->rb, (uint8_t *)buf, strlen(buf));

	free(buf);

	return 0;
}

static int console_deactivate_inner(struct console *console)
{
	console_print_timestamped(console, "DISCONNECTED");
	return 0;
}

int console_activate_inner(struct console *console)
{
	int status = set_console_gpios(console);

	if (status != 0) {
		warnx("Error: unable to set mux gpios.");
		return status;
	}

	struct console_server *server = console->server;

	for (size_t i = 0; i < server->n_consoles; i++) {
		struct console *other = server->consoles[i];
		if (other == console) {
			continue;
		}
		console_deactivate_inner(other);
	}

	console_print_timestamped(console, "CONNECTED");
	server->active_console = console;

	return 0;
}

int console_activate(struct console *console)
{
	const bool is_active = console->server->active_console == console;
	if (is_active) {
		if (console->server->debug) {
			printf("console already active / inactive, no-op\n");
		}
		return 0;
	}

	return console_activate_inner(console);
}
