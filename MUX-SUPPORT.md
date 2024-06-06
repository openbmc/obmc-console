## Mux Support

In some hardware designs, multiple UARTS may be available behind a Mux, which
requires obmc-console to select one at a time.

For example, let's say behind `/dev/ttyS1` there are `UART0` and `UART1`, behind
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

## Mux Control

The utility `obmc-console-ctl` can be used to select the active UART, using the
`--console-id`.

It can disconnect all the other consoles using the same device by calling
'Activate(true)' dbus method on the console that should be enabled.

### Example with `/dev/ttyS1`

```
$ obmc-console-ctl --activate --console-id console2
```

Then obmc-console-ctl calls dbus method `Activate()` on console2 which will
cause console1 to:

1. stop forwarding bytes
2. print a log message to its clients, if the server was connected before

Then the following happens for console2:

1. switch the mux using the gpios
2. print a log message to its clients, if the server was disconnected before
3. start forwarding bytes.

## Dbus interface

```
NAME                                 TYPE      SIGNATURE RESULT/VALUE FLAGS
xyz.openbmc_project.Console.Control  interface -         -            -
.Activate                            method    -         i            -
.Active                              property  b         true         -
```

## Example Diagram with 2 console instances

```
                   UART2 -------+
                   UART1 -----+ |
                              | |
                              | |
             +------------+  +------+
             | /dev/ttyS1 |--| Mux  |
             +-------+----+  +-+----+
                     |          |
+-------------+      |          |
|+---------+  |      |          | gpio
||console-1|--+------|----------+
|+---------++ |      |          |
|+---------++-+------+          |
||console-2|--+-----------------+
|+---------+  |
|             |
+-------------+
obmc-console-server
```
