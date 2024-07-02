/**
 * Copyright © 2024 9elements
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdbool.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define debug(S)                                                               \
	if (debug_logging_enabled) {                                           \
		fprintf(stderr, "[debug] ");                                   \
		fprintf(stderr, S);                                            \
		fprintf(stderr, "\n");                                         \
	}

#define debug2(A, B)                                                           \
	if (debug_logging_enabled) {                                           \
		fprintf(stderr, "[debug] ");                                   \
		fprintf(stderr, A, B);                                         \
		fprintf(stderr, "\n");                                         \
	}

#define debug3(A, B, C)                                                        \
	if (debug_logging_enabled) {                                           \
		fprintf(stderr, "[debug] ");                                   \
		fprintf(stderr, A, B, C);                                      \
		fprintf(stderr, "\n");                                         \
	}

extern bool debug_logging_enabled;
