check_PROGRAMS =

# Run all 'check' test programs
TESTS = $(check_PROGRAMS)

check_PROGRAMS += \
	test-ringbuffer-contained-read \
	test-ringbuffer-contained-offset-read \
	test-ringbuffer-read-commit \
	test-ringbuffer-boundary-read \
	test-ringbuffer-simple-poll \
	test-ringbuffer-boundary-poll \
	test-ringbuffer-poll-force

EXTRA_DIST = ringbuffer-test-utils.c

AM_CPPFLAGS = -I$(top_srcdir)
