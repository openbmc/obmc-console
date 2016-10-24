## To Build
Note: In addition to a toolchain and autoconf tools, this requires `autotools-archive` to be installed.

To build this project, run the following shell commands:

```
./bootstrap.sh
./configure ${CONFIGURE_FLAGS}
make
```

To fully clean the repository, run:
```
./bootstrap.sh clean
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
