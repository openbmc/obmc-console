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

meson.override_find_program('test-console-logs-to-file', files('test-console-logs-to-file'))
test('test-console-logs-to-file',
  find_program('test-console-logs-to-file'),
  args: [ socat.full_path(), server.full_path() ],
  depends: [ server ],
  suite: 'itests'
)

meson.override_find_program('test-console-client-can-read', files('test-console-client-can-read'))
test('test-console-client-can-read',
  find_program('test-console-client-can-read'),
  args: [ socat.full_path(), server.full_path(), client.full_path() ],
  depends: [ server, client ],
  suite: 'itests'
)

meson.override_find_program('test-console-client-can-write', files('test-console-client-can-write'))
test('test-console-client-can-write',
  find_program('test-console-client-can-write'),
  args: [ socat.full_path(), server.full_path(), client.full_path() ],
  depends: [ server, client ],
  suite: 'itests'
)

meson.override_find_program('test-console-socket-read', files('test-console-socket-read'))
test('test-console-socket-read',
  find_program('test-console-socket-read'),
  args: [ socat.full_path(), server.full_path() ],
  depends: [ server ],
  suite: 'itests'
)
