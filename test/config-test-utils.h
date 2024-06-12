#pragma once

#include <stdlib.h>
#include <string.h>

struct config *config_mock(char *key, char *value);

struct config *mock_config_from_buffer(const char *input);
