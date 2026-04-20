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
    test(
        t,
        executable(
            t,
            f'@t@.c',
            c_args: ['-DSYSCONFDIR=""'],
            include_directories: '..',
        ),
    )
endforeach

tests_depend_iniparser = [
    'test-client-escape',
    'test-config-parse',
    'test-config-parse-bytesize',
    'test-config-resolve-console-id',
]

foreach ct : tests_depend_iniparser
    test(
        ct,
        executable(
            ct,
            f'@ct@.c',
            c_args: ['-DSYSCONFDIR=""'],
            dependencies: [iniparser_dep],
            include_directories: '..',
        ),
    )
endforeach

socat = find_program('socat', native: true)

server_tests = [
    'test-console-logs-to-file',
    'test-console-logs-to-file-no-sections',
    'test-console-socket-read',
    'test-console-socket-write',
    'test-multiple-consoles',
]

foreach st : server_tests
    test(
        st,
        find_program(st),
        args: [socat.full_path(), server.full_path()],
        depends: [server],
        suite: 'itests',
    )
endforeach

client_tests = [
    ['test-console-client-can-read',    client],
    ['test-console-client-can-write',   client],
    ['test-console-client-no-args',     client],
    ['test-console-client-reconnect',   client_reconnect],
]

foreach ct : client_tests
    test(
        ct[0],
        find_program(ct[0]),
        args: [socat.full_path(), server.full_path(), ct[1].full_path()],
        depends: [server, ct[1]],
        suite: 'itests',
    )
endforeach
