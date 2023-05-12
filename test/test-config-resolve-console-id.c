#include <assert.h>

#define TEST_CONSOLE_ID "test"

#include "config.c"

static void test_independence_cmdline_optarg(void)
{
	const char *console_id;
	struct config *ctx;

	ctx = calloc(1, sizeof(*ctx));
	console_id = config_resolve_console_id(ctx, TEST_CONSOLE_ID);

	assert(!strcmp(console_id, TEST_CONSOLE_ID));

	config_fini(ctx);
}

static void test_independence_config_console_id(void)
{
	const char *console_id;
	struct config *ctx;
	char *buf;

	ctx = calloc(1, sizeof(*ctx));
	buf = strdup("console-id = " TEST_CONSOLE_ID);
	config_parse(ctx, buf);
	free(buf);
	console_id = config_resolve_console_id(ctx, NULL);

	assert(!strcmp(console_id, TEST_CONSOLE_ID));

	config_fini(ctx);
}

static void test_independence_config_socket_id(void)
{
	const char *console_id;
	struct config *ctx;
	char *buf;

	ctx = calloc(1, sizeof(*ctx));
	buf = strdup("socket-id = " TEST_CONSOLE_ID);
	config_parse(ctx, buf);
	free(buf);
	console_id = config_resolve_console_id(ctx, NULL);

	assert(!strcmp(console_id, TEST_CONSOLE_ID));

	config_fini(ctx);
}

static void test_independence_default(void)
{
	const char *console_id;
	struct config *ctx;

	ctx = calloc(1, sizeof(*ctx));
	console_id = config_resolve_console_id(ctx, NULL);

	assert(!strcmp(console_id, DEFAULT_CONSOLE_ID));

	config_fini(ctx);
}

static void test_precedence_cmdline_optarg(void)
{
	static const char *const config = "console-id = console\n"
					  "socket-id = socket\n";
	const char *console_id;
	struct config *ctx;
	char *buf;

	ctx = calloc(1, sizeof(*ctx));
	buf = strdup(config);
	config_parse(ctx, buf);
	free(buf);
	console_id = config_resolve_console_id(ctx, TEST_CONSOLE_ID);

	assert(config_get_value(ctx, "console-id"));
	assert(config_get_value(ctx, "socket-id"));
	assert(!strcmp(console_id, TEST_CONSOLE_ID));

	config_fini(ctx);
}

static void test_precedence_config_console_id(void)
{
	static const char *const config = "console-id = console\n"
					  "socket-id = socket\n";
	const char *console_id;
	struct config *ctx;
	char *buf;

	ctx = calloc(1, sizeof(*ctx));
	buf = strdup(config);
	config_parse(ctx, buf);
	free(buf);
	console_id = config_resolve_console_id(ctx, NULL);

	assert(config_get_value(ctx, "console-id"));
	assert(config_get_value(ctx, "socket-id"));
	assert(!strcmp(console_id, "console"));

	config_fini(ctx);
}

static void test_precedence_config_socket_id(void)
{
	static const char *const config = "socket-id = socket\n";
	const char *console_id;
	struct config *ctx;
	char *buf;

	ctx = calloc(1, sizeof(*ctx));
	buf = strdup(config);
	config_parse(ctx, buf);
	free(buf);
	console_id = config_resolve_console_id(ctx, NULL);

	assert(!config_get_value(ctx, "console-id"));
	assert(config_get_value(ctx, "socket-id"));
	assert(!strcmp(console_id, "socket"));

	config_fini(ctx);
}

int main(void)
{
	test_independence_cmdline_optarg();
	test_independence_config_console_id();
	test_independence_config_socket_id();
	test_independence_default();
	test_precedence_cmdline_optarg();
	test_precedence_config_console_id();
	test_precedence_config_socket_id();

	return EXIT_SUCCESS;
}
