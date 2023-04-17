tests = [
	'test-ringbuffer-contained-read',
	'test-ringbuffer-contained-offset-read',
	'test-ringbuffer-read-commit',
	'test-ringbuffer-boundary-read',
	'test-ringbuffer-simple-poll',
	'test-ringbuffer-boundary-poll',
	'test-ringbuffer-poll-force',
	'test-config-parse-logsize',
	'test-client-escape'
]

foreach t : tests
  test(t, executable(t, f'@t@.c', c_args: [ '-DSYSCONFDIR=""' ],
		     include_directories: '..'))
endforeach
