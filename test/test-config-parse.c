
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#ifndef SYSCONFDIR
// Bypass compilation error due to -DSYSCONFDIR not provided
#define SYSCONFDIR
#endif

#include "config.c"

static struct config *mock_config_from_buffer(const char *input)
{
	struct config *ctx;
	ssize_t rc;

	int fd = memfd_create("test-parse-ini", 0);
	assert(fd != -1);

	const size_t len = strlen(input);
	rc = write(fd, input, len);

	assert(rc >= 0);
	assert((size_t)rc == len);

	rc = lseek(fd, 0, SEEK_SET);
	assert(rc == 0);

	FILE *f = fdopen(fd, "r");
	assert(f != NULL);

	dictionary *dict = iniparser_load_file(f, "");

	fclose(f);

	if (dict == NULL) {
		return NULL;
	}

	ctx = calloc(1, sizeof(*ctx));

	if (ctx) {
		ctx->dict = dict;
	}

	return ctx;
}

static void execute_test(const char *input, const char *key,
			 const char *expected)
{
	struct config *ctx = mock_config_from_buffer(input);
	const char *found;

	if (!expected) {
		if (ctx == NULL) {
			return;
		}

		found = config_get_value(ctx, key);
		assert(!found);

		goto cleanup;
	}

	assert(ctx->dict != NULL);
	found = config_get_value(ctx, key);

	assert(found);
	assert(!strcmp(expected, found));
cleanup:
	config_fini(ctx);
}

static void test_config_parse_basic(void)
{
	execute_test("tty = ttyS0", "tty", "ttyS0");
}

static void test_config_parse_no_key(void)
{
	execute_test("= ttyS0", "tty", NULL);
}

static void test_config_parse_no_value(void)
{
	execute_test("tty =", "tty", NULL);
}

static void test_config_parse_no_operator(void)
{
	execute_test("tty ttyS0", "tty", NULL);
}

static void test_config_parse_no_spaces(void)
{
	execute_test("tty=ttyS0", "tty", "ttyS0");
}

static void test_config_parse_empty(void)
{
	execute_test("", "tty", NULL);
}

int main(void)
{
	test_config_parse_basic();
	test_config_parse_no_key();
	test_config_parse_no_value();
	test_config_parse_no_operator();
	test_config_parse_no_spaces();
	test_config_parse_empty();

	return EXIT_SUCCESS;
}
