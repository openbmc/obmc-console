#include "console-server.h"
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "console-server-test-util.h"

static int test_write_socket(int masterfd, int fd)
{
	printf("TEST: testing write to socket\n");

	ssize_t status;
	const size_t bufsize = 100;
	char buf[bufsize];

	// write to socket
	memset(buf, 0, bufsize);
	sprintf(buf, "hello3\n\n");
	status = write(fd, buf, 7);

	if (status <= 0) {
		perror("");
		return 1;
	}

	// read form masterfd
	memset(buf, 0, bufsize);
	ssize_t nread = read(masterfd, buf, 7);

	assert(nread == strlen("hello3\n"));

	assert_contains(buf, "hello3\n");

	return 0;
}

static int test_read_socket(int masterfd, int fd)
{
	printf("TEST: testing read from socket\n");

	const size_t bufsize = 100;
	char buf[bufsize];

	// write to masterfd
	memset(buf, 0, bufsize);
	sprintf(buf, "hello2\n\n");
	ssize_t nwrite = write(masterfd, buf, 7);

	assert(nwrite == 7);

	// read from socket
	ssize_t nread = read(fd, buf, 7);

	assert(nread == strlen("hello2\n"));
	assert_contains(buf, "hello2\n");

	return 0;
}

static bool test_console_socket_read_write(struct sd_bus *bus)
{
	char console_id_1[200];
	randomize_console_id(console_id_1, "testconsole1rw");

	int masterfd;
	int status;
	char *slave_filename = NULL;

	status = create_pty2(&masterfd, &slave_filename);

	if (status != 0) {
		fprintf(stderr, "Error: could not create pty\n");
		return false;
	}

	pid_t pid;
	const char *console_ids[] = { console_id_1 };
	status = fork_off_console_server(bus, slave_filename,
					 (char **)console_ids, 1, &pid);
	if (status != 0) {
		close(masterfd);
		return false;
	}

	int socket_sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (socket_sd < 0) {
		status = 1;
		goto cleanup;
	}

	struct sockaddr_un addr;
	socket_path_t path;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	ssize_t len = console_socket_path(addr.sun_path, console_id_1);
	if (len < 0) {
		if (errno) {
			warn("Failed to configure socket: %s", strerror(errno));
		} else {
			warn("Socket name length exceeds buffer limits");
		}
		goto cleanup;
	}

	status = connect(socket_sd, (struct sockaddr *)&addr,
			 sizeof(addr) - sizeof(addr.sun_path) + len);

	if (status != 0) {
		console_socket_path_readable(&addr, len, path);
		warn("Can't connect to console server '@%s'", path);
		perror("");
		goto cleanup;
	}

	status = test_write_socket(masterfd, socket_sd);

	if (status != 0) {
		goto cleanup;
	}

	status = test_read_socket(masterfd, socket_sd);

cleanup:

	kill(pid, SIGINT);

	close(socket_sd);
	close(masterfd);

	return (status == 0) ? true : false;
}

int main(int argc, char *argv[])
{
	printf("\n\nTEST: console socket read/write\n");
	run_with_dbus(test_console_socket_read_write, argc, argv);

	printf("\nTEST PASSED\n");

	return EXIT_SUCCESS;
}
