#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "console-server.h"
#include "console-ctl/activate-console.h"
#include "console-ctl/console-ctl.h"
#include "dbus-test-utils.h"

const char *log_filename = "test.log";
const char *should_be_in_log = "should-be-in-log";
const char *should_not_be_in_log = "should-not-be-in-log";
const char *test_console_id = "test";

static char *console_server_process(char *slave_filename, char *console_id,
				    bool active, char *conflicting_console_id)
{
	char *pty_filename = (char *)slave_filename;

	char config_filename[50];
	sprintf(config_filename, "%s.conf", console_id);

	FILE *config = fopen(config_filename, "w");
	fprintf(config, "logfile = %s\n", log_filename);
	if (conflicting_console_id != NULL) {
		fprintf(config, "conflicting-console-ids = %s\n",
			conflicting_console_id);
	}
	fclose(config);

	char *argv[20];
	int i = 0;
	argv[i++] = "";
	argv[i++] = "--console-id", argv[i++] = (char *)console_id;
	argv[i++] = "--config";
	argv[i++] = (char *)config_filename;
	if (!active) {
		argv[i++] = "--inactive";
	}
	argv[i++] = pty_filename;
	int argc = i;

	console_server_main(argc, argv, true);
	return slave_filename;
}

static void create_pty(FILE **master, char **slave_filename)
{
	int masterfd = open("/dev/ptmx", O_RDWR);
	grantpt(masterfd);
	unlockpt(masterfd);
	*slave_filename = ptsname(masterfd);

	*master = fdopen(masterfd, "w");
}

static char *read_console_log_file(void)
{
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

static void assert_logfile_states_disconnected(void)
{
	char *buf = read_console_log_file();

	char *ptr0 = strstr(buf, " DISCONNECTED");
	assert(ptr0 != NULL);

	char *ptr1 = strstr(buf, " CONNECTED");
	assert(ptr1 == NULL);

	free(buf);
}

static void assert_logfile(void)
{
	char *buf = read_console_log_file();

	char *ptr0 = strstr(buf, " DISCONNECTED");
	assert(ptr0 != NULL);

	char *ptr1 = strstr(buf, should_not_be_in_log);
	assert(ptr1 == NULL);

	char *ptr2 = strstr(buf, " CONNECTED");
	assert(ptr2 != NULL);

	char *ptr3 = strstr(buf, should_be_in_log);
	assert(ptr3 != NULL);

	// assert the order of these
	assert(ptr0 < ptr2);
	assert(ptr2 < ptr3);

	free(buf);
}

static int fork_off_console_server(struct sd_bus *bus, char *slave_filename,
				   char *console_id, bool active,
				   char *conflicting_console_id, pid_t *pid)
{
	*pid = fork();

	if (*pid == 0) {
		console_server_process(slave_filename, console_id, active,
				       conflicting_console_id);
		exit(EXIT_SUCCESS);
	}

	if (*pid > 0) {
		return block_on_dbus_console_id(bus, console_id);
	}

	fprintf(stderr, "Error creating process\n");
	return EXIT_FAILURE;
}

static bool test_activate_deactivate_console_dbus(struct sd_bus *bus)
{
	FILE *master = NULL;
	char *slave_filename = NULL;

	create_pty(&master, &slave_filename);

	if (master == NULL || slave_filename == NULL) {
		fprintf(stderr, "Error: could not create pty\n");
		return false;
	}

	pid_t pid;
	int status = fork_off_console_server(
		bus, slave_filename, (char *)test_console_id, true, NULL, &pid);
	if (status != 0) {
		return false;
	}

	activate_console_by_id(bus, (char *)test_console_id, false, false);
	fprintf(master, "%s\n", should_not_be_in_log);

	activate_console_by_id(bus, (char *)test_console_id, true, false);
	fprintf(master, "%s\n", should_be_in_log);

	// sleep to allow for writing logs
	sleep(1);

	kill(pid, SIGINT);

	assert_logfile();

	return true;
}

static bool test_start_inactive_console_server_dbus(struct sd_bus *bus)
{
	FILE *master = NULL;
	char *slave_filename = NULL;
	(void)bus;

	create_pty(&master, &slave_filename);

	if (master == NULL || slave_filename == NULL) {
		fprintf(stderr, "Error: could not create pty\n");
		return false;
	}

	pid_t pid;
	int status = fork_off_console_server(bus, slave_filename,
					     (char *)test_console_id, false,
					     NULL, &pid);
	if (status != 0) {
		return false;
	}

	kill(pid, SIGINT);

	assert_logfile_states_disconnected();

	return true;
}

static bool dbus_get_active_state(struct sd_bus *bus, char *console_id,
				  int *status)
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

static bool
test_console_ctl_deactivates_conflicting_console_id_dbus(struct sd_bus *bus)
{
	FILE *master = NULL;
	char *slave_filename = NULL;
	bool active1;
	bool active2;
	int status;

	create_pty(&master, &slave_filename);

	if (master == NULL || slave_filename == NULL) {
		fprintf(stderr, "Error: could not create pty\n");
		return false;
	}

	pid_t pid1;
	pid_t pid2;
	status = fork_off_console_server(bus, slave_filename, "testconsole1",
					 false, "testconsole2", &pid1);
	if (status != 0) {
		return false;
	}
	status = fork_off_console_server(bus, slave_filename, "testconsole2",
					 true, "testconsole1", &pid2);
	if (status != 0) {
		return false;
	}

	active1 = dbus_get_active_state(bus, "testconsole1", &status);
	if (status != 0) {
		return false;
	}
	active2 = dbus_get_active_state(bus, "testconsole2", &status);
	if (status != 0) {
		return false;
	}
	assert(active1 == false);
	assert(active2 == true);

	// activate the inactive console
	console_ctl_bus(bus, "testconsole1", false);

	active1 = dbus_get_active_state(bus, "testconsole1", &status);
	if (status != 0) {
		return false;
	}
	active2 = dbus_get_active_state(bus, "testconsole2", &status);
	if (status != 0) {
		return false;
	}
	assert(active1 == true);
	assert(active2 == false);

	kill(pid1, SIGINT);
	kill(pid2, SIGINT);
	return true;
}

static void run_with_dbus(bool (*test)(struct sd_bus *))
{
	struct sd_bus *bus;
	int status;
	bool pass;
	// for testing the user bus is preferred

	status = sd_bus_open_user(&bus);
	if (status < 0) {
		printf("Failed to connect to user bus: %s\n",
		       strerror(-status));
		assert(false);
	}

	if (status >= 0) {
		goto has_bus;
	}

	status = sd_bus_open_system(&bus);
	if (status < 0) {
		printf("Failed to connect to system bus: %s\n",
		       strerror(-status));
	}

has_bus:
	pass = test(bus);
	sd_bus_unref(bus);
	assert(pass);
}

static void test_start_inactive_console_server(void)
{
	run_with_dbus(test_start_inactive_console_server_dbus);
}

static void test_activate_deactivate_console(void)
{
	run_with_dbus(test_activate_deactivate_console_dbus);
}

static void test_console_ctl_deactivates_conflicting_console_id(void)
{
	run_with_dbus(test_console_ctl_deactivates_conflicting_console_id_dbus);
}

int main(void)
{
	printf("TEST 1\n");
	test_activate_deactivate_console();
	printf("TEST 2\n");
	test_start_inactive_console_server();
	printf("TEST 3\n");
	test_console_ctl_deactivates_conflicting_console_id();

	return EXIT_SUCCESS;
}
