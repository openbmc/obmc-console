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

static unsigned int get_seed(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_nsec ^ getpid();
}

void randomize_console_id(char *buf, char *console_id)
{
	srandom(get_seed());

	long r = random();
	sprintf(buf, "%sr%ld", console_id, r);
}

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

static int console_server_prep_conf_dir(char *test_conf_dir, char **console_ids,
					size_t n_console_ids)
{
	mkdir(test_conf_dir, 0755);

	for (size_t i = 0; i < n_console_ids; i++) {
		char config_filename[200];
		sprintf(config_filename, "%s/%s.conf", test_conf_dir,
			console_ids[i]);

		FILE *config = fopen(config_filename, "w");

		if (config == NULL) {
			fprintf(stderr, "Error: could not open %s\n",
				config_filename);
			return -1;
		}

		fprintf(config, "logfile = %s.log\n", console_ids[i]);
		fprintf(config, "console-id = %s\n", console_ids[i]);

		if (i == 0) {
			fprintf(config, "initially-active = true\n");
		}

		fclose(config);
	}
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

	char unique_conf_dir[100];
	randomize_console_id(unique_conf_dir, (char *)test_config_directory);

	status = console_server_prep_conf_dir((char *)unique_conf_dir,
					      console_ids, n_console_ids);
	if (status != 0) {
		fprintf(stderr, "test: could not prep conf dir\n");
		return NULL;
	}

	argv[i++] = "--config-dir";
	argv[i++] = (char *)unique_conf_dir;

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

#define dbus_obj_path_len 1024

const char *access_dbus_interface = "xyz.openbmc_project.Console.Access";

// does not return fd, onl activates
int activate_console_by_id(struct sd_bus *bus, char *console_id)
{
	connect_console_by_id(bus, console_id);
	return 0;
}

// returns fd
int connect_console_by_id(struct sd_bus *bus, char *console_id)
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

	// return file handle
	return reply_fd;
}
