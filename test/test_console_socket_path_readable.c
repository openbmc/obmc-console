#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/un.h>

// Derive a socket_path_t matching the function's expectations.
// It should hold up to sun_path plus a terminating NUL.
enum { SOCKET_PATH_MAX = (int)sizeof(((struct sockaddr_un *)0)->sun_path) };
typedef char socket_path_t[SOCKET_PATH_MAX + 1];

// Function under test
ssize_t console_socket_path_readable(const struct sockaddr_un *addr,
				     size_t addrlen, socket_path_t path)
{
	size_t len;

	if (addrlen > SSIZE_MAX) {
		return -EINVAL;
	}

	if (addr->sun_path[0] == '\0') {
		// Abstract namespace: name is the next (addrlen - 1) bytes
		len = addrlen > 0 ? addrlen - 1 : 0;
		memcpy(path, addr->sun_path + 1, len);
	} else {
		// Filesystem path: treat as a normal C string
		len = strnlen(addr->sun_path, sizeof(addr->sun_path));
		memcpy(path, addr->sun_path, len);
	}
	path[len] = '\0';
	return (ssize_t)len; /* strlen() style */
}

// Helpers
static void fill_bytes(char *buf, size_t n, char c)
{
	for (size_t i = 0; i < n; i++) {
		buf[i] = c;
	}
}

static void test_filesystem_basic(void)
{
	struct sockaddr_un addr = { 0 };
	socket_path_t out;

	const char *src = "/tmp/test.sock";
	strncpy(addr.sun_path, src,
		sizeof(addr.sun_path)); // ensure NUL within sun_path

	ssize_t ret =
		console_socket_path_readable(&addr, /*addrlen ignored*/ 0, out);
	assert(ret == (ssize_t)strlen(src));
	assert(strcmp(out, src) == 0);
}

static void test_abstract_basic(void)
{
	struct sockaddr_un addr = { 0 };
	socket_path_t out;

	// Abstract name "abc": sun_path[0] == '\0', then 'a','b','c'
	addr.sun_path[0] = '\0';
	addr.sun_path[1] = 'a';
	addr.sun_path[2] = 'b';
	addr.sun_path[3] = 'c';

	// For this implementation, len = addrlen - 1 when abstract.
	size_t addrlen = 1 + 3; // 3-byte name
	ssize_t ret = console_socket_path_readable(&addr, addrlen, out);
	assert(ret == 3);
	assert(out[0] == 'a' && out[1] == 'b' && out[2] == 'c' &&
	       out[3] == '\0');
}

static void test_abstract_len_zero(void)
{
	struct sockaddr_un addr = { 0 };
	socket_path_t out;

	addr.sun_path[0] = '\0';
	// addrlen = 0 => len = 0
	ssize_t ret = console_socket_path_readable(&addr, 0, out);
	assert(ret == 0);
	assert(out[0] == '\0');
}

static void test_addrlen_gt_SSIZE_MAX(void)
{
	struct sockaddr_un addr = { 0 };
	socket_path_t out;
	// Initialize path to detect unintended writes
	strlcpy(out, "UNCHANGED");

	size_t addrlen = (size_t)SSIZE_MAX + 1; // larger than SSIZE_MAX
	ssize_t ret = console_socket_path_readable(&addr, addrlen, out);
	assert(ret == -EINVAL);
	assert(strcmp(out, "UNCHANGED") ==
	       0); // early return should leave path untouched
}

static void test_filesystem_no_nul_in_sun_path(void)
{
	struct sockaddr_un addr = { 0 };
	socket_path_t out;

	// Fill sun_path with non-NUL bytes to force strnlen to hit the cap.
	fill_bytes(addr.sun_path, sizeof(addr.sun_path), 'A');

	ssize_t ret = console_socket_path_readable(&addr, 0, out);
	assert(ret == (ssize_t)sizeof(addr.sun_path));
	// Output should be exactly sizeof(sun_path) 'A's followed by a NUL.
	for (size_t i = 0; i < sizeof(addr.sun_path); i++) {
		assert(out[i] == 'A');
	}
	assert(out[sizeof(addr.sun_path)] == '\0');
}

int main(void)
{
	test_filesystem_basic();
	test_abstract_basic();
	test_abstract_len_zero();
	test_addrlen_gt_SSIZE_MAX();
	test_filesystem_no_nul_in_sun_path();

	puts("All tests passed.");
	return 0;
}
