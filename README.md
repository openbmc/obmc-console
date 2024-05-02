## To Build

To build this project, run the following shell commands:

```
meson setup build
meson compile -C build
```

To test:

```
meson test -C build
```

## To Run Server

Running the server requires a serial port (e.g. /dev/ttyS0):

```
touch obmc-console.conf
./obmc-console-server --config obmc-console.conf ttyS0
```

## To Connect Client

To connect to the server, simply run the client:

```
./obmc-console-client
```

To disconnect the client, use the standard `~.` combination.

## Underlying design

This shows how the host UART connection is abstracted within the BMC as a Unix
domain socket.

```
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
```

This supports multiple independent consoles. The `console-id` is a unique
portion for the unix domain socket created by the obmc-console-server instance.
The server needs to know this because it needs to know what to name the pipe;
the client needs to know it as it needs to form the abstract socket name to
which to connect.

## Mux Support

In some hardware designs, multiple UARTS may be available behind a Mux, which
requires obmc-console to select one at a time. The kernel will not be aware of
this mux and the support can be implemented in userspace.

For example, let's say behind `/dev/ttyS1` there are `UART0` and `UART1`, behind
a mux. GPIO `UART-MUX-CTL` can be used to select one. Then there will be 2
obmc-console-server processes, and one of them will be started with `--inactive`
flag.

They will both receive configuration detailing the gpios to be used to control
the mux and the values they should have when their `--console-id` is active.

## Mux Support - Mux Control

The utility `obmc-console-ctl` can be used to select the active UART, using the
`--console-id`.

It can find all the servers using the same device by reading
'ConflictingConsoleIds' property on 'xyz.openbmc_project.Console.Control' dbus
interface.

Then it can disconnect all the servers using the same device by calling
'Activate(false)' dbus method.

### Example with `/dev/ttyS1`

```
$ obmc-console-ctl activate --console-id console2
deactivated 'console1'
activated 'console2'
```

obmc-console-ctl can read 'ConflictingConsoleIds' property of console2 to find
that console1 is conflicting with it.

Then obmc-console-ctl calls dbus method `Activate(false)` on console1 which will
cause it to:

1. stop forwarding bytes
2. print a log message to its clients, if the server was connected before

Then it calls `Activate(true)` on the server gaining connection (console2),
which causes it to:

1. switch the mux using the gpios
2. print a log message to its clients, if the server was disconnected before
3. start forwarding bytes.

## Mux Support - Dbus interface

```
NAME                                 TYPE      SIGNATURE RESULT/VALUE FLAGS
xyz.openbmc_project.Console.Control  interface -         -            -
.Activate                            method    b         i            -
.Active                              property  b         true         -
.ConflictingConsoleIds               property  as        1 "console1" -
```

## Mux Support - Example Diagram with 2 server instances

```
                   UART2 -------+
                   UART1 -----+ |
                              | |
                              | |
             +------------+  +------+
             | /dev/ttyS1 |--| Mux  |
             +--+---+-----+  +-+----+
                |   |          |
+---------+-----+   |          | gpio
|console-1|---------|----------+
+---------+         |          |
+---------+---------+          |
|console-2|--------------------+
+---------+
```

## Sample Development Setup

For developing obmc-console, we can use pseudo terminals (pty's) in Linux.

The socat command will output names of 2 pty's, one of which is the master and
the other one is the slave. The master pty can be used to emulate a UART.

```
$ socat -d -d pty,raw,echo=0 pty,raw,echo=0
N PTY is /dev/pts/1
N PTY is /dev/pts/2

$ obmc-console-server --console-id dev /dev/pts/2
$ obmc-console-server --console-id devinactive --inactive /dev/pts/2

$ obmc-console-client -i dev

# this message should appear for the client
$ echo "hi" > /dev/pts/1

```
