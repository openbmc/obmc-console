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

#include "console-server.h"

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>

#define CONSOLE_SOCKET_PREFIX "obmc-console"

/* Build the socket path. */
ssize_t console_socket_path(socket_path_t sun_path, const char *id)
{
	ssize_t rc;

	if (!id) {
		errno = EINVAL;
		return -1;
	}

	rc = snprintf(sun_path + 1, sizeof(socket_path_t) - 1,
		      CONSOLE_SOCKET_PREFIX ".%s", id);
	if (rc < 0) {
		return rc;
	}

	if ((size_t)rc > (sizeof(socket_path_t) - 1)) {
		errno = 0;
		return -1;
	}

	sun_path[0] = '\0';

	return rc + 1 /* Capture NUL prefix */;
}

ssize_t console_socket_path_readable(const struct sockaddr_un *addr,
				     size_t addrlen, socket_path_t path)
{
	const char *src = (const char *)addr;
	size_t len;

	if (addrlen > SSIZE_MAX) {
		return -EINVAL;
	}

	len = addrlen - sizeof(addr->sun_family) - 1;
	memcpy(path, src + sizeof(addr->sun_family) + 1, len);
	path[len] = '\0';

	return (ssize_t)len; /* strlen() style */
}
