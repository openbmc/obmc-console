#include <err.h>
#include <gpiod.h>
#include <stdlib.h>

#include "console-server.h"
#include "console-gpio.h"

static int set_mux_gpio_from_config(struct console *console,
				    struct config *config, int j)
{
	char name[100];
	char value_name[100];

	console->mux_gpios[j] = malloc(sizeof(struct console_gpio));
	struct console_gpio *gpio = console->mux_gpios[j];

	sprintf(name, "mux-gpio-%d", j + 1);
	sprintf(value_name, "mux-gpio-%d-value", j + 1);
	gpio->name = (char *)config_get_value(config, name);
	const char *gpio_value = config_get_value(config, value_name);

	if (gpio_value == NULL) {
		warnx("mux-gpio %s does not have property %s in config\n", name,
		      value_name);
		return 1;
	}

	if (strcmp(gpio_value, "0") == 0) {
		gpio->value = 0;
	} else if (strcmp(gpio_value, "1") == 0) {
		gpio->value = 1;
	} else {
		warnx("invalid value for %s: %s\n", value_name, gpio_value);
		return 1;
	}
	gpio->line = gpiod_chip_find_line(console->gpio_chip, gpio->name);

	if (gpio->line == NULL) {
		warnx("libgpio: could not find line %s\n", gpio->name);
	}

	gpio->line_offset = gpiod_line_offset(gpio->line);

	if (console->debug) {
		printf("added mux gpio %s with value %d\n", gpio->name,
		       gpio->value);
	}

	return 0;
}

int mux_gpios_init(struct console *console, struct config *config)
{
	const char *value;
	long count = 0;
	int status = 0;
	int i;

	if (console->debug) {
		printf("setting mux gpios from config\n");
	}

	const char *const chip_path = "/dev/gpiochip0";
	console->gpio_chip = NULL;

	for (i = 1;; i++) {
		char name[100];
		sprintf(name, "mux-gpio-%d", i);
		value = config_get_value(config, name);
		if (value == NULL) {
			break;
		}
	}
	count = i - 1;
	console->n_mux_gpios = count;

	if (console->debug) {
		printf("found %ld mux gpios in config\n", count);
	}

	if (console->n_mux_gpios == 0) {
		return 0;
	}

	if (console->debug) {
		printf("opening gpio chip %s\n", chip_path);
	}
	console->gpio_chip = gpiod_chip_open(chip_path);
	if (console->gpio_chip == NULL) {
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

	if (console->active) {
		return set_console_gpios(console);
	}

	return 0;
}

void mux_gpios_fini(struct console *console)
{
	if (console->gpio_chip != NULL) {
		release_console_gpios(console);
		gpiod_chip_close(console->gpio_chip);
	}
}

void release_console_gpios(struct console *console)
{
	// release all the lines already requested

	for (int i = 0; i < console->n_mux_gpios; i++) {
		struct console_gpio *gpio = console->mux_gpios[i];
		if (console->debug) {
			printf("releasing gpio: %s\n", gpio->name);
		}
		gpiod_line_release(gpio->line);
	}
}

int set_console_gpios(struct console *console)
{
	int status = 0;

	if (console->debug) {
		printf("setting %ld gpios\n", console->n_mux_gpios);
	}

	for (int i = 0; i < console->n_mux_gpios; i++) {
		struct console_gpio *gpio = console->mux_gpios[i];
		//gpiod_ctxless_set_value(chip_path, gpio->line_offset, gpio->value, false, "obmc-console-ctl", NULL, NULL);

		if (console->debug) {
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
