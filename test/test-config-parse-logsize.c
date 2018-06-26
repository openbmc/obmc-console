#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

// Bypass compilation error due to -DSYSCONFDIR not provided
#define SYSCONFDIR

#include "config.c"
#include "console-server.h"

struct test_parse_size_unit {
	const char	*test_str;
	size_t		expected_size;
	int		expected_rc;
};

void test_config_parse_logsize(void)
{
	const struct test_parse_size_unit test_data[] = {
		{"0",		0,		-1},
		{"1",		1,		0},
		{"2B",		2,		0},
		{"3B ",		3,		0},
		{"4k",		4*1024,		0},
		{"5kB",		5*1024,		0},
		{"6M",		6*1024*1024,	0},
		{"7MB",		7*1024*1024,	0},
		{"4095M",	(4095ul << 20),	0},
		{"2G",		(2ul << 30),	0},
		{"8M\n",	8,		0},	/* Suffix ignored */
		{"9T",		9,		0},	/* Suffix ignored */
		{" 10",		10,		0},	/* Leading spaces ignored */
		{"11G",		0,		-1},	/* Overflow */
		{"4294967296",	0,		-1},	/* Overflow */
		{"4096M",	0,		-1},	/* Overflow */
		{"65535G",	0,		-1},	/* Overflow */
		{"xyz",		0,		-1},	/* Invalid */
		{"000",		0,		-1},	/* Invalid */
		{"0.1",		0,		-1},	/* Invalid */
	};
	const size_t num_tests = sizeof(test_data) /
				 sizeof(struct test_parse_size_unit);
	size_t size;
	int i, rc;

	for (i = 0; i < num_tests; i++) {
		rc = config_parse_logsize(test_data[i].test_str, &size);

		assert(rc == test_data[i].expected_rc);
		if (rc == 0)
			assert(size == test_data[i].expected_size);
	}
}

int main(void)
{
	test_config_parse_logsize();
	return EXIT_SUCCESS;
}
