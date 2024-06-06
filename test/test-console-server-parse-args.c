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

	assert(args.n_config_filenames == 0);
	assert(args.debug == false);

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

	assert(args.n_config_filenames == 1);
	assert(args.config_filenames != NULL);
	assert(strcmp(args.config_filenames[0], "test.conf") == 0);

	assert(args.debug == false);

	console_server_args_fini(&args);
}

static void test_config_directory(void)
{
	printf("TEST: argv Parsing: config directory\n");
	int rc;
	struct console_server_args args;
	char *argv[] = { "prog",	 "--console-id", "dev",
			 "--config-dir", "test-dir",	 "/dev/ttyS1" };
	int argc = sizeof(argv) / sizeof(char *);

	mkdir("test-dir", 0755);
	FILE *f;
	f = fopen("test-dir/test1.conf", "w");
	if (f == NULL) {
		perror("could not open file");
	}
	assert(f != NULL);
	fclose(f);
	f = fopen("test-dir/test2.conf", "w");
	if (f == NULL) {
		perror("could not open file");
	}
	assert(f != NULL);
	fclose(f);

	rc = console_server_args_init(argc, argv, &args);

	assert(rc == 0);
	assert(args.console_id != NULL);

	assert(args.n_config_filenames == 2);
	assert(args.config_filenames != NULL);
	assert(args.config_filenames[0] != NULL);
	assert(args.config_filenames[1] != NULL);
	assert(strcmp(args.config_filenames[0], "test-dir/test1.conf") == 0 ||
	       strcmp(args.config_filenames[0], "test-dir/test2.conf") == 0);

	assert(args.debug == false);

	console_server_args_fini(&args);
}

int main(void)
{
	test_no_tty_device();
	test_parses_console_id();
	test_single_config_file();
	test_config_directory();

	return EXIT_SUCCESS;
}
