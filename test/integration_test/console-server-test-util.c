#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "console-server.h"
#include "dbus-test-utils.h"

#include "console-server-test-util.h"

const char *test_config_directory = "testconf.d";

void assert_contains(char *buf, char *str)
{
	char *ptr = strstr(buf, str);
	assert(ptr != NULL);
}

void assert_not_contains(char *buf, char *str)
{
	char *ptr = strstr(buf, str);
	assert(ptr == NULL);
}

char *console_server_process_n_consoles(char *slave_filename,
					char **console_ids,
					size_t n_console_ids)
{
	char *pty_filename = (char *)slave_filename;
	char *argv[20];
	int i = 0;
	argv[i++] = "";

	char cmd[200];
	sprintf(cmd, "rm -r %s", test_config_directory);
	int rc = system(cmd);
	(void)rc;

	mkdir(test_config_directory, 0755);

	for (size_t i = 0; i < n_console_ids; i++) {
		char config_filename[100];
		sprintf(config_filename, "%s/%s.conf", test_config_directory,
			console_ids[i]);

		FILE *config = fopen(config_filename, "w");

		if (config == NULL) {
			fprintf(stderr, "Error: could not open %s\n",
				config_filename);
			return NULL;
		}

		fprintf(config, "logfile = %s.log\n", console_ids[i]);
		fprintf(config, "console-id = %s\n", console_ids[i]);

		if (i == 0) {
			fprintf(config, "initially-active = true\n");
		}

		fclose(config);
	}

	argv[i++] = "--config-dir";
	argv[i++] = (char *)test_config_directory;

	// verbose logs makes testing easier
	argv[i++] = "--verbose";

	argv[i++] = pty_filename;
	int argc = i;

	console_server_main(argc, argv, true);
	return slave_filename;
}

void create_pty(FILE **master, char **slave_filename)
{
	int masterfd = open("/dev/ptmx", O_RDWR);
	grantpt(masterfd);
	unlockpt(masterfd);
	*slave_filename = ptsname(masterfd);

	*master = fdopen(masterfd, "w");
}

int create_pty2(int *masterfd, char **slave_filename)
{
	*masterfd = open("/dev/ptmx", O_RDWR);
	//experimental
	//*masterfd = open("/dev/ptmx", O_RDWR | O_NONBLOCK);

	if (*masterfd < 0) {
		return -1;
	}
	grantpt(*masterfd);
	unlockpt(*masterfd);
	*slave_filename = ptsname(*masterfd);

	if (*slave_filename == NULL) {
		return -1;
	}

	return 0;
}

char *read_console_log_file(char *console_id)
{
	char log_filename[200];
	sprintf(log_filename, "%s.log", console_id);

	FILE *logfile = fopen(log_filename, "r");
	if (logfile == NULL) {
		fprintf(stderr, "Error: could not open %s\n", log_filename);
		assert(false);
	}

	const size_t bufsize = 1024;
	// not freed, process will exit anyways
	char *buf = calloc(bufsize, 1);

	size_t nread = fread(buf, bufsize, 1, logfile);
	(void)nread;

	fclose(logfile);
	return buf;
}

int fork_off_console_server(struct sd_bus *bus, char *slave_filename,
			    char **console_ids, size_t n_console_ids,
			    pid_t *pid)
{
	int rc;
	if (n_console_ids == 0) {
		printf("called %s with no console ids.\n", __func__);
		return EXIT_FAILURE;
	}

	*pid = fork();

	if (*pid == 0) {
		console_server_process_n_consoles(slave_filename, console_ids,
						  n_console_ids);
		exit(EXIT_SUCCESS);
	}

	if (*pid > 0) {
		for (size_t i = 0; i < n_console_ids; i++) {
			rc = block_on_dbus_console_id(bus, console_ids[i]);
			if (rc != 0) {
				return rc;
			}
		}
		return 0;
	}

	fprintf(stderr, "Error creating process\n");
	return EXIT_FAILURE;
}

bool dbus_get_active_state(struct sd_bus *bus, char *console_id, int *status)
{
	char dbus_name[1024];
	char dbus_path[1024];
	sprintf(dbus_name, "xyz.openbmc_project.Console.%s", console_id);
	sprintf(dbus_path, "/xyz/openbmc_project/console/%s", console_id);

	bool reply;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	*status = sd_bus_get_property_trivial(
		bus, dbus_name, dbus_path,
		"xyz.openbmc_project.Console.Control", "Active", &error, 'b',
		&reply);

	if (*status < 0) {
		fprintf(stderr, "Error: Failed to get dbus property: %s\n",
			error.message);
		return reply;
	}

	*status = 0;
	return reply;
}

void run_with_dbus(bool (*test)(struct sd_bus *))
{
	struct sd_bus *bus;
	int status;
	bool pass;
	// for testing the user bus is preferred

	status = sd_bus_open_user(&bus);
	if (status < 0) {
		printf("Failed to connect to user bus: %s, retrying with system bus\n",
		       strerror(-status));
		sd_bus_unref(bus);
	}

	if (status >= 0) {
		goto has_bus;
	}

	status = sd_bus_open_system(&bus);
	if (status < 0) {
		printf("Failed to connect to system bus: %s\n",
		       strerror(-status));
		sd_bus_unref(bus);
		assert(false);
	}

has_bus:
	pass = test(bus);

	sd_bus_flush_close_unref(bus);
	assert(pass);
}
