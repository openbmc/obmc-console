
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#ifndef SYSCONFDIR
// Bypass compilation error due to -DSYSCONFDIR not provided
#define SYSCONFDIR
#endif

#include "config-test-utils.h"
#include "config.c"

static void execute_test(const char *input, const char *key,
			 const char *expected)
{
	struct config *ctx = mock_config_from_buffer(input);
	const char *found;

	if (!expected) {
		if (ctx->dict != NULL) {
			found = config_get_value(ctx, key);
			assert(!found);
			iniparser_freedict(ctx->dict);
		}

		free(ctx);
		return;
	}

	assert(ctx->dict != NULL);
	found = config_get_value(ctx, key);

	assert(found);
	assert(!strcmp(expected, found));

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
