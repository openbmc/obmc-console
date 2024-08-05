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

socat = find_program('socat', native: true)
strace = find_program('strace', native: true)

meson.override_find_program('test-console-logs-to-file', files('test-console-logs-to-file'))
test('test-console-logs-to-file',
  strace,
  args: [
    '-tt', '-y', '-Y', '-f', '-e', 'exit_group,write,%signal',
    find_program('test-console-logs-to-file').full_path(),
    socat.full_path(), server.full_path()
  ],
  depends: [ server ],
  suite: 'itests'
)
