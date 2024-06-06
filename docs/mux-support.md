## Mux Support

In some hardware designs, multiple UARTS may be available behind a Mux, which
requires obmc-console to select one at a time.

For example, let's say behind `/dev/ttyS0` there are `UART1` and `UART2`, behind
a mux. GPIO `UART-MUX-CTL` can be used to select one. Then there will be one
obmc-console-server process, with 2 configuration files, so 2 consoles will be
created in this server process.

They will both receive configuration detailing the gpios to be used to control
the mux and the values they should have when they are active.

The kernel will not be aware of this mux and the support can be implemented in
userspace. Reasons for this are that the hardware this is for is not static
(blade server) and thus cannot be represented by devicetree. Also, it is unclear
how the virtual uart should notify a process that the mux state has changed.

When a userspace implementation is available for reference, this may later pave
the way for a kernel implementation if someone wants to do that.

## Configuration Example

The configuration is similar to i2c-mux-gpio in the linux kernel.

The order of GPIOs listed in 'mux-gpios' forms the LSB-first bit representation
of an N-bit number that is taken from 'mux-index'.

```
$ cat conf/uart1.conf
console-id = host1
mux-gpios = MUX_CTL
mux-index = 0
logfile = /var/log/console-host1.log

$ cat conf/uart1.conf
console-id = host2
mux-gpios = MUX_CTL
mux-index = 1
logfile = /var/log/console-host2.log
```

Now the server can be started:

```
$ obmc-console-server --config-dir conf /dev/ttyS0
```

## Mux Control

The utility `obmc-console-ctl` can be used to select the active UART, using the
`--console-id`.

It can disconnect all the other consoles using the same device by calling
'Activate()' dbus method on the console that should be enabled.

### Mux Control - Example

```
$ obmc-console-ctl --activate --console-id host1
```

Then obmc-console-ctl calls dbus method `Activate()` on console 'host1' which
will cause console 'host2' to:

1. stop forwarding bytes
2. print a log message to its clients, if the server was connected before

Then the following happens for console 'host1':

1. switch the mux using the gpios
2. print a log message to its clients, if the server was disconnected before
3. start forwarding bytes.

## Dbus interface - Example

```
$ busctl list
...
xyz.openbmc_project.Console.host1 926 obmc-console-server root ...
xyz.openbmc_project.Console.host2 926 obmc-console-server root ...
...

$ busctl tree xyz.openbmc_project.Console.host2
└─ /xyz
  └─ /xyz/openbmc_project
    └─ /xyz/openbmc_project/console
      └─ /xyz/openbmc_project/console/host2

$ busctl tree xyz.openbmc_project.Console.host1
└─ /xyz
  └─ /xyz/openbmc_project
    └─ /xyz/openbmc_project/console
      └─ /xyz/openbmc_project/console/host1

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
xyz.openbmc_project.Console.Control interface -         -            -
.Activate                           method    -         -            -
.Active                             property  b         true         emits-change
```

## Example Diagram

```
                                          +--------------------+
                                          | uart1.conf         |
                                          +--------------------+
                                          | console-id = host1 |
                                          | mux-index = 0      |
                                          +----+---------------+
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
                                               |
                                               |
                                               |
                                               |
                                          +----+---------------+
                                          | uart2.conf         |
                                          +--------------------+
                                          | console-id = host2 |
                                          | mux-index = 1      |
                                          +--------------------+
```
