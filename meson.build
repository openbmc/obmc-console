project('obmc-console', 'c',
    default_options: [
      'buildtype=debugoptimized',
      'warning_level=3',
      'werror=true',
      'c_std=gnu17',
      'tests=' + (meson.is_subproject() ? 'false' : 'true'),
    ],
    version: '1.1.0',
    meson_version: '>=1.1.0',
)

add_project_arguments('-D_GNU_SOURCE', language: 'c')

systemdsystemunitdir = dependency('systemd').get_variable('systemdsystemunitdir')
install_data('conf/obmc-console@.service.in',
             'conf/obmc-console@.socket.in',
             rename: [
               'obmc-console@.service',
               'obmc-console@.socket',
             ],
             install_dir: systemdsystemunitdir,
           )
if get_option('ssh').allowed()
  install_data('conf/obmc-console-ssh@.service.in',
            rename: [
              'obmc-console-ssh@.service'
            ],
            install_dir: systemdsystemunitdir,
          )
endif

if get_option('concurrent-servers')
  install_data('conf/client.2200.conf.in',
               rename: [ 'client.2200.conf' ],
               install_dir: systemdsystemunitdir)
else
  if get_option('ssh').allowed()
    install_data('conf/obmc-console-ssh.socket.in',
                rename: [ 'obmc-console-ssh.socket' ],
                install_dir: systemdsystemunitdir)
    install_data('conf/obmc-console-ssh@.service.d/use-socket.conf.in',
                rename: [ 'use-socket.conf' ],
                install_dir: systemdsystemunitdir / 'obmc-console-ssh@.service.d')
  endif
endif

udev = dependency('udev', required: get_option('udev'))
if udev.found()
  install_data('conf/80-obmc-console-uart.rules.in',
               rename: [ '80-obmc-console-uart.rules' ],
               install_dir: udev.get_variable('udevdir') / 'rules.d')
endif

iniparser_dep = dependency('iniparser')

sources_server = [
 'config.c',
 'console-dbus.c',
 'console-server.c',
 'console-socket.c',
 'console-mux.c',
 'log-handler.c',
 'ringbuffer.c',
 'socket-handler.c',
 'tty-handler.c',
 'util.c',
]

deps_server = [
 dependency('libsystemd'),
 iniparser_dep,
 dependency('libgpiod'),
 meson.get_compiler('c').find_library('rt')
]

server = executable('obmc-console-server',
           sources_server,
           c_args: [
             '-DLOCALSTATEDIR="@0@"'.format(get_option('localstatedir')),
             '-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
           ],
           dependencies: deps_server,
           install_dir: get_option('sbindir'),
           install: true)

server_testing = executable('obmc-console-server-testing',
           sources_server,
           c_args: [
             '-DLOCALSTATEDIR="@0@"'.format(get_option('localstatedir')),
             '-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
             '-fsanitize=address',
           ],
           link_args: [
             '-fsanitize=address',
           ],
           dependencies: deps_server,
           install_dir: get_option('sbindir'),
           install: true)

sources_client = [
 'config.c',
 'console-client.c',
 'console-socket.c',
 'util.c',
]

deps_client = [
 iniparser_dep,
]

client = executable('obmc-console-client',
           sources_client,
           c_args: [
             '-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
           ],
           dependencies: deps_client,
           install: true)

client_testing = executable('obmc-console-client-testing',
           sources_client,
           c_args: [
             '-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
             '-fsanitize=address',
           ],
           link_args: [
             '-fsanitize=address',
           ],
           dependencies: deps_client,
           install: true)

if get_option('tests')
  subdir('test')
endif
