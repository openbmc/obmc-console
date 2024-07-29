#!/bin/bash

# taken from
# https://github.com/amboar/dbus-sensors/commit/deef99485d7671634baee09aac6368b8b839b574

# Support the docker container environment. OpenBMC relies on libsystemd's
# sd_bus APIs, where sd_bus_default() relies on detection of a systemd
# user-slice to determine whether the session or system bus is used. However, in
# the container we do not appear to run in a user-slice, hence sd_bus_default()
# hooks us up to the system bus.

TEST="$1"

export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
export server="$2"
export client="$3"

"${TEST}"
