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

integration_tests = [
        'test-console-logs-to-file',
        'test-call-activate-dbus-method',
        'test-console-activate-deactivate',
        'test-console-activate-deactivates-other',
        'test-console-client-can-write',
        'test-console-client-can-read',
]

foreach t : tests
  test(t, executable(t, f'@t@.c', c_args: [ '-DSYSCONFDIR=""' ],
		     include_directories: '..'))
endforeach

test(
  'test-console-server-parse-args',
  executable(
    'test-console-server-parse-args',
    'test-console-server-parse-args.c',
    '../console-server.c',
    '../console-socket.c',
    '../console-gpio.c',
    '../console-dbus.c',
    '../ringbuffer.c',
    '../socket-handler.c',
    '../log-handler.c',
    '../tty-handler.c',
    '../config.c',
    '../util.c',
    '../console-ctl/activate-console.c',
    '../console-ctl/console-ctl.c',
    c_args: [
      '-DSYSCONFDIR=""',
      '-DLOCALSTATEDIR="@0@"'.format(get_option('localstatedir')),
    ],
    include_directories: '..',
    dependencies: [
      dependency('libsystemd'),
      dependency('libgpiod'),
    ]
  )
)

foreach t : integration_tests
test(
  t,
  executable(
    t,
    f'integration_test/@t@.c',
    'integration_test/dbus-test-utils.c',
    'integration_test/console-server-test-util.c',
    '../console-server.c',
    '../console-socket.c',
    '../console-gpio.c',
    '../console-dbus.c',
    '../ringbuffer.c',
    '../socket-handler.c',
    '../log-handler.c',
    '../tty-handler.c',
    '../config.c',
    '../util.c',
    '../console-client.c',
    '../console-ctl/activate-console.c',
    '../console-ctl/console-ctl.c',
    c_args: [
      '-g',
      '-DSYSCONFDIR=""',
      '-DLOCALSTATEDIR="@0@"'.format(get_option('localstatedir')),
    ],
    include_directories: '..',
    dependencies: [
      dependency('libsystemd'),
      dependency('libgpiod'),
    ]
  )
)
endforeach
