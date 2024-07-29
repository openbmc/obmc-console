#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "console-server.h"
#include "util.h"
#include "dbus-test-utils.h"

#include "console-server-test-util.h"

const char *test_config_file = "testconf";

void randomize_console_id(char *buf, char *console_id)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	srandom(ts.tv_nsec ^ getpid());

	sprintf(buf, "%sr%ld", console_id, random());
}

void assert_contains(char *buf, char *str)
{
	assert(*buf != '\0');
	assert(*str != '\0');
	char *ptr = strstr(buf, str);
	assert(ptr != NULL);
}

void assert_not_contains(char *buf, char *str)
{
	char *ptr = strstr(buf, str);
	assert(ptr == NULL);
}

static int console_server_prep_conf_file(char *config_filename,
					 char **console_ids,
					 size_t n_console_ids)
{
	FILE *config = fopen(config_filename, "w");

	assert(n_console_ids != 0);

	if (config == NULL) {
		fprintf(stderr, "Error: could not open %s\n", config_filename);
		return -1;
	}

	fprintf(config, "active-console = %s\n", console_ids[0]);

	for (size_t i = 0; i < n_console_ids; i++) {
		fprintf(config, "[%s]\n", console_ids[i]);
		fprintf(config, "logfile = %s.log\n", console_ids[i]);
	}

	fclose(config);
	return 0;
}

char *console_server_process_n_consoles(char *slave_filename,
					char **console_ids,
					size_t n_console_ids)
{
	int status;
	char *pty_filename = (char *)slave_filename;
	char *argv[20];
	int i = 0;
	argv[i++] = "";

	char unique_conf_file[100];
	randomize_console_id(unique_conf_file, (char *)test_config_file);

	status = console_server_prep_conf_file((char *)unique_conf_file,
					       console_ids, n_console_ids);
	if (status != 0) {
		fprintf(stderr, "test: could not prep conf dir\n");
		return NULL;
	}

	argv[i++] = "--config";
	argv[i++] = (char *)unique_conf_file;
	argv[i++] = pty_filename;

	char *server_path = getenv("server");
	assert(server_path != NULL);

	status = execl(server_path, argv[0], argv[1], argv[2], argv[3], NULL);

	assert(status == 0);

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

	size_t nread = fread(buf, 1, bufsize, logfile);

	printf("TEST: read %ld bytes from log file\n", nread);

	fclose(logfile);
	return buf;
}

int fork_off_console_server(struct sd_bus **bus, char *slave_filename,
			    char **console_ids, size_t n_console_ids,
			    pid_t *pid)
{
	int rc;
	if (n_console_ids == 0) {
		printf("called %s with no console ids.\n", __func__);
		return EXIT_FAILURE;
	}

	debug_logging_enabled = true;

	*pid = fork();

	if (*pid == 0) {
		console_server_process_n_consoles(slave_filename, console_ids,
						  n_console_ids);
		exit(EXIT_SUCCESS);
	}

	if (*pid > 0) {
		rc = sd_bus_default(bus);
		// sd_bus_flush_close_unref(bus);
		// will be done in the individual tests

		if (rc < 0) {
			fprintf(stderr, "Failed to connect to system bus: %s\n",
				strerror(-rc));
			sd_bus_unref(*bus);
			assert(false);
		}

		for (size_t i = 0; i < n_console_ids; i++) {
			rc = block_on_dbus_console_id(*bus, console_ids[i]);
			if (rc != 0) {
				return rc;
			}
		}
		return 0;
	}

	fprintf(stderr, "Error creating process\n");
	return EXIT_FAILURE;
}

#define dbus_obj_path_len 1024

const char *access_dbus_interface = "xyz.openbmc_project.Console.Access";

// does not return fd, only activates
int activate_console_by_id(struct sd_bus *bus, char *console_id)
{
	char dbus_name[dbus_obj_path_len];
	char dbus_path[dbus_obj_path_len];
	sprintf(dbus_name, "xyz.openbmc_project.Console.%s", console_id);
	sprintf(dbus_path, "/xyz/openbmc_project/console/%s", console_id);

	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int reply_fd = 0;
	int status = sd_bus_call_method(bus, dbus_name, dbus_path,
					access_dbus_interface, "Connect", &err,
					&reply, "");
	if (status < 0) {
		fprintf(stderr, "%s: Failed to issue method call: %s\n",
			__func__, err.message);
		reply_fd = EXIT_FAILURE;
		goto cleanup;
	}
	status = sd_bus_message_read(reply, "h", &reply_fd);
	if (status < 0) {
		fprintf(stderr,
			"dbus call 'Connect()': could not read from response");
		reply_fd = EXIT_FAILURE;
		goto cleanup;
	}

cleanup:
	sd_bus_error_free(&err);
	sd_bus_message_unrefp(&reply);

	return (status < 0) ? -1 : 0;
}
