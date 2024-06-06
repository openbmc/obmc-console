#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

#include "console-server.h"

static void test_no_tty_device(void)
{
	printf("TEST: argv Parsing: no tty device\n");
	int rc;
	struct console_server_args args;
	char *argv[] = { "prog", "--console-id", "dev" };
	int argc = sizeof(argv) / sizeof(char *);
	rc = console_server_args_init(argc, argv, &args);

	assert(rc != 0);

	console_server_args_fini(&args);
}

static void test_parses_console_id(void)
{
	printf("TEST: argv Parsing: console-id\n");
	int rc;
	struct console_server_args args;
	char *argv[] = { "prog", "--console-id", "dev", "/dev/ttyS1" };
	int argc = sizeof(argv) / sizeof(char *);
	rc = console_server_args_init(argc, argv, &args);

	assert(rc == 0);
	assert(args.console_id != NULL);
	assert(strcmp(args.console_id, "dev") == 0);

	console_server_args_fini(&args);
}

static void test_single_config_file(void)
{
	printf("TEST: argv Parsing: config file\n");
	int rc;
	struct console_server_args args;
	char *argv[] = { "prog",     "--console-id", "dev",
			 "--config", "test.conf",    "/dev/ttyS1" };
	int argc = sizeof(argv) / sizeof(char *);
	rc = console_server_args_init(argc, argv, &args);

	assert(rc == 0);
	assert(args.console_id != NULL);
	assert(args.config_filename != NULL);
	assert(strcmp(args.config_filename, "test.conf") == 0);

	console_server_args_fini(&args);
}

int main(void)
{
	test_no_tty_device();
	test_parses_console_id();
	test_single_config_file();

	return EXIT_SUCCESS;
}
