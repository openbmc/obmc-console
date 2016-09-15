sbin_PROGRAMS = obmc-console-server
bin_PROGRAMS = obmc-console-client

EXTRA_DIST = obmc-console.conf.sample

obmc_console_server_CPPFLAGS = \
	-DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\"

obmc_console_server_SOURCES = \
	 console-server.c \
	 console-server.h \
	 util.c \
	 config.c \
	 log-handler.c \
	 socket-handler.c \
	 tty-handler.c \
	 console-socket.c

obmc_console_client_SOURCES = \
	 console-client.c \
	 console-server.h \
	 console-socket.c \
	 util.c

#SUBDIRS = test
