#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "console-server.h"
#include "config.h"
#include "iniparser/iniparser.h"

struct config *config_mock(char *key, char *value)
{
	struct config *config = malloc(sizeof(struct config));

	config->dict = dictionary_new(1);

	char buf[CONFIG_MAX_KEY_LENGTH];
	snprintf(buf, CONFIG_MAX_KEY_LENGTH, ":%s", key);

	dictionary_set(config->dict, buf, value);

	return config;
}
