// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright 2016 IBM Corporation

#include <err.h>
#include <unistd.h>

#include "console-server.h"

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
