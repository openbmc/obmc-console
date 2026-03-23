/**
 * Copyright © 2016 IBM Corporation
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

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "util.h"

int write_buf_to_fd(int fd, const uint8_t *buf, size_t len)
{
	size_t pos;
	ssize_t rc;

	for (pos = 0; pos < len; pos += rc) {
		rc = write(fd, buf + pos, len - pos);
		if (rc <= 0) {
			warn("Write error");
			return -1;
		}
	}

	return 0;
}

int rotate_single_file(const char *filename, const char *rotate_filename,
		       int old_fd)
{
	int rc;

	/* close old fd (ignore errors) */
	if (old_fd >= 0) {
		close(old_fd);
	}

	/* rename old -> rotate */
	rc = rename(filename, rotate_filename);
	if (rc) {
		warn("Failed to rename %s to %s\n", filename, rotate_filename);
	}

	/* open new empty file */
	int new_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (new_fd < 0) {
		warn("Can't open log file %s\n", filename);
		return -1;
	}

	return new_fd;
}
