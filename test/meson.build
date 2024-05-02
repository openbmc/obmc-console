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

test(
  'test-console-activate-deactivate',
  executable(
    'test-console-activate-deactivate',
    'test-console-activate-deactivate.c',
    'dbus-test-utils.c',
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
    '../console-ctl/dbus-helper.c',
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
