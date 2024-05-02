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

In some hardware designs, multiple UARTS may be available behind a Mux,
which requires obmc-console to select one at a time.
The kernel will not be aware of this mux and the support can be implemented in userspace.

## Mux Support - Mux Control

The UART with the most first client connection will be chosen.
As long as the client is still active, this UART will be selected.

## Mux Support - Diagram

```
obmc-console-server
                             +------ UART0
                             | +---- UART1
                             | | +-- ...
                             | | |
+-----+  +------------+     +------+
|     |--| /dev/ttyS1 |-----| Mux  |
|     |  +------------+     +------+
|     |                         |
|     |-------------------------+ n gpios
+-----+

```

