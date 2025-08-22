# SONiC Build Integration for obmc-console

This document explains how to integrate obmc-console with SONiC's Make-based build system, replacing the original Meson build system.

## Overview

The obmc-console package has been converted from Meson to Make to better integrate with SONiC's build environment. This conversion provides **two build options**:

1. **Full Makefile** - Complete feature set with configure script
2. **Makefile.simple** - Minimal, zero-configuration build (**Recommended for SONiC**)

## Recommended SONiC Integration (Makefile.simple)

For SONiC integration, **use `Makefile.simple`** which provides:

- **Identical functionality** - Same binaries as full Makefile (100% feature parity)
- **Zero configuration** - No configure script needed
- **Minimal build complexity** - Hardcoded library flags
- **Fast builds** - Direct compilation
- **SONiC compatibility** - Follows SONiC build patterns

### Simple SONiC Package Integration

```makefile
# In your SONiC package Makefile
OBMC_CONSOLE_VERSION = 1.1.0
OBMC_CONSOLE_SRC = obmc-console-$(OBMC_CONSOLE_VERSION)

$(addprefix $(DEST)/, $(OBMC_CONSOLE)): $(DEST)/% :
	# Extract source
	tar xf $(OBMC_CONSOLE_SRC).tar.gz

	# Build with simple Makefile (no configure needed)
	pushd $(OBMC_CONSOLE_SRC) && \
	$(MAKE) -f Makefile.simple && \
	$(MAKE) -f Makefile.simple install DESTDIR=$(DEST) && \
	popd

	# Cleanup
	rm -rf $(OBMC_CONSOLE_SRC)
```

### Required Dependencies in SONiC Build Container

```dockerfile
# Add to SONiC build container
RUN apt-get update && apt-get install -y \
    libsystemd-dev \
    libiniparser-dev \
    libgpiod-dev \
    && rm -rf /var/lib/apt/lists/*
```

### Cross-Compilation with Makefile.simple

```makefile
# Cross-compilation setup
export CC=$(CROSS_COMPILE)gcc

$(addprefix $(DEST)/, $(OBMC_CONSOLE)): $(DEST)/% :
	pushd $(OBMC_CONSOLE_SRC) && \
	$(MAKE) -f Makefile.simple CC=$(CC) && \
	$(MAKE) -f Makefile.simple install DESTDIR=$(DEST) && \
	popd
```

## Alternative: Full Makefile (Advanced Features)

If you need advanced features like systemd service generation, use the full Makefile:

```makefile
# Full Makefile with configure (more complex)
$(addprefix $(DEST)/, $(OBMC_CONSOLE)): $(DEST)/% :
	pushd $(OBMC_CONSOLE_SRC) && \
	./configure \
		--prefix=/usr \
		--sysconfdir=/etc \
		--localstatedir=/var \
		--disable-ssh \
		--disable-udev && \
	$(MAKE) && \
	$(MAKE) install DESTDIR=$(DEST) && \
	popd
```

## Build Configuration Options

### Configure Script Options

```bash
./configure [options]

# Essential options for SONiC
--prefix=/usr                    # Install prefix
--sysconfdir=/etc               # Configuration directory  
--localstatedir=/var            # Variable data directory

# Feature control
--disable-ssh                   # Disable SSH support (recommended for SONiC)
--disable-udev                  # Disable udev rules (may not be needed)
--enable-concurrent-servers     # Enable multiple console instances
--disable-tests                 # Skip test suite (faster builds)

# Cross-compilation
--host=ARCH-linux-gnu          # Target architecture
```

### Environment Variables

```bash
# Compiler settings
CC=gcc                         # C compiler
CFLAGS="-O2 -g"               # Compiler flags
LDFLAGS=""                    # Linker flags

# Cross-compilation
CROSS_COMPILE=arm-linux-gnueabihf-
PKG_CONFIG_PATH=/target/lib/pkgconfig
PKG_CONFIG_SYSROOT_DIR=/target

# Installation
DESTDIR=/tmp/package          # Staging directory
```

## SONiC-Specific Configurations

### Minimal SONiC Build

For a minimal SONiC integration, disable unnecessary features:

```bash
./configure \
    --prefix=/usr \
    --sysconfdir=/etc \
    --localstatedir=/var \
    --disable-ssh \
    --disable-udev \
    --disable-tests
make
make install DESTDIR=$DEST
```

### Full-Featured SONiC Build

For complete functionality:

```bash
./configure \
    --prefix=/usr \
    --sysconfdir=/etc \
    --localstatedir=/var \
    --enable-ssh \
    --enable-udev \
    --enable-concurrent-servers
make
make install DESTDIR=$DEST
```

## Dependency Management

### Required Dependencies

The build system automatically detects these dependencies:

- **libsystemd** - System service management
- **libiniparser** - Configuration file parsing  
- **libgpiod** - GPIO control for mux support

### SONiC Dependency Integration

```makefile
# In SONiC rules/obmc-console.mk
OBMC_CONSOLE_DEPENDS = $(LIBSYSTEMD_DEV) $(LIBINIPARSER_DEV) $(LIBGPIOD_DEV)

$(OBMC_CONSOLE): $(OBMC_CONSOLE_DEPENDS)
	# Build commands here
```

### Fallback for Missing Dependencies

If pkg-config detection fails, the build system falls back to:

```makefile
# Manual library linking
LIBS = -lsystemd -liniparser -lgpiod -lrt
```

## File Installation Layout

The Make build system installs files in standard locations:

```
/usr/sbin/obmc-console-server           # Server binary
/usr/bin/obmc-console-client            # Client binary
/lib/systemd/system/obmc-console@.service    # Systemd service
/lib/systemd/system/obmc-console@.socket     # Systemd socket
/lib/udev/rules.d/80-obmc-console-uart.rules # Udev rules (optional)
```

## Testing Integration

### Unit Tests

```bash
# Run unit tests during build
make check
```

### Integration Tests

```bash
# Run integration tests (requires socat)
make itest
```

### SONiC Test Integration

```makefile
# In SONiC test target
test-obmc-console:
	pushd $(OBMC_CONSOLE_SRC) && \
	make check && \
	popd
```

## Migration from Meson

### Key Differences

| Aspect | Meson | Make |
|--------|-------|------|
| Configuration | `meson setup build` | `./configure` |
| Building | `meson compile -C build` | `make` |
| Testing | `meson test -C build` | `make check` |
| Installation | `meson install -C build` | `make install` |
| Cross-compilation | Built-in | Environment variables |

### Migration Steps

1. **Remove Meson artifacts**: `rm -rf build/`
2. **Configure with Make**: `./configure [options]`
3. **Build**: `make`
4. **Test**: `make check`
5. **Install**: `make install DESTDIR=$DEST`

## Troubleshooting

### Common Issues

**pkg-config not finding libraries**
```bash
# Set PKG_CONFIG_PATH
export PKG_CONFIG_PATH=/usr/lib/pkgconfig:/usr/local/lib/pkgconfig
```

**Cross-compilation failures**
```bash
# Ensure target sysroot is properly configured
export PKG_CONFIG_SYSROOT_DIR=/path/to/target/sysroot
export PKG_CONFIG_PATH=$PKG_CONFIG_SYSROOT_DIR/usr/lib/pkgconfig
```

**Missing dependencies**
```bash
# Use simple Makefile for minimal dependencies
make -f Makefile.simple
```

### Debug Build

```bash
# Enable debug output
./configure --enable-debug
make CFLAGS="-O0 -g -DDEBUG"
```

## Example SONiC Integration

Complete example for SONiC package:

```makefile
# rules/obmc-console.mk
OBMC_CONSOLE_VERSION = 1.1.0
OBMC_CONSOLE = obmc-console_$(OBMC_CONSOLE_VERSION)_$(CONFIGURED_ARCH).deb

$(OBMC_CONSOLE)_SRC_PATH = $(SRC_PATH)/obmc-console
$(OBMC_CONSOLE)_DEPENDS += $(LIBSYSTEMD_DEV) $(LIBINIPARSER_DEV) $(LIBGPIOD_DEV)
$(OBMC_CONSOLE)_RDEPENDS += $(LIBSYSTEMD) $(LIBINIPARSER) $(LIBGPIOD)

SONIC_MAKE_DEBS += $(OBMC_CONSOLE)

# Build rule
$(addprefix $(DEST)/, $(OBMC_CONSOLE)): $(DEST)/% :
	pushd $(OBMC_CONSOLE_SRC_PATH) && \
	./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var && \
	$(MAKE) && \
	$(MAKE) install DESTDIR=$(DEST) && \
	popd
```

This integration provides a robust, SONiC-compatible build system for obmc-console while maintaining all the original functionality.
