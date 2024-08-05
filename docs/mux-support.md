# Mux Support

In some hardware designs, multiple UARTS may be available behind a Mux, which
requires obmc-console to select one at a time.

For example, let's say behind `/dev/ttyS0` there are `UART1` and `UART2`, behind
a mux. GPIO `UART-MUX-CTL` can be used to select one. This scenario is shown in
the [Example Diagram](#example-diagram)

Then there will be one obmc-console-server process, and 2 consoles will be
created in this server process.

The obmc-console-server will receive configuration for both consoles detailing
the gpios to be used to control the mux and the values they should have when
they are active.

The implementation documented here has been made based on the
[design document](https://github.com/openbmc/docs/blob/master/designs/uart-mux-support.md)

## Userspace Implementation

The kernel will not be aware of this mux and the support can be implemented in
userspace.

Reasons for this are that the hardware this is for is not static (blade server)
and thus cannot be represented by devicetree. Also, it is unclear how the
virtual uart should notify a process that the mux state has changed.

When a userspace implementation is available for reference, this may later pave
the way for a kernel implementation if someone wants to do that.

## Configuration Example

The configuration is similar to i2c-mux-gpio in the linux kernel.

The order of GPIOs listed in 'mux-gpios' forms the LSB-first bit representation
of an N-bit number that is taken from 'mux-index'.

For declaring the different consoles, the section name, e.g. `[host1]` must be
the same as the console-id. All of the section names need to be unique.

```sh
$ cat server.conf
mux-gpios = MUX_CTL

[host1]
mux-index = 0
logfile = /var/log/console-host1.log

[host2]
mux-index = 1
logfile = /var/log/console-host2.log
```

Now the server can be started. See the [Dbus Interface](#dbus-interface-example)
and [Example Diagram](#example-diagram)

```sh
obmc-console-server --config server.conf /dev/ttyS0
```

## Mux Control

Mux Control happens implicitly via connections. When a client connects to a
console, the new connection is accepted and the console-server switches the mux
to this console. Any clients connected to other consoles are disconnected.

### Mux Control - Example

```sh
$ obmc-console-client -i host2 &
[1] 3422
$ obmc-console-client -i host1
```

Connecting to console 'host1' will cause console 'host2' to:

1. stop forwarding bytes
2. print a log message to its clients, if the server was connected before, see
   [Mux Control Log](#mux-control-log)

Then the following happens for console 'host1':

1. switch the mux using the gpios
2. print a log message to its clients, if the server was disconnected before,
   see [Mux Control Log](#mux-control-log)
3. start forwarding bytes.

## Mux Control Log

Whenever the mux is switched, there should be a way for people reading the log
to know that that console was (dis)connected, and at which time. Otherwise there
may be confusion as to why there is a gap in the logs.

So obmc-console-server will print one of these messages to all clients:

```sh
[obmc-console] %Y-%m-%d %H:%M:%S UTC CONNECTED
```

```sh
[obmc-console] %Y-%m-%d %H:%M:%S UTC DISCONNECTED
```

### Mux Control Log Disclaimer

Note that this log message is not a reliable source of information, and is only
provided as a convenience feature. This same log message could be printed by
anything that's connected to the uart on the other side.

The exact format of this log message is not fixed and could change.

## Dbus Interface Example

```sh
$ busctl list
...
xyz.openbmc_project.Console.host1 926 obmc-console-server root ...
xyz.openbmc_project.Console.host2 926 obmc-console-server root ...
...
```

```sh
$ busctl tree xyz.openbmc_project.Console.host2
└─ /xyz
  └─ /xyz/openbmc_project
    └─ /xyz/openbmc_project/console
      └─ /xyz/openbmc_project/console/host1
      └─ /xyz/openbmc_project/console/host2

$ busctl tree xyz.openbmc_project.Console.host1
└─ /xyz
  └─ /xyz/openbmc_project
    └─ /xyz/openbmc_project/console
      └─ /xyz/openbmc_project/console/host1
      └─ /xyz/openbmc_project/console/host2
```

```sh
$ busctl introspect xyz.openbmc_project.Console.host1 /xyz/openbmc_project/console/host1
NAME                                TYPE      SIGNATURE RESULT/VALUE FLAGS
org.freedesktop.DBus.Introspectable interface -         -            -
.Introspect                         method    -         s            -
org.freedesktop.DBus.Peer           interface -         -            -
.GetMachineId                       method    -         s            -
.Ping                               method    -         -            -
org.freedesktop.DBus.Properties     interface -         -            -
.Get                                method    ss        v            -
.GetAll                             method    s         a{sv}        -
.Set                                method    ssv       -            -
.PropertiesChanged                  signal    sa{sv}as  -            -
xyz.openbmc_project.Console.Access  interface -         -            -
.Connect                            method    -         h            -
```

## Example Diagram

```text
                                          +--------------------+
                                          | server.conf        |
                                          +--------------------+
                                               |
                                               |
                                               |
                                               |
                                          +----+----+                                 +-----+     +-------+
                                          |         |                                 |     |     |       |
                                          |         |     +-------+     +-------+     |     +-----+ UART1 |
+-----------------------------------+     |         |     |       |     |       |     |     |     |       |
| xyz.openbmc_project.Console.host1 +-----+         +-----+ ttyS0 +-----+ UART0 +-----+     |     +-------+
+-----------------------------------+     |         |     |       |     |       |     |     |
                                          |  obmc   |     +-------+     +-------+     |     |
                                          | console |                                 | MUX |
                                          | server  |                   +-------+     |     |
+-----------------------------------+     |         |                   |       |     |     |
| xyz.openbmc_project.Console.host2 +-----+         +-------------------+ GPIO  +-----+     |     +-------+
+-----------------------------------+     |         |                   |       |     |     |     |       |
                                          |         |                   +-------+     |     +-----+ UART2 |
                                          |         |                                 |     |     |       |
                                          +----+----+                                 +-----+     +-------+

```
