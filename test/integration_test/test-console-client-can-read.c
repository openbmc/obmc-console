#include <asm-generic/errno-base.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <poll.h>

#include "console-client.h"

#include "console-server-test-util.h"

int fork_off_console_client_reader(char *console_id, pid_t *pid,
				   int *child_write)
{
	//int child_write[2]; // parent read, child write
	int rc;

	rc = pipe(child_write);
	if (rc != 0) {
		return EXIT_FAILURE;
	}

	printf("test: forking off console client\n");
	*pid = fork();

	if (*pid == 0) {
		// child
		close(child_write[0]);

		rc = dup2(child_write[1], STDOUT_FILENO);
		if (rc < 0) {
			fprintf(stderr, "test: dup2(...) failure \n");
			exit(EXIT_FAILURE);
		}

		char *argv[] = { "obmc-console-client", "-i", console_id };
		int argc = sizeof(argv) / sizeof(char *);
		console_client_main(argc, argv);
		exit(EXIT_FAILURE);
	}

	if (*pid > 0) {
		// parent
		rc = 0;
		close(child_write[1]);

		// Wait for console-client to be ready.
		// It needs to connect to the console-server to receive bytes.
		sleep(1);

		return rc;
	}

	fprintf(stderr, "test: Error creating process\n");
	return EXIT_FAILURE;
}

static bool test_console_client_can_read(struct sd_bus *bus)
{
	int masterfd;
	char *slave_filename = NULL;
	char console_id[200];
	randomize_console_id(console_id, "testclientread");
	int status;
	bool result = false;

	status = create_pty2(&masterfd, &slave_filename);

	if (status != 0) {
		fprintf(stderr, "test: error: could not create pty\n");
		return false;
	}

	pid_t pid;
	const char *console_ids[] = { console_id };
	status = fork_off_console_server(bus, slave_filename,
					 (char **)console_ids, 1, &pid);
	if (status != 0) {
		fprintf(stderr,
			"test: error: could not fork off console-server\n");
		kill(pid, SIGINT);
		close(masterfd);
		return false;
	}

	int child_write[2]; // parent read, child write
	pid_t client_pid;
	status = fork_off_console_client_reader(console_id, &client_pid,
						child_write);
	if (status != 0) {
		fprintf(stderr,
			"test: error: could not fork off console-client\n");
		result = false;
		goto cleanup_server;
	}

	//write to masterfd
	char *buf = "hello client\n";
	ssize_t nwritten = write(masterfd, buf, strlen(buf));

	if (nwritten != (int)strlen(buf)) {
		fprintf(stderr, "test: error: could not write to masterfd\n");
		perror("error");
		result = false;
		goto cleanup;
	}

	char str[201];
	uint32_t retry = 0;

	while (retry < 5) {
		ssize_t nread = -1;
		retry++;
		memset(str, 0, 201);
		nread = read(child_write[0], str, 200);
		if (nread < 0) {
			if (errno == EIO) {
				fprintf(stderr, "test: retry read\n");
				continue;
			}

			fprintf(stderr,
				"test: error: could not fread from child\n");
			perror("error:");
			break;
		}

		if (nread == 0) {
			//EOF
			fprintf(stderr, "test: error: nread == 0\n");
			break;
		}

		if (strstr(str, "hello client") != NULL) {
			printf("test: found message in stdout of console-client\n");
			result = true;
			break;
		}
	}

cleanup:
	close(child_write[0]);
	kill(client_pid, SIGINT);

cleanup_server:
	kill(pid, SIGINT);
	close(masterfd);

	return result;
}

int main(int argc, char *argv[])
{
	printf("\n\nTEST: obmc-console-client can read\n");
	run_with_dbus(test_console_client_can_read, argc, argv);

	printf("\nTEST PASSED\n");

	return EXIT_SUCCESS;
}
