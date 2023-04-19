
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#ifndef SYSCONFDIR
// Bypass compilation error due to -DSYSCONFDIR not provided
#define SYSCONFDIR
#endif

#include "config.c"

static void execute_test(const char *input, const char *key,
			 const char *expected)
{
	struct config *ctx;
	const char *found;
	char *buf;

	ctx = calloc(1, sizeof(*ctx));
	buf = strdup(input);
	config_parse(ctx, buf);
	free(buf);
	found = config_get_value(ctx, key);
	if (!expected) {
		assert(!found);
	}
	if (expected) {
		assert(found);
		assert(!strcmp(expected, found));
	}
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
