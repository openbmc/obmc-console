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

integration_tests = [
  'test-console-logs-to-file',
  'test-console-socket-read-write',
  'test-console-client-can-read',
]

foreach t : integration_tests
bin = executable(
  t,
  f'integration_test/@t@.c',
  'integration_test/dbus-test-utils.c',
  'integration_test/console-server-test-util.c',
  '../console-socket.c',
  '../util.c',
  c_args: [
    '-g',
    '-DSYSCONFDIR=""',
    '-DLOCALSTATEDIR="@0@"'.format(get_option('localstatedir')),
    '-fsanitize=address',
  ],
  link_args: [
    '-fsanitize=address',
  ],
  include_directories: '..',
  dependencies: [
    dependency('libsystemd'),
    dependency('libgpiod'),
    iniparser_dep,
  ],
)

test(
  t,
  find_program('dbus-run-session'),
  args: [
    '--',
    files('wrapper.sh'),
    bin.full_path(),
    server_testing.full_path(),
    client_testing.full_path(),
  ],
)
endforeach
