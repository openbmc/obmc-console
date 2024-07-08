/**
 * Copyright © 2016 IBM
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

#include <stdint.h>
#include <termios.h>

struct config;

const char *config_get_section_value(struct config *config, const char *secname,
				     const char *name);
const char *config_get_value(struct config *config, const char *name);
struct config *config_init(const char *filename);
const char *config_resolve_console_id(struct config *config,
				      const char *id_arg);
void config_fini(struct config *config);

int config_parse_baud(speed_t *speed, const char *baud_string);
uint32_t parse_baud_to_int(speed_t speed);
speed_t parse_int_to_baud(uint32_t baud);
int config_parse_bytesize(const char *size_str, size_t *size);

int config_count_sections(struct config *config);
const char *config_get_section_name(struct config *config, int i);
