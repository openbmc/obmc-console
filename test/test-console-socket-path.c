#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "console-server.h"

/* Helper to calculate expected addrlen */
static ssize_t expected_addrlen(const char *id)
{
	return offsetof(struct sockaddr_un, sun_path) +
	       strlen(CONSOLE_SOCKET_PREFIX) + strlen(".") + strlen(id) +
	       1 /* NUL prefix */;
}

/* Test that a valid id returns full addrlen */
static void test_console_socket_path_returns_full_addrlen(void)
{
	socket_path_t sun_path;
	const char *id = "test-console";
	ssize_t addrlen;

	addrlen = console_socket_path(sun_path, id);

	assert(addrlen > 0);
	assert(addrlen == expected_addrlen(id));

	printf("PASS: %s\n", __func__);
}

/* Test that returned addrlen is directly usable in bind()/connect() */
static void test_console_socket_path_addrlen_usable_in_bind(void)
{
	struct sockaddr_un addr;
	ssize_t addrlen;
	int sd;
	int rc;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;

	addrlen = console_socket_path(addr.sun_path, "test-bind");
	assert(addrlen > 0);

	sd = socket(AF_UNIX, SOCK_STREAM, 0);
	assert(sd >= 0);

	/* addrlen should be directly usable without any arithmetic */
	rc = bind(sd, (struct sockaddr *)&addr, addrlen);
	assert(rc == 0);

	close(sd);
	printf("PASS: %s\n", __func__);
}

/* Test that NULL id returns -1 and sets errno to EINVAL */
static void test_console_socket_path_null_id(void)
{
	socket_path_t sun_path;
	ssize_t addrlen;

	errno = 0;
	addrlen = console_socket_path(sun_path, NULL);

	assert(addrlen == -1);
	assert(errno == EINVAL);

	printf("PASS: %s\n", __func__);
}

/* Test that an id that exceeds buffer limits returns -1 and sets errno to 0 */
static void test_console_socket_path_id_too_long(void)
{
	socket_path_t sun_path;
	/* Create an id that will exceed buffer size */
	char long_id[sizeof(socket_path_t) + 1];
	ssize_t addrlen;

	memset(long_id, 'a', sizeof(long_id) - 1);
	long_id[sizeof(long_id) - 1] = '\0';

	errno = 0;
	addrlen = console_socket_path(sun_path, long_id);

	assert(addrlen == -1);
	assert(errno == 0);

	printf("PASS: %s\n", __func__);
}

/* Test that the NUL prefix (abstract socket) is correctly set */
static void test_console_socket_path_abstract_socket(void)
{
	socket_path_t sun_path;
	ssize_t addrlen;

	addrlen = console_socket_path(sun_path, "test-abstract");
	assert(addrlen > 0);

	/* First byte should be NUL for abstract socket */
	assert(sun_path[0] == '\0');

	printf("PASS: %s\n", __func__);
}

/* Test that the socket path contains the expected prefix and id */
static void test_console_socket_path_content(void)
{
	socket_path_t sun_path;
	const char *id = "test-content";
	char expected[sizeof(socket_path_t)];
	ssize_t addrlen;

	addrlen = console_socket_path(sun_path, id);
	assert(addrlen > 0);

	/* Build expected path (without NUL prefix) */
	snprintf(expected, sizeof(expected), CONSOLE_SOCKET_PREFIX ".%s", id);

	/* Compare path content after NUL prefix */
	assert(strcmp(sun_path + 1, expected) == 0);

	printf("PASS: %s\n", __func__);
}

int main(void)
{
	test_console_socket_path_returns_full_addrlen();
	test_console_socket_path_addrlen_usable_in_bind();
	test_console_socket_path_null_id();
	test_console_socket_path_id_too_long();
	test_console_socket_path_abstract_socket();
	test_console_socket_path_content();

	printf("\nAll tests passed.\n");
	return EXIT_SUCCESS;
}
