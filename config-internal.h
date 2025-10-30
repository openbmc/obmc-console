// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright 2024 obmc-console authors

#pragma once

#include <iniparser/dictionary.h>

#define CONFIG_MAX_KEY_LENGTH 512

struct config {
	dictionary *dict;
};
