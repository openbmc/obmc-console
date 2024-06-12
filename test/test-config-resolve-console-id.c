#include <assert.h>

#define TEST_CONSOLE_ID "test"

#include "config.c"

static struct config *config_mock(char *key, char *value)
{
	char buf[CONFIG_MAX_KEY_LENGTH];
	struct config *config;
	int rc;

	config = malloc(sizeof(struct config));
	assert(config != NULL);

	config->dict = dictionary_new(1);
	assert(config->dict != NULL);

	rc = snprintf(buf, CONFIG_MAX_KEY_LENGTH, ":%s", key);
	assert(rc >= 0 && (size_t)rc < sizeof(buf));

	dictionary_set(config->dict, buf, value);

	return config;
}

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

	ctx = config_mock("console-id", TEST_CONSOLE_ID);
	console_id = config_resolve_console_id(ctx, NULL);

	assert(!strcmp(console_id, TEST_CONSOLE_ID));

	config_fini(ctx);
}

static void test_independence_config_socket_id(void)
{
	const char *console_id;
	struct config *ctx;

	ctx = config_mock("socket-id", TEST_CONSOLE_ID);
	console_id = config_resolve_console_id(ctx, NULL);

	/*
	 * socket-id is no-longer an alias for console-id, therefore we should observe
	 * DEFAULT_CONSOLE_ID and not TEST_CONSOLE_ID
	 */
	assert(!strcmp(console_id, DEFAULT_CONSOLE_ID));

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
	const char *console_id;
	struct config *ctx;

	ctx = config_mock("console-id", "console");
	console_id = config_resolve_console_id(ctx, TEST_CONSOLE_ID);

	assert(config_get_value(ctx, "console-id"));
	assert(!strcmp(console_id, TEST_CONSOLE_ID));

	config_fini(ctx);
}

static void test_precedence_config_console_id(void)
{
	const char *console_id;
	struct config *ctx;

	ctx = config_mock("console-id", "console");
	console_id = config_resolve_console_id(ctx, NULL);

	assert(config_get_value(ctx, "console-id"));
	assert(!strcmp(console_id, "console"));

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

	return EXIT_SUCCESS;
}
