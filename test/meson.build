tests = [
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

tests_depend_iniparser = [
	'test-client-escape',
	'test-config-parse',
	'test-config-parse-bytesize',
	'test-config-resolve-console-id'
]

foreach ct : tests_depend_iniparser
  test(
    ct,
    executable(
      ct,
      f'@ct@.c',
      c_args: [ '-DSYSCONFDIR=""' ],
      dependencies: [ iniparser_dep ],
      include_directories: '..'
    )
  )
endforeach