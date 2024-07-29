#include <asm-generic/errno-base.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <poll.h>

#include "console-server-test-util.h"

int fork_off_console_client(char *console_id, pid_t *pid, int *parent_write)
{
	//int parent_write[2]; // child read, parent write
	int rc;

	rc = pipe(parent_write);
	if (rc != 0) {
		return EXIT_FAILURE;
	}

	printf("forking off console client\n");
	*pid = fork();

	if (*pid == 0) {
		// child
		close(parent_write[1]);

		rc = dup2(parent_write[0], STDIN_FILENO);
		if (rc < 0) {
			fprintf(stderr, "test: dup2(...) failure \n");
			exit(EXIT_FAILURE);
		}

		char *argv[] = { "obmc-console-client", "-i", console_id };

		char *client_path = getenv("client");
		execl(client_path, argv[0], argv[1], argv[2], NULL);

		exit(EXIT_FAILURE);
	}

	if (*pid > 0) {
		// parent
		rc = 0;
		close(parent_write[0]);

		int write_fd = parent_write[1];

		sleep(1);

		printf("test: write string to server\n");
		char *buf = "01234\n\n";
		ssize_t nwritten = write(write_fd, buf, 6);
		if (nwritten <= 0) {
			fprintf(stderr,
				"test: could not write to the console-client stdin\n");
			kill(*pid, SIGINT);
			rc = -1;
			goto exit_close;
		}

		printf("test: done write string to server\n");
		sleep(1);
	exit_close:

		return rc;
	}

	fprintf(stderr, "Error creating process\n");
	return EXIT_FAILURE;
}

int main(void)
{
	printf("\n\nTEST: obmc-console-client can write\n");

	int masterfd;
	struct sd_bus *bus;
	char *slave_filename = NULL;
	char console_id[200];
	randomize_console_id(console_id, "testclientwrite");
	int status;

	status = create_pty2(&masterfd, &slave_filename);

	if (status != 0) {
		fprintf(stderr, "Error: could not create pty\n");
		return EXIT_FAILURE;
	}

	pid_t pid;
	const char *console_ids[] = { console_id };
	status = fork_off_console_server(&bus, slave_filename,
					 (char **)console_ids, 1, &pid);
	if (status != 0) {
		kill(pid, SIGINT);
		close(masterfd);
		return EXIT_FAILURE;
	}

	int parent_write[2]; // child read, parent write
	pid_t client_pid;
	status = fork_off_console_client(console_id, &client_pid, parent_write);
	if (status != 0) {
		kill(pid, SIGINT);
		close(masterfd);
		return EXIT_FAILURE;
	}

	sleep(1);

	printf("using read to read from master pts\n");

	char str[200];
	uint32_t retry = 0;
	ssize_t nread = -1;

	while (retry < 10) {
		retry++;
		memset(str, 0, 200);
		nread = read(masterfd, str, 5);
		if (nread < 0) {
			if (errno == EIO) {
				fprintf(stderr, "retry read\n");
				continue;
			}

			fprintf(stderr,
				"error: could not fread from master pts\n");
			perror("error:");
			break;
		}

		if (nread == 0) {
			//EOF
			fprintf(stderr, "error: nread == 0\n");
			break;
		}
		printf("did read %ld bytes\n", nread);
		printf("read: %5s\n", str);
		break;
	}

	close(parent_write[1]);

	kill(client_pid, SIGINT);
	kill(pid, SIGINT);

	close(masterfd);

	assert(nread == 5);
	assert_contains(str, "0123");

	sd_bus_flush_close_unref(bus);

	printf("\nTEST PASSED\n");

	return EXIT_SUCCESS;
}
