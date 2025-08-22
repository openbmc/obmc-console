# Building obmc-console with Make

This document describes how to build obmc-console using the Make build system, which replaces the original Meson build system for better SONiC compatibility.

## Build Options

obmc-console provides **two build systems** that produce **identical binaries**:

1. **Full Makefile** - Configurable build process with advanced options (this document)
2. **Makefile.simple** - Zero-configuration build process (recommended for SONiC)

**Both produce the same fully-featured binaries with identical functionality.**

## Quick Start (Full Makefile)

```bash
# Configure the build
./configure

# Build the project
make

# Install (requires root)
sudo make install
```

## Quick Start (Simple Makefile)

```bash
# Build directly (no configure needed)
make -f Makefile.simple

# Install binaries only
sudo make -f Makefile.simple install
```

## Dependencies

### Required Dependencies

- **C compiler** (gcc or clang)
- **pkg-config** - for dependency detection
- **libsystemd-dev** - systemd library and headers
- **libiniparser-dev** - INI file parser library
- **libgpiod-dev** - GPIO library for mux support

### Optional Dependencies

- **libudev-dev** - for udev rules (can be disabled with `--disable-udev`)
- **socat** - for integration tests

### Installing Dependencies

#### Ubuntu/Debian
```bash
sudo apt-get install build-essential pkg-config \
    libsystemd-dev libiniparser-dev libgpiod-dev libudev-dev
```

#### CentOS/RHEL/Fedora
```bash
sudo yum install gcc pkg-config systemd-devel \
    iniparser-devel libgpiod-devel libudev-devel
```

## Configuration Options

The `configure` script supports the following options:

```bash
./configure [options]

Options:
  --prefix=PATH              Installation prefix [/usr]
  --sysconfdir=PATH          System configuration directory [/etc]
  --localstatedir=PATH       Local state directory [/var]
  --enable-ssh               Enable SSH support [yes]
  --disable-ssh              Disable SSH support
  --enable-udev              Enable udev rules [yes]
  --disable-udev             Disable udev rules
  --enable-concurrent-servers Enable concurrent servers [no]
  --disable-concurrent-servers Disable concurrent servers
  --enable-tests             Enable test suite [yes]
  --disable-tests            Disable test suite
  --help                     Show help message
```

### Examples

```bash
# Minimal build without SSH or udev support
./configure --disable-ssh --disable-udev

# Build for custom prefix
./configure --prefix=/opt/obmc-console

# Build with concurrent servers support
./configure --enable-concurrent-servers
```

## Build Targets

- `make` or `make all` - Build both server and client
- `make obmc-console-server` - Build only the server
- `make obmc-console-client` - Build only the client
- `make check` - Build and run unit tests
- `make itest` - Run integration tests (requires socat)
- `make install` - Install binaries and configuration files
- `make clean` - Remove build artifacts
- `make distclean` - Remove build artifacts and configuration

## Installation

```bash
# Install to system directories (requires root)
sudo make install

# Install to custom destination (for packaging)
make install DESTDIR=/tmp/package-root

# Install only binaries
make install-binaries

# Install only systemd service files
make install-systemd

# Install only udev rules
make install-udev
```

## Testing

### Unit Tests
```bash
make check
```

### Integration Tests
```bash
# Requires socat to be installed
make itest
```

### Manual Testing
```bash
# Test the build system
./test-build.sh
```

## Cross-Compilation

Set the appropriate environment variables:

```bash
export CC=arm-linux-gnueabihf-gcc
export PKG_CONFIG_PATH=/usr/arm-linux-gnueabihf/lib/pkgconfig
./configure --prefix=/usr
make
```

## SONiC Integration

This Make-based build system is designed to integrate well with SONiC's build environment:

1. **Standard Make interface** - Compatible with SONiC's build scripts
2. **pkg-config dependency detection** - Automatically finds system libraries
3. **Configurable installation paths** - Supports DESTDIR for packaging
4. **Cross-compilation support** - Works with SONiC's cross-compilation setup

### Example SONiC Integration

```makefile
# In SONiC package Makefile
$(addprefix $(DEST)/, $(OBMC_CONSOLE)): $(DEST)/% :
	pushd $(OBMC_CONSOLE_SRC) && \
	./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var && \
	make && \
	make install DESTDIR=$(DEST) && \
	popd
```

## Differences from Meson Build

- **Simplified configuration** - Uses shell script instead of Meson
- **Standard Make** - No special build tools required
- **Better SONiC compatibility** - Follows SONiC build patterns
- **Explicit dependency checking** - Clear error messages for missing dependencies
- **Configurable features** - Enable/disable features at configure time

## Troubleshooting

### Missing Dependencies
If configure fails with missing dependencies, install the required development packages for your distribution.

### pkg-config Issues
If pkg-config can't find libraries, check that the development packages are installed and PKG_CONFIG_PATH is set correctly.

### Cross-compilation Issues
Ensure that the cross-compilation toolchain and target libraries are properly installed and PKG_CONFIG_PATH points to the target's pkg-config files.

## Migration from Meson

To migrate from the Meson build system:

1. Remove Meson build artifacts: `rm -rf build/`
2. Run the new configure script: `./configure`
3. Build with Make: `make`

The functionality and output should be identical to the Meson build.
