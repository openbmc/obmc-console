#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

#include "console-ctl.h"

static const struct option options[] = {
	{ "activate", no_argument, 0, 'a' },
	{ "console-id", required_argument, 0, 'i' },
	{ "verbose", no_argument, 0, 'v' },
	{ 0, 0, 0, 0 },
};

static void usage(const char *progname)
{
	fprintf(stderr,
		"usage: %s --activate --console-id=<NAME> [OPTION...]\n"
		"\n"
		"Options:\n"
		"  --activate \tActivate the console specified by --console-id\n"
		"  --console-id <NAME>\tSelect a console\n"
		"  --verbose \tprint additional information\n"
		"",
		progname);
}

int main(int argc, char **argv)
{
	char *console_id = NULL;
	bool activate = false;
	bool debug = false;
	for (;;) {
		int c;
		int idx;

		c = getopt_long(argc, argv, "i:", options, &idx);
		if (c == -1) {
			break;
		}

		switch (c) {
		case 'a':
			activate = true;
			break;
		case 'i':
			console_id = optarg;
			break;
		case 'v':
			debug = true;
			break;
		case 'h':
		case '?':
			usage(argv[0]);
			return EXIT_SUCCESS;
		}
	}

	if (!activate || console_id == NULL) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	return console_ctl(console_id, debug);
}
