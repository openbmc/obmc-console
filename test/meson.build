tests = [
	'test-client-escape',
	'test-config-parse',
	'test-config-parse-bytesize',
	'test-config-resolve-console-id',
	'test-ringbuffer-boundary-poll',
	'test-ringbuffer-boundary-read',
	'test-ringbuffer-contained-offset-read',
	'test-ringbuffer-contained-read',
	'test-ringbuffer-poll-force',
	'test-ringbuffer-read-commit',
	'test-ringbuffer-simple-poll',
]

foreach t : tests
  test(t, executable(t, f'@t@.c', c_args: [ '-DSYSCONFDIR=""' ],
		     include_directories: '..'))
endforeach
