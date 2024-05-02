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

## Mux Control

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

## Dbus interface

```
NAME                                 TYPE      SIGNATURE RESULT/VALUE FLAGS
xyz.openbmc_project.Console.Control  interface -         -            -
.Activate                            method    b         i            -
.Active                              property  b         true         -
.ConflictingConsoleIds               property  as        1 "console1" -
```

## Example Diagram with 2 server instances

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
