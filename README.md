# obmc-console

## To Build

To build this project, run the following shell commands:

    meson setup build
    meson compile -C build

To test:

    dbus-run-session meson test -C build

## To Run Server

Running the server requires a serial port (e.g. /dev/ttyS0):

    touch obmc-console.conf
    ./obmc-console-server --config obmc-console.conf ttyS0

## To Connect Client

To connect to the server, simply run the client:

    ./obmc-console-client

To disconnect the client, use the standard `~.` combination.

## Underlying design

This shows how the host UART connection is abstracted within the BMC as a Unix
domain socket.

                +---------------------------------------------------------------------------------------------+
                |                                                                                             |
                |       obmc-console-client       unix domain socket         obmc-console-server              |
                |                                                                                             |
                |     +----------------------+                           +------------------------+           |
                |     |   client.2200.conf   |  +---------------------+  | server.ttyVUART0.conf  |           |
            +---+--+  +----------------------+  |                     |  +------------------------+  +--------+-------+
    Network    | 2200 +-->                      +->+ @obmc-console.host0 +<-+                        <--+ /dev/ttyVUART0 |   UARTs
            +---+--+  | console-id = "host0" |  |                     |  |  console-id = "host0"  |  +--------+-------+
                |     |                      |  +---------------------+  |                        |           |
                |     +----------------------+                           +------------------------+           |
                |                                                                                             |
                |                                                                                             |
                |                                                                                             |
                +---------------------------------------------------------------------------------------------+

This supports multiple independent consoles. The `console-id` is a unique
portion for the unix domain socket created by the obmc-console-server instance.
The server needs to know this because it needs to know what to name the pipe;
the client needs to know it as it needs to form the abstract socket name to
which to connect.

## Mux Support

In some hardware designs, multiple UARTS may be available behind a Mux. Please
reference
[docs/mux-support.md](https://github.com/openbmc/obmc-console/blob/master/docs/mux-support.md)
in that case.

## Sample Development Setup

For developing obmc-console, we can use pseudo terminals (pty's) in Linux.

The socat command will output names of 2 pty's, one of which is the master and
the other one is the slave. The master pty can be used to emulate a UART.

    $ socat -d -d pty,raw,echo=0,link=pty1 pty,raw,echo=0,link=pty2

    $ obmc-console-server --console-id dev $(realpath pty2)

    $ obmc-console-client -i dev

    # this message should appear for the client
    $ echo "hi" > pty1
