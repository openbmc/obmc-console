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

# List of tests and whether they require the client
itests = [
  [ 'test-console-logs-to-file', false ],
  [ 'test-console-socket-read', false ],
  [ 'test-console-socket-write', false ],
  [ 'test-console-client-can-read', true ],
  [ 'test-console-client-can-write', true ],
  [ 'test-multiple-consoles', false ],
]

foreach entry : itests
  ct = entry[0]
  requires_client = entry[1]

  meson.override_find_program(ct, files(ct))

  if requires_client
    test_deps = [ server, client ]
    test_args = [ socat.full_path(), server.full_path(), client.full_path() ]
  else
    test_deps = [ server ]
    test_args = [ socat.full_path(), server.full_path() ]
  endif

  test(ct,
    find_program(ct),
    args: test_args,
    depends: test_deps,
    suite: 'itests'
  )
endforeach
