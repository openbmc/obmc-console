#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>

#include <iniparser/iniparser.h>

#include "console-server.h"
#include "config.h"
#include "config-internal.h"

struct config *config_mock(char *key, char *value)
{
	struct config *config = malloc(sizeof(struct config));

	config->dict = dictionary_new(1);

	char buf[CONFIG_MAX_KEY_LENGTH];
	snprintf(buf, CONFIG_MAX_KEY_LENGTH, ":%s", key);

	dictionary_set(config->dict, buf, value);

	return config;
}

struct config *mock_config_from_buffer(const char *input)
{
	struct config *ctx;
	int rc;

	int fd = memfd_create("test-parse-ini", 0);
	assert(fd != -1);

	FILE *f = fdopen(fd, "w+");
	assert(f != NULL);
	rc = fprintf(f, "%s\n", input);

	assert(rc > (int)strlen(input));

	fseek(f, 0, SEEK_SET);
	dictionary *dict = iniparser_load_file(f, "");

	close(fd);

	ctx = calloc(1, sizeof(*ctx));
	ctx->dict = dict;

	return ctx;
}
