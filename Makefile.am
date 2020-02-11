sbin_PROGRAMS = obmc-console-server
bin_PROGRAMS = obmc-console-client

EXTRA_DATA = \
	 conf/client.2200.conf \
	 conf/server.ttyVUART0.conf

if HAVE_SYSTEMD
EXTRA_DATA += \
	 conf/obmc-console@.service \
	 conf/obmc-console-ssh@.service \
	 conf/obmc-console-ssh@.service.d/use-socket.conf \
	 conf/obmc-console-ssh.socket
endif

obmc_console_server_CPPFLAGS = \
	-DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\"

obmc_console_server_SOURCES = \
	 console-server.c \
	 console-server.h \
	 util.c \
	 ringbuffer.c \
	 config.c \
	 log-handler.c \
	 socket-handler.c \
	 tty-handler.c \
	 console-socket.c

obmc_console_server_LDFLAGS = $(SYSTEMD_LIBS) -lrt
obmc_console_server_CFLAGS = $(SYSTEMD_CFLAGS)

obmc_console_client_CPPFLAGS = \
	-DSYSCONFDIR=\"$(sysconfdir)\"

obmc_console_client_SOURCES = \
	 console-client.c \
	 console-server.h \
	 console-socket.c \
	 config.c \
	 util.c

SUBDIRS = test
