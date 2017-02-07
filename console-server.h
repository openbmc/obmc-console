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

#include <poll.h>
#include <stdint.h>
#include <stdbool.h>

struct console;
struct config;

/* handler API */
enum {
	HANDLER_OK = 0,
	HANDLER_EXIT,
};

struct handler {
	const char	*name;
	int		(*init)(struct handler *handler,
				struct console *console,
				struct config *config);
	int		(*data_in)(struct handler *handler,
				uint8_t *buf, size_t len);
	void		(*fini)(struct handler *handler);
	bool		active;
};

#define __handler_name(n) __handler_  ## n
#define  _handler_name(n) __handler_name(n)

#define console_register_handler(h) \
	static const \
		__attribute__((section("handlers"))) \
		__attribute__((used)) \
		struct handler * _handler_name(__COUNTER__) = h;

int console_data_out(struct console *console, const uint8_t *data, size_t len);

/* poller API */
struct poller;

enum poller_ret {
	POLLER_OK = 0,
	POLLER_REMOVE,
	POLLER_EXIT,
};

typedef enum poller_ret (*poller_fn_t)(struct handler *handler,
					int revents, void *data);

struct poller *console_register_poller(struct console *console,
		struct handler *handler, poller_fn_t poller_fn,
		int fd, int events, void *data);

void console_unregister_poller(struct console *console, struct poller *poller);

/* ringbuffer API */
enum ringbuffer_poll_ret {
	RINGBUFFER_POLL_OK = 0,
	RINGBUFFER_POLL_REMOVE,
};

typedef enum ringbuffer_poll_ret (*ringbuffer_poll_fn_t)(void *data,
		size_t force_len);

struct ringbuffer;
struct ringbuffer_consumer;

struct ringbuffer *ringbuffer_init(size_t size);
void ringbuffer_fini(struct ringbuffer *rb);

struct ringbuffer_consumer *ringbuffer_consumer_register(struct ringbuffer *rb,
		ringbuffer_poll_fn_t poll_fn, void *data);

void ringbuffer_consumer_unregister(struct ringbuffer_consumer *rbc);

int ringbuffer_queue(struct ringbuffer *rb, uint8_t *data, size_t len);

size_t ringbuffer_dequeue_peek(struct ringbuffer_consumer *rbc, size_t offset,
		uint8_t **data);

int ringbuffer_dequeue_commit(struct ringbuffer_consumer *rbc, size_t len);

/* config API */
struct config;
const char *config_get_value(struct config *config, const char *name);
struct config *config_init(const char *filename);
void config_fini(struct config *config);

/* socket paths */
extern const char *console_socket_path;
extern const size_t console_socket_path_len;
extern const char *console_socket_path_readable;

/* utils */
int write_buf_to_fd(int fd, const uint8_t *buf, size_t len);

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define offsetof(type, member) \
	((unsigned long)&((type *)NULL)->member)

#define container_of(ptr, type, member) \
	((type *)((void *)((ptr) - offsetof(type, member))))

#define BUILD_ASSERT(c) \
	do { \
		char __c[(c)?1:-1] __attribute__((unused)); \
	} while (0)
