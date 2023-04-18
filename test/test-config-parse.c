
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#ifndef SYSCONFDIR
// Bypass compilation error due to -DSYSCONFDIR not provided
#define SYSCONFDIR
#endif

#include "config.c"

static void test_config_parse_basic(void)
{
	struct config *ctx;
	const char *val;
	char *buf;

	ctx = calloc(1, sizeof(*ctx));
	buf = strdup("tty = ttyS0");
	config_parse(ctx, buf);
	free(buf);
	val = config_get_value(ctx, "tty");
	assert(!strcmp("ttyS0", val));
	config_fini(ctx);
}

static void test_config_parse_no_key(void)
{
	struct config *ctx;
	const char *val;
	char *buf;

	ctx = calloc(1, sizeof(*ctx));
	buf = strdup("= ttyS0");
	config_parse(ctx, buf);
	free(buf);
	val = config_get_value(ctx, "tty");
	assert(!val);
	config_fini(ctx);
}

static void test_config_parse_no_value(void)
{
	struct config *ctx;
	const char *val;
	char *buf;

	ctx = calloc(1, sizeof(*ctx));
	buf = strdup("tty =");
	config_parse(ctx, buf);
	free(buf);
	val = config_get_value(ctx, "tty");
	assert(!val);
	config_fini(ctx);
}

static void test_config_parse_no_operator(void)
{
	struct config *ctx;
	const char *val;
	char *buf;

	ctx = calloc(1, sizeof(*ctx));
	buf = strdup("tty ttyS0");
	config_parse(ctx, buf);
	free(buf);
	val = config_get_value(ctx, "tty");
	assert(!val);
	config_fini(ctx);
}

static void test_config_parse_empty(void)
{
	struct config *ctx;
	const char *val;
	char *buf;

	ctx = calloc(1, sizeof(*ctx));
	buf = strdup("");
	config_parse(ctx, buf);
	free(buf);
	val = config_get_value(ctx, "tty");
	assert(!val);
	config_fini(ctx);
}

int main(void)
{
	test_config_parse_basic();
	test_config_parse_no_key();
	test_config_parse_no_value();
	test_config_parse_no_operator();
	test_config_parse_empty();

	return EXIT_SUCCESS;
}
